#ifndef DOCTORCLAW_LOG_H
#define DOCTORCLAW_LOG_H

#include "c23_check.h"
#include <stddef.h>

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

/** Set minimum level (default INFO). Messages below this are dropped. */
void log_set_level(log_level_t level);
/** Optional: write to file as well as stdout. Pass NULL to close. */
int log_set_file(const char *path);
/** Returns the currently configured log file path (or NULL). */
const char *log_get_file_path(void);
/** Export/copy current log file to dest_path. */
int log_export(const char *dest_path);
/** Initialize logging (call early). */
void log_init(void);

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif
