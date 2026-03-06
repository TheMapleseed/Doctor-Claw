#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static log_level_t g_log_level = LOG_LEVEL_INFO;
static FILE *g_log_file = NULL;
static char g_log_file_path[4096] = {0};

void log_set_level(log_level_t level) {
    g_log_level = level;
}

int log_set_file(const char *path) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_file_path[0] = '\0';
    if (path && path[0]) {
        g_log_file = fopen(path, "a");
        if (g_log_file) {
            snprintf(g_log_file_path, sizeof(g_log_file_path), "%s", path);
        }
        return g_log_file ? 0 : -1;
    }
    return 0;
}

const char *log_get_file_path(void) {
    return g_log_file_path[0] ? g_log_file_path : NULL;
}

int log_export(const char *dest_path) {
    if (!dest_path || !dest_path[0]) return -1;
    if (!g_log_file_path[0]) return -1;

    FILE *src = fopen(g_log_file_path, "rb");
    if (!src) return -1;
    FILE *dst = fopen(dest_path, "wb");
    if (!dst) {
        fclose(src);
        return -1;
    }

    char buf[8192];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            return -1;
        }
    }

    fclose(src);
    fclose(dst);
    return 0;
}

void log_init(void) {
    (void)g_log_level;
}

static void log_write(log_level_t level, const char *tag, const char *fmt, va_list ap) {
    if (level < g_log_level) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char tbuf[32];
    if (tm)
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm);
    else
        tbuf[0] = '\0';
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    fprintf(stdout, "[%s] %s %s\n", tbuf, tag, buf);
    fflush(stdout);
    if (g_log_file) {
        fprintf(g_log_file, "[%s] %s %s\n", tbuf, tag, buf);
        fflush(g_log_file);
    }
}

void log_debug(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write(LOG_LEVEL_DEBUG, "DEBUG", fmt, ap);
    va_end(ap);
}

void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write(LOG_LEVEL_INFO, "INFO", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write(LOG_LEVEL_WARN, "WARN", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write(LOG_LEVEL_ERROR, "ERROR", fmt, ap);
    va_end(ap);
}
