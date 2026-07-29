#pragma once

#include "cler.hpp"
#include <algorithm>

// Rising edge trigger block - Normal mode, 10% pre-trigger
template<typename T = float>
struct TriggerBlock : public cler::BlockBase {
    cler::Channel<T> in;
    
    TriggerBlock(const char* name,
                 float threshold_db,      // Trigger threshold in dB
                 float window_ms,         // Display window in milliseconds
                 size_t sample_rate,      // Sample rate for time calculation
                 float holdoff_ms = 100.0f,  // Minimum time between triggers
                 size_t buffer_size = 65536)
        : BlockBase(name),
          in(buffer_size),
          _threshold_db(threshold_db),
          _sample_rate(sample_rate)
    {
        // Calculate window size in samples
        _window_samples = static_cast<size_t>((window_ms / 1000.0f) * sample_rate);
        
        // 10% pre-trigger (hard-coded)
        _pretrigger_samples = static_cast<size_t>(_window_samples * 0.1f);
        _posttrigger_samples = _window_samples - _pretrigger_samples;
        
        // Calculate holdoff in samples
        _holdoff_samples = static_cast<size_t>((holdoff_ms / 1000.0f) * sample_rate);
        
        // Allocate buffers
        _pretrigger_buffer = new T[_pretrigger_samples];
        _capture_buffer = new T[_window_samples];
        
        // Initialize state
        std::fill_n(_pretrigger_buffer, _pretrigger_samples, -120.0f);
        _write_ptr = 0;
        _state = State::WAITING_FOR_TRIGGER;
        _was_below_threshold = true;
        _holdoff_counter = 0;
    }
    
    ~TriggerBlock() {
        delete[] _pretrigger_buffer;
        delete[] _capture_buffer;
    }
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out_base) {
        auto* out = static_cast<cler::Channel<T>*>(out_base);
        
        size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }
        
        T sample;
        for (size_t i = 0; i < available; ++i) {
            in.pop(sample);
            
            switch (_state) {
                case State::WAITING_FOR_TRIGGER:
                    // Maintain circular pre-trigger buffer
                    _pretrigger_buffer[_write_ptr] = sample;
                    _write_ptr = (_write_ptr + 1) % _pretrigger_samples;
                    
                    // Check for rising edge ONLY if not in holdoff
                    if (_holdoff_counter > 0) {
                        _holdoff_counter--;
                    } else if (_was_below_threshold && sample >= _threshold_db) {
                        // Trigger detected! Copy pre-trigger data to output
                        for (size_t j = 0; j < _pretrigger_samples; ++j) {
                            size_t idx = (_write_ptr + j) % _pretrigger_samples;
                            _capture_buffer[j] = _pretrigger_buffer[idx];
                        }
                        
                        // Output pre-trigger samples immediately
                        for (size_t j = 0; j < _pretrigger_samples; ++j) {
                            out->push(_capture_buffer[j]);
                        }
                        
                        _capture_index = 0;
                        _state = State::CAPTURING;
                    }
                    
                    _was_below_threshold = (sample < _threshold_db);
                    break;
                    
                case State::CAPTURING:
                    // Output post-trigger data
                    out->push(sample);
                    _capture_index++;
                    
                    if (_capture_index >= _posttrigger_samples) {
                        // Capture complete
                        _state = State::WAITING_FOR_TRIGGER;
                        _was_below_threshold = true;
                        _holdoff_counter = _holdoff_samples;  // Start holdoff
                    }
                    break;
            }
        }
        
        return cler::Empty{};
    }
    
private:
    enum class State {
        WAITING_FOR_TRIGGER,
        CAPTURING
    };
    
    float _threshold_db;
    size_t _sample_rate;
    size_t _window_samples;
    size_t _pretrigger_samples;
    size_t _posttrigger_samples;
    size_t _holdoff_samples;
    
    T* _pretrigger_buffer;  // Circular buffer for pre-trigger data
    T* _capture_buffer;     // Full capture window
    
    size_t _write_ptr;      // Write pointer for circular buffer
    size_t _capture_index;  // Current position in post-trigger capture
    size_t _holdoff_counter; // Samples remaining in holdoff
    
    State _state;
    bool _was_below_threshold;
};