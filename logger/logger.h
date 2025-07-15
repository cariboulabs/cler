#ifndef ZF_LOGGER_H
#define ZF_LOGGER_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// === Colors for terminal ===
#define COLOR_RED "\x1b[31m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_WHITE "\x1b[37m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_BLUE "\x1b[34m"
#define COLOR_RESET "\x1b[0m"
#define COLOR_DARK_RED "\x1b[31;1m"

#define LOGGER_MAX_ENUM_STR_LEN 255

// === Return codes ===
typedef enum {
    LOGGER_SUCCESS = 0,
    LOGGER_FILEPATH_EMPTY,
    LOGGER_ALREADY_STARTED,
    LOGGER_NOT_STARTED,
    LOGGER_COULD_NOT_OPEN_FILE,
    LOGGER_FILE_PTR_IS_NULL,
    LOGGER_FILE_FAILED_FLUSH,
    LOGGER_FILE_INVALID_FD,
    LOGGER_FILE_NOT_SYNCED,
} logger_retval_enum;

// === Convenience macro for file/line ===
#define FILENAME(file) (strrchr(file, '/') ? strrchr(file, '/') + 1 : (strrchr(file, '\\') ? strrchr(file, '\\') + 1 : file))
#if RELEASE_MODE
    #define ZF_ADD_LOCATION(msg, ...) "%s: " msg, FILENAME(__FILE__), ##__VA_ARGS__
#else
    #define ZF_ADD_LOCATION(msg, ...) "%s @ line: %d: " msg, FILENAME(__FILE__), __LINE__, ##__VA_ARGS__
#endif

// === API ===
logger_retval_enum start_logging(const char *log_filepath);
logger_retval_enum reset_logfile(const char *log_filepath);
void close_log_file();
logger_retval_enum verify_logfile();
void logger_enum_to_cstr(logger_retval_enum enum_val, char* out_str);

#ifdef __cplusplus
}
#endif

#endif // LOGGER_H
