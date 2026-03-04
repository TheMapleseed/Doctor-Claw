#ifndef DOCTORCLAW_INTEGRATIONS_H
#define DOCTORCLAW_INTEGRATIONS_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_INTEGRATION_NAME 64
#define MAX_INTEGRATIONS 32
#define MAX_REPOS 100

typedef enum {
    INTEGRATION_GITHUB,
    INTEGRATION_JIRA,
    INTEGRATION_NOTION,
    INTEGRATION_SLACK,
    INTEGRATION_GITLAB,
    INTEGRATION_AWS,
    INTEGRATION_HEROKU,
    INTEGRATION_DIGITALOCEAN,
    INTEGRATION_UNKNOWN
} integration_type_t;

typedef struct {
    char name[MAX_INTEGRATION_NAME];
    bool enabled;
    char config[512];
    char api_key[256];
    char api_url[256];
    integration_type_t type;
} integration_t;

typedef struct {
    integration_t integrations[MAX_INTEGRATIONS];
    size_t count;
    char github_token[256];
    char jira_token[256];
    char slack_token[256];
} integrations_manager_t;

typedef struct {
    char name[256];
    char full_name[512];
    char description[2048];
    char html_url[512];
    char clone_url[512];
    char language[64];
    int stars;
    int forks;
    bool is_private;
    uint64_t updated_at;
} github_repo_t;

typedef struct {
    github_repo_t repos[MAX_REPOS];
    size_t repo_count;
    int total_count;
} github_search_result_t;

typedef struct {
    char key[64];
    char summary[256];
    char description[4096];
    char status[32];
    char priority[32];
    char assignee[128];
    uint64_t created_at;
    uint64_t updated_at;
} jira_issue_t;

typedef struct {
    char id[64];
    char title[256];
    char content[8192];
    char url[512];
    uint64_t created_at;
    uint64_t updated_at;
} notion_page_t;

int integrations_init(integrations_manager_t *mgr);
int integrations_add(integrations_manager_t *mgr, const char *name, const char *config);
int integrations_add_type(integrations_manager_t *mgr, const char *name, integration_type_t type, const char *api_key);
int integrations_enable(integrations_manager_t *mgr, const char *name);
int integrations_disable(integrations_manager_t *mgr, const char *name);
int integrations_list(integrations_manager_t *mgr, integration_t **out, size_t *out_count);
int integrations_get(integrations_manager_t *mgr, const char *name, integration_t *out);
void integrations_free(integrations_manager_t *mgr);

int github_search_repos(const char *token, const char *query, int limit, github_search_result_t *result);
int github_list_user_repos(const char *token, const char *username, int limit, github_search_result_t *result);
int github_get_repo(const char *token, const char *owner, const char *repo, github_repo_t *out);
int github_create_issue(const char *token, const char *owner, const char *repo, const char *title, const char *body, char *out_url, size_t url_size);
int github_list_issues(const char *token, const char *owner, const char *repo, int limit, char *out_json, size_t out_size);

int jira_search_issues(const char *token, const char *base_url, const char *jql, int limit, jira_issue_t *out_issues, size_t *out_count);
int jira_create_issue(const char *token, const char *base_url, const char *project, const char *summary, const char *description, char *out_key, size_t key_size);
int jira_transition_issue(const char *token, const char *base_url, const char *issue_key, const char *transition_id);

int notion_search(const char *token, const char *query, int limit, notion_page_t *out_pages, size_t *out_count);
int notion_get_page(const char *token, const char *page_id, notion_page_t *out);
int notion_create_page(const char *token, const char *parent_id, const char *title, const char *content, char *out_id, size_t id_size);

#endif
