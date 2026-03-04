#include "approval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int approval_manager_init(approval_manager_t *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(approval_manager_t));
    mgr->auto_approve_enabled = false;
    return 0;
}

int approval_request(approval_manager_t *mgr, const char *action, const char *reason, approval_request_t *out_request) {
    if (!mgr || !action || !out_request) return -1;
    if (mgr->request_count >= 32) return -1;
    
    approval_request_t *req = &mgr->requests[mgr->request_count];
    snprintf(req->request_id, sizeof(req->request_id), "%08x%08x", rand(), rand());
    snprintf(req->action, sizeof(req->action), "%s", action);
    if (reason) {
        snprintf(req->reason, sizeof(req->reason), "%s", reason);
    }
    req->status = APPROVAL_PENDING;
    req->created_at = (uint64_t)time(NULL);
    req->expires_at = req->created_at + 300;
    req->requires_confirmation = true;
    
    *out_request = *req;
    mgr->request_count++;
    return 0;
}

int approval_respond(approval_manager_t *mgr, const char *request_id, bool approved) {
    if (!mgr || !request_id) return -1;
    
    for (size_t i = 0; i < mgr->request_count; i++) {
        if (strcmp(mgr->requests[i].request_id, request_id) == 0) {
            mgr->requests[i].status = approved ? APPROVAL_APPROVED : APPROVAL_DENIED;
            return 0;
        }
    }
    return -1;
}

int approval_check(approval_manager_t *mgr, const char *action) {
    if (!mgr || !action) return -1;
    if (mgr->auto_approve_enabled) return 1;
    
    if (strstr(mgr->approved_actions, action) != NULL) {
        return 1;
    }
    return 0;
}

int approval_list_pending(approval_manager_t *mgr, approval_request_t **out_requests, size_t *out_count) {
    if (!mgr || !out_requests || !out_count) return -1;
    *out_requests = mgr->requests;
    *out_count = mgr->request_count;
    return 0;
}

int approval_clear_expired(approval_manager_t *mgr) {
    if (!mgr) return -1;
    uint64_t now = (uint64_t)time(NULL);
    size_t count = 0;
    for (size_t i = 0; i < mgr->request_count; i++) {
        if (mgr->requests[i].expires_at > now) {
            mgr->requests[count++] = mgr->requests[i];
        }
    }
    mgr->request_count = count;
    return 0;
}

void approval_manager_free(approval_manager_t *mgr) {
    if (mgr) {
        memset(mgr, 0, sizeof(approval_manager_t));
    }
}
