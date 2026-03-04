#ifndef DOCTORCLAW_CRON_H
#define DOCTORCLAW_CRON_H

#include "c23_check.h"

int cron_init(void);
int cron_run_pending(void);
int cron_add_task(const char *id, const char *expression, const char *command);
int cron_remove_task(const char *id);
void cron_shutdown(void);

#endif
