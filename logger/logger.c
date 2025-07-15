#include "logger.h"
#include "zf_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

static FILE* _log_file = NULL;
static bool _logger_started = false;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _unguarded_close_log_file(void) {
    if (_log_file) {
        fclose(_log_file);
    }
    _log_file = NULL;
}

void close_log_file(void) {
    pthread_mutex_lock(&log_mutex);
    _unguarded_close_log_file();
    pthread_mutex_unlock(&log_mutex);
}

logger_retval_enum reset_logfile(const char *log_filepath) {
    if (!log_filepath) return LOGGER_FILEPATH_EMPTY;
    if (!_logger_started) return LOGGER_NOT_STARTED;

    pthread_mutex_lock(&log_mutex);
    _unguarded_close_log_file();

    _log_file = fopen(log_filepath, "a");
    if (!_log_file) {
        pthread_mutex_unlock(&log_mutex);
        return LOGGER_COULD_NOT_OPEN_FILE;
    }

    pthread_mutex_unlock(&log_mutex);
    return LOGGER_SUCCESS;
}

logger_retval_enum verify_logfile() {
    pthread_mutex_lock(&log_mutex);

    if (!_log_file) {
        pthread_mutex_unlock(&log_mutex);
        return LOGGER_FILE_PTR_IS_NULL;
    }

    if (fflush(_log_file) != 0) {
        _unguarded_close_log_file();
        pthread_mutex_unlock(&log_mutex);
        return LOGGER_FILE_FAILED_FLUSH;
    }

    int fd = fileno(_log_file);
    if (fd < 0 || fcntl(fd, F_GETFL) == -1) {
        _unguarded_close_log_file();
        pthread_mutex_unlock(&log_mutex);
        return LOGGER_FILE_INVALID_FD;
    }

    if (fsync(fd) != 0) {
        _unguarded_close_log_file();
        pthread_mutex_unlock(&log_mutex);
        return LOGGER_FILE_NOT_SYNCED;
    }

    pthread_mutex_unlock(&log_mutex);
    return LOGGER_SUCCESS;
}

static void zf_output_callback(const zf_log_message *msg, void *arg) {
    (void)arg;

    time_t t = time(NULL);
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

    pthread_mutex_lock(&log_mutex);
    if (_log_file) {
        fprintf(_log_file, "[%s] [%s] %.*s\n",
            time_str, lvl_char,
            (int)(msg->p - msg->msg_b), msg->msg_b);
        fflush(_log_file);
    }
    pthread_mutex_unlock(&log_mutex);
}

logger_retval_enum start_logging(const char *log_filepath) {
    pthread_mutex_lock(&log_mutex);
    if (_logger_started) {
        pthread_mutex_unlock(&log_mutex);
        return LOGGER_ALREADY_STARTED;
    }
    zf_log_set_output_v(ZF_LOG_PUT_STD, NULL, zf_output_callback);
    _logger_started = true;
    atexit(_unguarded_close_log_file);

    pthread_mutex_unlock(&log_mutex);

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
