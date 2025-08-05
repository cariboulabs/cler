#ifndef CLER_LOGGER_HPP
#define CLER_LOGGER_HPP

#include <cstdint>
#include <cstring>

/**
 * CLER Logger - Cross-platform logger with std::mutex support and log rotation
 * 
 * Basic Usage:
 *   auto ret = cler::start_logging("/path/to/log.txt");
 *   cler::set_log_level(cler::LOG_INFO);
 *   
 *   // Use zf_log macros
 *   ZF_LOGI("Application started");
 *   
 * Log Rotation:
 *   // Enable rotation: 5MB files, keep 3 backups
 *   cler::enable_log_rotation(5 * 1024 * 1024, 3);
 *   
 *   // Files: log.txt (current), log.txt.1, log.txt.2, log.txt.3
 *   // When rotating: log.txt → log.txt.1, oldest (log.txt.3) is deleted
 */

namespace cler {

// === Colors for terminal ===
constexpr const char* COLOR_RED = "\x1b[31m";
constexpr const char* COLOR_YELLOW = "\x1b[33m";
constexpr const char* COLOR_WHITE = "\x1b[37m";
constexpr const char* COLOR_GREEN = "\x1b[32m";
constexpr const char* COLOR_BLUE = "\x1b[34m";
constexpr const char* COLOR_RESET = "\x1b[0m";
constexpr const char* COLOR_DARK_RED = "\x1b[31;1m";

constexpr int LOGGER_MAX_ENUM_STR_LEN = 255;

// Log rotation configuration
struct LogRotationConfig {
    size_t max_file_size = 10 * 1024 * 1024;  // 10MB default
    int max_backup_files = 5;                   // Keep 5 backup files
    bool enabled = false;                       // Disabled by default
};

// === Return codes ===
enum logger_retval_enum {
    LOGGER_SUCCESS = 0,
    LOGGER_FILEPATH_EMPTY,
    LOGGER_ALREADY_STARTED,
    LOGGER_NOT_STARTED,
    LOGGER_COULD_NOT_OPEN_FILE,
    LOGGER_FILE_PTR_IS_NULL,
    LOGGER_FILE_FAILED_FLUSH,
    LOGGER_FILE_INVALID_FD,
    LOGGER_FILE_NOT_SYNCED,
};

// === Convenience functions for file/line for ZF_ADD_LOCATION ===
//   This makes log messages cleaner:
//   - Instead of: /home/user/very/long/path/to/project/src/main.cpp @ line: 
//   42
//   - You get: main.cpp @ line: 42

inline const char* filename(const char* file) noexcept {
    const char* slash = std::strrchr(file, '/');
    const char* backslash = std::strrchr(file, '\\');
    return slash ? slash + 1 : (backslash ? backslash + 1 : file);
}

// Log level constants (matching zf_log values)
constexpr int LOG_VERBOSE = 1;
constexpr int LOG_DEBUG   = 2;
constexpr int LOG_INFO    = 3;
constexpr int LOG_WARN    = 4;
constexpr int LOG_ERROR   = 5;
constexpr int LOG_FATAL   = 6;

// Functions - implemented in logger.cpp
logger_retval_enum start_logging(const char *log_filepath = nullptr) noexcept;
logger_retval_enum reset_logfile(const char *log_filepath) noexcept;
void close_log_file() noexcept;
logger_retval_enum verify_logfile() noexcept;
void logger_enum_to_cstr(logger_retval_enum enum_val, char* out_str) noexcept;
void set_log_level(int level) noexcept;

// Log rotation functions
void enable_log_rotation(size_t max_file_size = 10 * 1024 * 1024, int max_backups = 5) noexcept;
void disable_log_rotation() noexcept;

} // namespace cler

// Macros for zf_log compatibility (kept as macros for __FILE__ and __LINE__)
// These need to be global for use with zf_log macros
#if RELEASE_MODE
    #define ZF_ADD_LOCATION(msg, ...) "%s: " msg, cler::filename(__FILE__), ##__VA_ARGS__
#else
    #define ZF_ADD_LOCATION(msg, ...) "%s @ line: %d: " msg, cler::filename(__FILE__), __LINE__, ##__VA_ARGS__
#endif

#endif // CLER_LOGGER_HPP
