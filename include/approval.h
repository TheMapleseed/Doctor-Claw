#ifndef DOCTORCLAW_APPROVAL_H
#define DOCTORCLAW_APPROVAL_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_APPROVAL_REQUEST 64
#define MAX_APPROVAL_REASON 512

typedef enum {
    APPROVAL_PENDING,
    APPROVAL_APPROVED,
    APPROVAL_DENIED,
    APPROVAL_TIMEOUT
} approval_status_t;

typedef struct {
    char request_id[64];
    char action[128];
    char reason[MAX_APPROVAL_REASON];
    approval_status_t status;
    uint64_t created_at;
    uint64_t expires_at;
    bool requires_confirmation;
} approval_request_t;

typedef struct {
    approval_request_t requests[32];
    size_t request_count;
    bool auto_approve_enabled;
    char approved_actions[1024];
} approval_manager_t;

int approval_manager_init(approval_manager_t *mgr);
int approval_request(approval_manager_t *mgr, const char *action, const char *reason, approval_request_t *out_request);
int approval_respond(approval_manager_t *mgr, const char *request_id, bool approved);
int approval_check(approval_manager_t *mgr, const char *action);
int approval_list_pending(approval_manager_t *mgr, approval_request_t **out_requests, size_t *out_count);
int approval_clear_expired(approval_manager_t *mgr);
void approval_manager_free(approval_manager_t *mgr);

#endif
