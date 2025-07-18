#pragma once

#include "cler_spsc-queue.hpp"
#include "cler_result.hpp"
#include <array>
#include <algorithm> // for std::min, which a-lot of cler blocks use
#include <complex> //again, a lot of cler blocks use complex numbers
#include <string> //block names are self owning strings
#include <chrono> // for timing measurements in FlowGraph
#include <tuple> // for storing block runners

namespace cler {

    template<size_t MaxLen = 64>
    class EmbeddableString {
    public:
        // Constructors
        constexpr EmbeddableString() : _data{}, _len(0) {}
        
        EmbeddableString(const char* str) : _data{}, _len(0) {
            if (str) append(str);
        }
        
        EmbeddableString(const std::string& str) : _data{}, _len(0) {
            append(str.c_str());
        }
        
        // Copy constructor
        EmbeddableString(const EmbeddableString& other) : _data{}, _len(0) {
            append(other._data);
        }
        
        // Assignment
        EmbeddableString& operator=(const EmbeddableString& other) {
            if (this != &other) {
                _len = 0;
                _data[0] = '\0';
                append(other._data);
            }
            return *this;
        }
        
        // String concatenation
        EmbeddableString operator+(const char* suffix) const {
            EmbeddableString result(*this);
            result.append(suffix);
            return result;
        }
        
        EmbeddableString operator+(const EmbeddableString& suffix) const {
            EmbeddableString result(*this);
            result.append(suffix._data);
            return result;
        }
        
        // Conversions
        operator const char*() const { return _data; }
        const char* c_str() const { return _data; }
        size_t length() const { return _len; }
        bool empty() const { return _len == 0; }
        
    private:
        char _data[MaxLen];
        size_t _len;
        
        void append(const char* str) {
            if (!str) return;
            size_t str_len = strlen(str);
            size_t copy_len = std::min(str_len, MaxLen - _len - 1);
            memcpy(_data + _len, str, copy_len);
            _len += copy_len;
            _data[_len] = '\0';
        }
    };

    enum class Error {
        NotEnoughSamples,
        NotEnoughSpace,
        ProcedureError,
        BadData,
        TERMINATE_FLOWGRAPH,
        TERM_InvalidChannelIndex,
        TERM_ProcedureError,
        TERM_IOError,
        TERM_EOFReached,
    };

    inline const char* to_str(Error error) {
        switch (error) {
            case Error::NotEnoughSpace:
                return "Not enough space in output buffers";
            case Error::NotEnoughSamples:
                return "Not enough samples in input buffers";
            case Error::ProcedureError:
                return "Procedure error";
            case Error::BadData:
                return "Bad data received";
            case Error::TERM_InvalidChannelIndex:
                return "TERM: Invalid channel index";
            case Error::TERM_ProcedureError:
                return "TERM: Procedure error";
            case Error::TERM_IOError:
                return "TERM: IO error";
            case Error::TERM_EOFReached:
                return "TERM: EOF reached";
            default:
                return "Unknown error";
        }
    }
    
    template <typename T>
    struct ChannelBase {
        virtual ~ChannelBase() = default;
        virtual size_t size() const = 0;
        virtual size_t space() const = 0;
        virtual void push(const T&) = 0;
        virtual void pop(T&) = 0;
        virtual bool try_push(const T&) = 0;
        virtual bool try_pop(T&) = 0;
        virtual size_t writeN(const T* data, size_t n) = 0;
        virtual size_t readN(T* data, size_t n) = 0;
        virtual size_t peek_write(T*& ptr1, size_t& size1, T*& ptr2, size_t& size2) = 0;
        virtual void commit_write(size_t count) = 0;
        virtual size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) = 0;
        virtual void commit_read(size_t count) = 0;
    };

    //allows for heap/stack allocation to share the same interface
    template <typename T, size_t N = 0>
    struct Channel : public ChannelBase<T> {
        dro::SPSCQueue<T, N> _queue;

        Channel() = default;
        
        // Constructor for dynamic size (N == 0)
        template<size_t M = N, typename = std::enable_if_t<M == 0>>
        Channel(size_t size) : _queue(size) {
            if (size == 0) {
                throw std::invalid_argument("Channel size must be greater than zero.");
            }
        }

        size_t size() const override { return _queue.size(); }
        size_t space() const override { return _queue.space(); }

        void push(const T& v) override { _queue.push(v); }
        void pop(T& v) override { _queue.pop(v); }

        bool try_push(const T& v) override {
            return _queue.try_push(v);
        }
        bool try_pop(T& v) override {
            return _queue.try_pop(v);
        }

        size_t writeN(const T* data, size_t n) override {
            return _queue.writeN(data, n);
        }
        size_t readN(T* data, size_t n) override {
            return _queue.readN(data, n);
        }
        size_t peek_write(T*& ptr1, size_t& size1, T*& ptr2, size_t& size2) override {
            return _queue.peek_write(ptr1, size1, ptr2, size2);
        }

        void commit_write(size_t count) override {
            _queue.commit_write(count);
        }

        size_t peek_read(const T*& ptr1, size_t& size1, const T*& ptr2, size_t& size2) override {
            return _queue.peek_read(ptr1, size1, ptr2, size2);
        }

        void commit_read(size_t count) override {
            _queue.commit_read(count);
        }
    };

    struct BlockBase {
        explicit BlockBase(const char* name) : _name(name) {}
        explicit BlockBase(const EmbeddableString<64>& name) : _name(name) {}
        const char* name() const { return _name.c_str(); }

        //OUR CHANNELS ARE NOT COPYABLE OR MOVABLE, SO OUR BLOCKS CAN'T BE EITHER
        // Non-copyable
        BlockBase(const BlockBase&) = delete;
        BlockBase& operator=(const BlockBase&) = delete;

        // Non-movable
        BlockBase(BlockBase&&) = delete;
        BlockBase& operator=(BlockBase&&) = delete;
    private:
       EmbeddableString<64> _name;
    };
    // Helper to convert Channel<T,N> to ChannelBase<T> for deduction
    template<typename T>
    struct channel_to_base { using type = T; };
    
    template<typename T, size_t N>
    struct channel_to_base<Channel<T, N>> { using type = ChannelBase<T>; };
    
    template<typename T>
    using channel_to_base_t = typename channel_to_base<T>::type;

    template<typename Block, typename... Channels>
    struct BlockRunner {
        Block* block;
        std::tuple<Channels*...> outputs;

        template<typename... InputChannels>
        BlockRunner(Block* blk, InputChannels*... outs)
            : block(blk), outputs(static_cast<Channels*>(outs)...) {}
    };
    
    // Deduction guide that converts Channel<T,N> to ChannelBase<T>
    template<typename Block, typename... Channels>
    BlockRunner(Block*, Channels*...) -> BlockRunner<Block, channel_to_base_t<Channels>...>;

    struct BlockExecutionStats {
        EmbeddableString<64> name;
        size_t successful_procedures = 0;
        size_t failed_procedures = 0;
        double avg_dead_time_us = 0.0;
        double total_dead_time_s = 0.0;
        double final_adaptive_sleep_us = 0.0;
        double total_runtime_s = 0.0;
    };

    struct FlowGraphConfig {
        bool adaptive_sleep = false;           // Enable or disable adaptive backoff when idle
        double adaptive_sleep_ramp_up_factor = 1.5; // Multiplier to ramp up sleep when stalling
        double adaptive_sleep_max_us = 5000.0;      // Cap sleep to avoid excessive delays (us)
        double adaptive_sleep_target_gain = 0.5;    // Portion of measured dead time to use as target
        double adaptive_sleep_decay_factor = 0.8;   // How fast to shrink sleep when conditions improve
        size_t adaptive_sleep_consecutive_fail_threshold = 50; // Number of consecutive failures before ramping up sleep
    };

    template<typename TaskPolicy, typename... BlockRunners>
    class FlowGraph {
    public:
        static constexpr std::size_t _N = sizeof...(BlockRunners);
        typedef void (*OnCrashCallback)(void* context);

        FlowGraph(BlockRunners... runners)
            : _runners(std::make_tuple(std::forward<BlockRunners>(std::move(runners))...)) {
            // std::array initialization handled by default constructor
        }

        ~FlowGraph() { stop(); }

        FlowGraph(const FlowGraph&) = delete;
        FlowGraph(FlowGraph&&) = delete;
        FlowGraph& operator=(const FlowGraph&) = delete;
        FlowGraph& operator=(FlowGraph&&) = delete;

    void set_on_crash_cb(OnCrashCallback cb, void* context) {
            _on_crash_cb = cb;
            _on_crash_context = context;
        }

        OnCrashCallback on_crash_cb() const { return _on_crash_cb; }
        void* on_crash_context() const { return _on_crash_context; }

    void run(FlowGraphConfig config = FlowGraphConfig{}) {
        _config = config;
        _stop_flag = false;

        auto launch_tasks = [this]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((_tasks[Is] = TaskPolicy::create_task([this]() {
                auto& runner = std::get<Is>(_runners);
                auto& stats  = _stats[Is];

                stats.name = runner.block->name();

                auto t_start = std::chrono::steady_clock::now();
                auto t_last  = t_start;

                size_t successful = 0;
                size_t failed = 0;
                double avg_dead_time_s = 0.0;
                double total_dead_time_s = 0.0;
                double adaptive_sleep_us = 0.0;
                size_t consecutive_fails = 0;

                while (!_stop_flag) {
                    auto t_now = std::chrono::steady_clock::now();
                    std::chrono::duration<double> dt = t_now - t_last;
                    t_last = t_now;

                    Result<Empty, Error> result = std::apply([&](auto*... outs) {
                        return runner.block->procedure(outs...);
                    }, runner.outputs);

                    if (result.is_err()) {
                        failed++;
                        auto err = result.unwrap_err();

                        if (err > Error::TERMINATE_FLOWGRAPH) {
                            _stop_flag = true;
                            if (_on_crash_cb) {
                                _on_crash_cb(_on_crash_context);
                            }
                            return; // Terminate the flow graph
                        }

                        if (err == Error::NotEnoughSamples || err == Error::NotEnoughSpace) {
                            total_dead_time_s += dt.count();
                            avg_dead_time_s += (dt.count() - avg_dead_time_s) / failed;
                            consecutive_fails++;

                            if (_config.adaptive_sleep) {
                                if (consecutive_fails > _config.adaptive_sleep_consecutive_fail_threshold) {
                                    adaptive_sleep_us = std::min(
                                        adaptive_sleep_us * _config.adaptive_sleep_ramp_up_factor + 1.0,
                                        _config.adaptive_sleep_max_us
                                    );
                                }

                                double desired_us = avg_dead_time_s * 1e6 * _config.adaptive_sleep_target_gain;
                                adaptive_sleep_us = std::max(adaptive_sleep_us, desired_us);

                                TaskPolicy::sleep_us(static_cast<size_t>(adaptive_sleep_us));
                            } else {
                                TaskPolicy::yield();
                            }

                        } else {
                            TaskPolicy::yield();
                        }

                    } else {
                        successful++;
                        consecutive_fails = 0;

                        if (_config.adaptive_sleep) {
                            adaptive_sleep_us *= _config.adaptive_sleep_decay_factor;
                        }
                    }
                }

                auto t_end = std::chrono::steady_clock::now();
                std::chrono::duration<double> total_runtime_s = t_end - t_start;

                stats.successful_procedures = successful;
                stats.failed_procedures = failed;
                stats.avg_dead_time_us = avg_dead_time_s * 1e6;
                stats.total_dead_time_s = total_dead_time_s;
                stats.final_adaptive_sleep_us = _config.adaptive_sleep ? adaptive_sleep_us : 0.0;
                stats.total_runtime_s = total_runtime_s.count();
            })), ...);
        };

        launch_tasks(std::make_index_sequence<_N>{});
    }

        void stop() {
            _stop_flag = true;
            for (auto& t : _tasks) {
                TaskPolicy::join_task(t);
            }
        }

        bool is_stopped() const {
            return _stop_flag.load(std::memory_order_seq_cst);
        }

        const FlowGraphConfig& config() const {
            return _config;
        }

        const std::array<BlockExecutionStats, _N>& stats() const {
            return _stats;
        }

    private:
        std::tuple<BlockRunners...> _runners;
        std::array<typename TaskPolicy::task_type, _N> _tasks;
        std::atomic<bool> _stop_flag = false;
        FlowGraphConfig _config;
        std::array<BlockExecutionStats, _N> _stats;
        OnCrashCallback _on_crash_cb = nullptr;
        void* _on_crash_context = nullptr;
    };

    constexpr size_t DEFAULT_BUFFER_SIZE = 1024;
    constexpr float PI = 3.14159265358979323846f;

} // namespace cler