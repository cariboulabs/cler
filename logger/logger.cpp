#include "logger.h"
#include "zf_log.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <mutex>

#ifdef _WIN32
    #include <io.h>
    #include <windows.h>
    #define fileno _fileno
    #define fsync _commit
    // For Windows console colors
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

static FILE* _log_file = nullptr;
static bool _logger_started = false;
static std::mutex log_mutex;

#ifdef _WIN32
static void enable_windows_ansi_colors() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
        return;
    }

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}
#endif

static void _unguarded_close_log_file() {
    if (_log_file) {
        fclose(_log_file);
    }
    _log_file = nullptr;
}

void close_log_file() {
    std::lock_guard<std::mutex> lock(log_mutex);
    _unguarded_close_log_file();
}

logger_retval_enum reset_logfile(const char *log_filepath) {
    if (!log_filepath) return LOGGER_FILEPATH_EMPTY;
    if (!_logger_started) return LOGGER_NOT_STARTED;

    std::lock_guard<std::mutex> lock(log_mutex);
    _unguarded_close_log_file();

    _log_file = fopen(log_filepath, "a");
    if (!_log_file) {
        return LOGGER_COULD_NOT_OPEN_FILE;
    }

    return LOGGER_SUCCESS;
}

logger_retval_enum verify_logfile() {
    std::lock_guard<std::mutex> lock(log_mutex);

    if (!_log_file) {
        return LOGGER_FILE_PTR_IS_NULL;
    }

    if (fflush(_log_file) != 0) {
        _unguarded_close_log_file();
        return LOGGER_FILE_FAILED_FLUSH;
    }

    int fd = fileno(_log_file);
    if (fd < 0) {
        _unguarded_close_log_file();
        return LOGGER_FILE_INVALID_FD;
    }

#ifdef _WIN32
    // On Windows, we can't use fcntl, so we'll just check if the handle is valid
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        _unguarded_close_log_file();
        return LOGGER_FILE_INVALID_FD;
    }
#else
    if (fcntl(fd, F_GETFL) == -1) {
        _unguarded_close_log_file();
        return LOGGER_FILE_INVALID_FD;
    }
#endif

    if (fsync(fd) != 0) {
        _unguarded_close_log_file();
        return LOGGER_FILE_NOT_SYNCED;
    }

    return LOGGER_SUCCESS;
}

static void zf_output_callback(const zf_log_message *msg, void *arg) {
    (void)arg;

    time_t t = time(nullptr);
    struct tm *tm = localtime(&t);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);

    const char *color, *lvl_char;
    switch (msg->lvl) {
        case ZF_LOG_VERBOSE: color = COLOR_GREEN; lvl_char = "v"; break;
        case ZF_LOG_DEBUG: color = COLOR_BLUE; lvl_char = "d"; break;
        case ZF_LOG_INFO: color = COLOR_WHITE; lvl_char = "I"; break;
        case ZF_LOG_WARN: color = COLOR_YELLOW; lvl_char = "W"; break;
        case ZF_LOG_ERROR: color = COLOR_RED; lvl_char = "E"; break;
        case ZF_LOG_FATAL: color = COLOR_DARK_RED; lvl_char = "F"; break;
        default: color = COLOR_WHITE; lvl_char = "N"; break;
    }

    fprintf(stdout, "%s[%s] [%s] %.*s%s\n",
        color, time_str, lvl_char,
        (int)(msg->p - msg->msg_b), msg->msg_b, COLOR_RESET);
    fflush(stdout);

    {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (_log_file) {
            fprintf(_log_file, "[%s] [%s] %.*s\n",
                time_str, lvl_char,
                (int)(msg->p - msg->msg_b), msg->msg_b);
            fflush(_log_file);
        }
    }
}

extern "C" {

logger_retval_enum start_logging(const char *log_filepath) {
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (_logger_started) {
            return LOGGER_ALREADY_STARTED;
        }
        
#ifdef _WIN32
        enable_windows_ansi_colors();
#endif
        
        zf_log_set_output_v(ZF_LOG_PUT_STD, nullptr, zf_output_callback);
        _logger_started = true;
        atexit(_unguarded_close_log_file);
    }
    // Lock released here automatically
    
    if (log_filepath) {
        return reset_logfile(log_filepath);
    } else {
        return LOGGER_SUCCESS;
    }
}

void logger_enum_to_cstr(logger_retval_enum enum_val, char* out_str) {
    if (!out_str) return;
    memset(out_str, 0, LOGGER_MAX_ENUM_STR_LEN);

    switch (enum_val) {
        case LOGGER_SUCCESS: strcpy(out_str, "LOGGER_SUCCESS"); break;
        case LOGGER_FILEPATH_EMPTY: strcpy(out_str, "LOGGER_FILEPATH_EMPTY"); break;
        case LOGGER_ALREADY_STARTED: strcpy(out_str, "LOGGER_ALREADY_STARTED"); break;
        case LOGGER_NOT_STARTED: strcpy(out_str, "LOGGER_NOT_STARTED"); break;
        case LOGGER_COULD_NOT_OPEN_FILE: strcpy(out_str, "LOGGER_COULD_NOT_OPEN_FILE"); break;
        case LOGGER_FILE_PTR_IS_NULL: strcpy(out_str, "LOGGER_FILE_PTR_IS_NULL"); break;
        case LOGGER_FILE_FAILED_FLUSH: strcpy(out_str, "LOGGER_FILE_FAILED_FLUSH"); break;
        case LOGGER_FILE_INVALID_FD: strcpy(out_str, "LOGGER_FILE_INVALID_FD"); break;
        case LOGGER_FILE_NOT_SYNCED: strcpy(out_str, "LOGGER_FILE_NOT_SYNCED"); break;
        default: strcpy(out_str, "UNKNOWN"); break;
    }
}

} // extern "C"