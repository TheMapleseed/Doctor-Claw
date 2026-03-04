#include "doctor.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>

static const char *result_strings[] = {
    "OK",
    "WARNING",
    "ERROR",
    "SKIP"
};

int doctor_init(doctor_report_t *report) {
    if (!report) return -1;
    memset(report, 0, sizeof(doctor_report_t));
    return 0;
}

static void doctor_add_check(doctor_report_t *report, const char *name, const char *description, doctor_check_result_t result, const char *message) {
    if (report->check_count >= MAX_DOCTOR_CHECKS) return;
    
    doctor_check_t *check = &report->checks[report->check_count];
    snprintf(check->name, sizeof(check->name), "%s", name);
    snprintf(check->description, sizeof(check->description), "%s", description);
    check->result = result;
    snprintf(check->message, sizeof(check->message), "%s", message);
    
    report->check_count++;
    
    switch (result) {
        case DOCTOR_CHECK_OK:
            report->passed++;
            break;
        case DOCTOR_CHECK_WARNING:
            report->warnings++;
            break;
        case DOCTOR_CHECK_ERROR:
            report->errors++;
            break;
        default:
            break;
    }
}

int doctor_check_config(doctor_report_t *report) {
    char home[1024];
    const char *home_env = getenv("HOME");
    if (home_env) {
        snprintf(home, sizeof(home), "%s", home_env);
    } else {
        const char *user = getenv("USER");
        if (user && *user)
            snprintf(home, sizeof(home), "/Users/%s", user);
        else
            snprintf(home, sizeof(home), ".");
    }
    
    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/.doctorclaw/config.toml", home);
    
    struct stat st;
    if (stat(config_path, &st) == 0) {
        doctor_add_check(report, "config", "Configuration file exists", DOCTOR_CHECK_OK, "Found at ~/.doctorclaw/config.toml");
    } else {
        doctor_add_check(report, "config", "Configuration file exists", DOCTOR_CHECK_WARNING, "Not found - run 'doctorclaw onboard' first");
    }
    
    return 0;
}

int doctor_check_auth(doctor_report_t *report) {
    char home[1024];
    const char *home_env = getenv("HOME");
    if (home_env) {
        snprintf(home, sizeof(home), "%s", home_env);
    } else {
        const char *user = getenv("USER");
        if (user && *user)
            snprintf(home, sizeof(home), "/Users/%s", user);
        else
            snprintf(home, sizeof(home), ".");
    }
    
    char auth_path[1024];
    snprintf(auth_path, sizeof(auth_path), "%s/.doctorclaw/auth.toml", home);
    
    struct stat st;
    if (stat(auth_path, &st) == 0) {
        doctor_add_check(report, "auth", "Authentication profiles configured", DOCTOR_CHECK_OK, "Auth file found");
        
        if (getenv("OPENAI_API_KEY") || getenv("ANTHROPIC_API_KEY") || getenv("OPENROUTER_API_KEY")) {
            doctor_add_check(report, "api_keys", "API keys present in environment", DOCTOR_CHECK_OK, "At least one API key found");
        } else {
            doctor_add_check(report, "api_keys", "API keys present in environment", DOCTOR_CHECK_WARNING, "No API keys found in environment");
        }
        {
            char env_summary[1024];
            if (config_env_summary(env_summary, sizeof(env_summary)) == 0 && env_summary[0]) {
                doctor_add_check(report, "env", "Environment variables detected", DOCTOR_CHECK_OK, env_summary);
            }
        }
    } else {
        doctor_add_check(report, "auth", "Authentication profiles configured", DOCTOR_CHECK_WARNING, "No auth file - run 'doctorclaw onboard' first");
    }
    
    return 0;
}

int doctor_check_providers(doctor_report_t *report) {
    void *handle = dlopen("libcurl.dylib", RTLD_LAZY);
    if (handle) {
        doctor_add_check(report, "libcurl", "libcurl available", DOCTOR_CHECK_OK, "HTTP client library found");
        dlclose(handle);
    } else {
        doctor_add_check(report, "libcurl", "libcurl available", DOCTOR_CHECK_ERROR, "libcurl not found");
    }
    
    handle = dlopen("libsqlite3.dylib", RTLD_LAZY);
    if (handle) {
        doctor_add_check(report, "libsqlite3", "SQLite3 available", DOCTOR_CHECK_OK, "Database library found");
        dlclose(handle);
    } else {
        doctor_add_check(report, "libsqlite3", "SQLite3 available", DOCTOR_CHECK_ERROR, "libsqlite3 not found");
    }
    
    return 0;
}

int doctor_check_memory(doctor_report_t *report) {
    char home[1024];
    const char *home_env = getenv("HOME");
    if (home_env) {
        snprintf(home, sizeof(home), "%s", home_env);
    } else {
        const char *user = getenv("USER");
        if (user && *user)
            snprintf(home, sizeof(home), "/Users/%s", user);
        else
            snprintf(home, sizeof(home), ".");
    }
    
    char db_path[1024];
    snprintf(db_path, sizeof(db_path), "%s/.doctorclaw/memory.db", home);
    
    struct stat st;
    if (stat(db_path, &st) == 0) {
        doctor_add_check(report, "memory", "Memory database exists", DOCTOR_CHECK_OK, "SQLite database found");
    } else {
        doctor_add_check(report, "memory", "Memory database exists", DOCTOR_CHECK_WARNING, "No database yet - memory will be created on first use");
    }
    
    return 0;
}

int doctor_check_channels(doctor_report_t *report) {
    char home[1024];
    const char *home_env = getenv("HOME");
    if (home_env) {
        snprintf(home, sizeof(home), "%s", home_env);
    } else {
        const char *user = getenv("USER");
        if (user && *user)
            snprintf(home, sizeof(home), "/Users/%s", user);
        else
            snprintf(home, sizeof(home), ".");
    }
    
    char channels_path[1024];
    snprintf(channels_path, sizeof(channels_path), "%s/.doctorclaw/channels/config.toml", home);
    
    struct stat st;
    if (stat(channels_path, &st) == 0) {
        doctor_add_check(report, "channels", "Channel configuration exists", DOCTOR_CHECK_OK, "Channels config found");
    } else {
        doctor_add_check(report, "channels", "Channel configuration exists", DOCTOR_CHECK_WARNING, "No channels configured yet");
    }
    
    return 0;
}

int doctor_check_dependencies(doctor_report_t *report) {
    if (access("/usr/bin/clang", X_OK) == 0) {
        doctor_add_check(report, "clang", "Clang compiler available", DOCTOR_CHECK_OK, "Found at /usr/bin/clang");
    } else {
        doctor_add_check(report, "clang", "Clang compiler available", DOCTOR_CHECK_ERROR, "Clang not found");
    }
    
    return 0;
}

int doctor_run_checks(doctor_report_t *report) {
    if (!report) return -1;
    
    doctor_check_config(report);
    doctor_check_auth(report);
    doctor_check_providers(report);
    doctor_check_memory(report);
    doctor_check_channels(report);
    doctor_check_dependencies(report);
    
    return 0;
}

void doctor_print_report(doctor_report_t *report) {
    if (!report) return;
    
    printf("DoctorClaw Diagnostics Report\n");
    printf("=============================\n\n");
    
    for (size_t i = 0; i < report->check_count; i++) {
        doctor_check_t *c = &report->checks[i];
        const char *symbol = "";
        switch (c->result) {
            case DOCTOR_CHECK_OK:
                symbol = "[✓]";
                break;
            case DOCTOR_CHECK_WARNING:
                symbol = "[!]";
                break;
            case DOCTOR_CHECK_ERROR:
                symbol = "[✗]";
                break;
            default:
                symbol = "[-]";
                break;
        }
        printf("  %s %-12s %s\n", symbol, result_strings[c->result], c->name);
        if (c->message[0]) {
            printf("                %s\n", c->message);
        }
    }
    
    printf("\n-----------------------------\n");
    printf("Summary: %d passed, %d warnings, %d errors\n", 
           report->passed, report->warnings, report->errors);
    
    if (report->errors > 0) {
        printf("\nStatus: FAILED - Please fix the errors above\n");
    } else if (report->warnings > 0) {
        printf("\nStatus: WARNING - Some issues need attention\n");
    } else {
        printf("\nStatus: OK - Everything looks good!\n");
    }
}

void doctor_free(doctor_report_t *report) {
    if (report) {
        memset(report, 0, sizeof(doctor_report_t));
    }
}
