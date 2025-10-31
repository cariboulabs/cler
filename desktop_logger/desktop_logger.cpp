#include "desktop_logger.hpp"
#include "zf_log.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <mutex>
#include <string>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
// No filesystem needed - using C functions for C++11 compatibility

static FILE* _log_file = nullptr;
static bool _logger_started = false;
static std::mutex log_mutex;
static std::string _log_filepath;
static cler::LogRotationConfig _rotation_config;
static std::atomic<bool> _shutting_down{false};

static void _unguarded_close_log_file() noexcept {
    if (_log_file) {
        fclose(_log_file);
    }
    _log_file = nullptr;
}

// Rotate log files: log.txt -> log.1.txt, log.1.txt -> log.2.txt, etc.
static void rotate_log_files() noexcept {
    if (_log_filepath.empty() || !_log_file) return;
    
    // Close current file
    fclose(_log_file);
    _log_file = nullptr;
    
    // Delete the oldest backup if it exists
    std::string oldest = _log_filepath + "." + std::to_string(_rotation_config.max_backup_files);
    std::remove(oldest.c_str());  // Ignore error
    
    // Rotate existing backups
    for (int i = _rotation_config.max_backup_files - 1; i > 0; --i) {
        std::string old_name = _log_filepath + "." + std::to_string(i);
        std::string new_name = _log_filepath + "." + std::to_string(i + 1);
        std::rename(old_name.c_str(), new_name.c_str());  // Ignore error
    }
    
    // Rename current log to .1
    std::rename(_log_filepath.c_str(), (_log_filepath + ".1").c_str());
    // If rename fails, the file stays as is - that's OK
    
    // Always try to open a new log file (or reopen existing if rename failed)
    _log_file = fopen(_log_filepath.c_str(), "a");
    
    // If we can't open the file, rotation is disabled
    if (!_log_file) {
        _rotation_config.enabled = false;
    }
}

// Check if log rotation is needed
static void check_and_rotate_if_needed() noexcept {
    if (!_rotation_config.enabled || !_log_file) return;
    
    try {
        // Get actual file size by seeking to end
        fflush(_log_file);  // Ensure all data is written
        long saved_pos = ftell(_log_file);
        if (saved_pos < 0) return;
        
        if (fseek(_log_file, 0, SEEK_END) != 0) return;
        long file_size = ftell(_log_file);
        fseek(_log_file, saved_pos, SEEK_SET);  // Restore position
        
        if (file_size < 0) return;
        
        if (static_cast<size_t>(file_size) >= _rotation_config.max_file_size) {
            rotate_log_files();
        }
    } catch (const std::bad_alloc&) {
        // Memory exhaustion - skip rotation
    }
}

static void _thread_safe_close_log_file() noexcept {
    _shutting_down = true;
    std::lock_guard<std::mutex> lock(log_mutex);
    _unguarded_close_log_file();
}

namespace cler {

logger_retval_enum reset_logfile(const char *log_filepath) noexcept {
    if (!log_filepath) return cler::LOGGER_FILEPATH_EMPTY;
    if (!_logger_started) return cler::LOGGER_NOT_STARTED;

    std::lock_guard<std::mutex> lock(log_mutex);
    _unguarded_close_log_file();

    _log_filepath = log_filepath;
    _log_file = fopen(log_filepath, "a");
    if (!_log_file) {
        _log_filepath.clear();
        return cler::LOGGER_COULD_NOT_OPEN_FILE;
    }

    return cler::LOGGER_SUCCESS;
}

logger_retval_enum verify_logfile() noexcept {
    std::lock_guard<std::mutex> lock(log_mutex);

    if (!_log_file) {
        return cler::LOGGER_FILE_PTR_IS_NULL;
    }

    if (fflush(_log_file) != 0) {
        _unguarded_close_log_file();
        return cler::LOGGER_FILE_FAILED_FLUSH;
    }

    int fd = fileno(_log_file);
    if (fd < 0) {
        _unguarded_close_log_file();
        return cler::LOGGER_FILE_INVALID_FD;
    }

    if (fcntl(fd, F_GETFL) == -1) {
        _unguarded_close_log_file();
        return cler::LOGGER_FILE_INVALID_FD;
    }

    if (fsync(fd) != 0) {
        _unguarded_close_log_file();
        return cler::LOGGER_FILE_NOT_SYNCED;
    }

    return cler::LOGGER_SUCCESS;
}

static void zf_output_callback(const zf_log_message *msg, void *arg) {
    (void)arg;
    
    // Check if we're shutting down to avoid race conditions
    if (_shutting_down.load(std::memory_order_relaxed)) {
        return;
    }

    // Thread-local storage for better thread safety
    thread_local char time_str[64];
    thread_local struct tm tm_buf;
    
    time_t t = time(nullptr);

    // Use thread-safe localtime_r on POSIX systems
    struct tm *tm = localtime_r(&t, &tm_buf);
    if (tm == nullptr) {
        time_str[0] = '\0';
    } else {
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
    }

    const char *color, *lvl_char;
    switch (msg->lvl) {
        case ZF_LOG_VERBOSE: color = cler::COLOR_GREEN; lvl_char = "v"; break;
        case ZF_LOG_DEBUG: color = cler::COLOR_BLUE; lvl_char = "d"; break;
        case ZF_LOG_INFO: color = cler::COLOR_WHITE; lvl_char = "I"; break;
        case ZF_LOG_WARN: color = cler::COLOR_YELLOW; lvl_char = "W"; break;
        case ZF_LOG_ERROR: color = cler::COLOR_RED; lvl_char = "E"; break;
        case ZF_LOG_FATAL: color = cler::COLOR_DARK_RED; lvl_char = "F"; break;
        default: color = cler::COLOR_WHITE; lvl_char = "N"; break;
    }

    fprintf(stdout, "%s[%s] [%s] %.*s%s\n",
        color, time_str, lvl_char,
        (int)(msg->p - msg->msg_b), msg->msg_b, cler::COLOR_RESET);
    fflush(stdout);

    {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (_log_file) {
            // Check for rotation before writing
            check_and_rotate_if_needed();
            
            if (_log_file) {  // Re-check after potential rotation
                fprintf(_log_file, "[%s] [%s] %.*s\n",
                    time_str, lvl_char,
                    (int)(msg->p - msg->msg_b), msg->msg_b);
                fflush(_log_file);
            }
        }
    }
}

logger_retval_enum start_logging(const char *log_filepath) noexcept {
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (_logger_started) {
            return cler::LOGGER_ALREADY_STARTED;
        }

        zf_log_set_output_v(ZF_LOG_PUT_STD, nullptr, zf_output_callback);
        _logger_started = true;
        atexit(_thread_safe_close_log_file);
    }
    // Lock released here automatically
    
    if (log_filepath) {
        return reset_logfile(log_filepath);
    } else {
        return cler::LOGGER_SUCCESS;
    }
}

void logger_enum_to_cstr(logger_retval_enum enum_val, char* out_str) noexcept {
    if (!out_str) return;
    
    const char* str = nullptr;
    switch (enum_val) {
        case cler::LOGGER_SUCCESS: str = "LOGGER_SUCCESS"; break;
        case cler::LOGGER_FILEPATH_EMPTY: str = "LOGGER_FILEPATH_EMPTY"; break;
        case cler::LOGGER_ALREADY_STARTED: str = "LOGGER_ALREADY_STARTED"; break;
        case cler::LOGGER_NOT_STARTED: str = "LOGGER_NOT_STARTED"; break;
        case cler::LOGGER_COULD_NOT_OPEN_FILE: str = "LOGGER_COULD_NOT_OPEN_FILE"; break;
        case cler::LOGGER_FILE_PTR_IS_NULL: str = "LOGGER_FILE_PTR_IS_NULL"; break;
        case cler::LOGGER_FILE_FAILED_FLUSH: str = "LOGGER_FILE_FAILED_FLUSH"; break;
        case cler::LOGGER_FILE_INVALID_FD: str = "LOGGER_FILE_INVALID_FD"; break;
        case cler::LOGGER_FILE_NOT_SYNCED: str = "LOGGER_FILE_NOT_SYNCED"; break;
        default: str = "UNKNOWN"; break;
    }
    
    // Safe string copy with guaranteed null termination
    std::strncpy(out_str, str, cler::LOGGER_MAX_ENUM_STR_LEN - 1);
    out_str[cler::LOGGER_MAX_ENUM_STR_LEN - 1] = '\0';
}

void close_log_file() noexcept {
    std::lock_guard<std::mutex> lock(log_mutex);
    _unguarded_close_log_file();
}

void set_log_level(int level) noexcept {
    // zf_log uses the same level values we defined
    zf_log_set_output_level(level);
}

void enable_log_rotation(size_t max_file_size, int max_backups) noexcept {
    std::lock_guard<std::mutex> lock(log_mutex);
    
    // Validate parameters
    if (max_file_size < 1024) {  // Minimum 1KB
        max_file_size = 1024;
    }
    if (max_backups < 1) {
        max_backups = 1;
    } else if (max_backups > 100) {  // Reasonable maximum
        max_backups = 100;
    }
    
    _rotation_config.enabled = true;
    _rotation_config.max_file_size = max_file_size;
    _rotation_config.max_backup_files = max_backups;
}

void disable_log_rotation() noexcept {
    std::lock_guard<std::mutex> lock(log_mutex);
    _rotation_config.enabled = false;
}

} // namespace cler