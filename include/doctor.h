#ifndef DOCTORCLAW_DOCTOR_H
#define DOCTORCLAW_DOCTOR_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_DOCTOR_CHECKS 32

typedef enum {
    DOCTOR_CHECK_OK,
    DOCTOR_CHECK_WARNING,
    DOCTOR_CHECK_ERROR,
    DOCTOR_CHECK_SKIP
} doctor_check_result_t;

typedef struct {
    char name[128];
    char description[256];
    doctor_check_result_t result;
    char message[512];
} doctor_check_t;

typedef struct {
    doctor_check_t checks[MAX_DOCTOR_CHECKS];
    size_t check_count;
    int passed;
    int warnings;
    int errors;
} doctor_report_t;

int doctor_init(doctor_report_t *report);
int doctor_run_checks(doctor_report_t *report);
int doctor_check_config(doctor_report_t *report);
int doctor_check_auth(doctor_report_t *report);
int doctor_check_providers(doctor_report_t *report);
int doctor_check_memory(doctor_report_t *report);
int doctor_check_channels(doctor_report_t *report);
int doctor_check_dependencies(doctor_report_t *report);
void doctor_print_report(doctor_report_t *report);
void doctor_free(doctor_report_t *report);

#endif
