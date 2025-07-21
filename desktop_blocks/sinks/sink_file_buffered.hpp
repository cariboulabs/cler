#pragma once

#include "cler.hpp"
#include <fstream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

template <typename T>
struct SinkFileBufferedBlock : public cler::BlockBase {
    cler::Channel<T> in;

    SinkFileBufferedBlock(std::string name, const char* filename, 
                          size_t buffer_size = cler::DEFAULT_BUFFER_SIZE,
                          size_t num_buffers = 4)
        : cler::BlockBase(std::move(name)), 
          in(buffer_size), 
          _filename(filename),
          _buffer_size(buffer_size),
          _num_buffers(num_buffers),
          _stop_thread(false) {

        if (buffer_size == 0) {
            throw std::invalid_argument("Buffer size must be greater than zero.");
        }
        if (!filename || filename[0] == '\0') {
            throw std::invalid_argument("Filename must not be empty.");
        }
        if (num_buffers < 2) {
            throw std::invalid_argument("Need at least 2 buffers for double buffering.");
        }

        _file.open(_filename, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!_file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + std::string(filename));
        }

        // Allocate multiple buffers
        _buffers.resize(_num_buffers);
        for (size_t i = 0; i < _num_buffers; ++i) {
            _buffers[i].data = new T[buffer_size];
            _buffers[i].size = 0;
            _buffers[i].ready = false;
        }

        // Start writer thread
        _writer_thread = std::thread(&SinkFileBufferedBlock::writer_thread_func, this);
    }

    ~SinkFileBufferedBlock() {
        // Signal thread to stop
        _stop_thread = true;
        _cv.notify_all();
        
        // Wait for thread to finish
        if (_writer_thread.joinable()) {
            _writer_thread.join();
        }

        if (_file.is_open()) {
            _file.close();
        }

        // Clean up buffers
        for (auto& buf : _buffers) {
            if (buf.data) {
                delete[] buf.data;
            }
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available_samples = in.size();
        if (available_samples == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Find next free buffer
        Buffer* free_buffer = nullptr;
        for (size_t i = 0; i < _num_buffers; ++i) {
            size_t idx = (_current_write_buffer + i) % _num_buffers;
            if (!_buffers[idx].ready) {
                free_buffer = &_buffers[idx];
                _current_write_buffer = (idx + 1) % _num_buffers;
                break;
            }
        }

        if (!free_buffer) {
            // All buffers full - this indicates we can't keep up with data rate
            _overflow_count++;
            return cler::Error::NotEnoughSpace;
        }

        // Fill the buffer
        size_t to_read = std::min(available_samples, _buffer_size);
        in.readN(free_buffer->data, to_read);
        free_buffer->size = to_read;
        
        // Mark buffer as ready for writing
        {
            std::lock_guard<std::mutex> lock(_mutex);
            free_buffer->ready = true;
        }
        _cv.notify_one();

        return cler::Empty{};
    }

    size_t get_overflow_count() const { return _overflow_count; }

private:
    struct Buffer {
        T* data = nullptr;
        size_t size = 0;
        std::atomic<bool> ready{false};
    };

    void writer_thread_func() {
        while (!_stop_thread) {
            std::unique_lock<std::mutex> lock(_mutex);
            
            // Wait for data or stop signal
            _cv.wait(lock, [this] {
                return _stop_thread || 
                       std::any_of(_buffers.begin(), _buffers.end(), 
                                   [](const Buffer& b) { return b.ready.load(); });
            });

            if (_stop_thread) {
                // Write any remaining data before exiting
                for (auto& buf : _buffers) {
                    if (buf.ready) {
                        _file.write(reinterpret_cast<char*>(buf.data), buf.size * sizeof(T));
                        buf.ready = false;
                    }
                }
                break;
            }

            // Write all ready buffers in order
            for (size_t i = 0; i < _num_buffers; ++i) {
                size_t idx = (_current_read_buffer + i) % _num_buffers;
                if (_buffers[idx].ready) {
                    lock.unlock();  // Don't hold lock during I/O
                    
                    _file.write(reinterpret_cast<char*>(_buffers[idx].data), 
                               _buffers[idx].size * sizeof(T));
                    
                    _buffers[idx].ready = false;
                    _current_read_buffer = (idx + 1) % _num_buffers;
                    
                    lock.lock();
                } else {
                    break;  // Maintain order
                }
            }
        }
    }

    const char* _filename;
    std::ofstream _file;
    size_t _buffer_size;
    size_t _num_buffers;
    
    std::vector<Buffer> _buffers;
    size_t _current_write_buffer = 0;
    size_t _current_read_buffer = 0;
    
    std::thread _writer_thread;
    std::atomic<bool> _stop_thread;
    std::mutex _mutex;
    std::condition_variable _cv;
    
    std::atomic<size_t> _overflow_count{0};
};