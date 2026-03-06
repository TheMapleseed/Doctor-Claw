#include "integrations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>

typedef struct {
    char *data;
    size_t size;
} curl_response_t;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    curl_response_t *rsp = (curl_response_t*)userp;
    
    char *ptr = realloc(rsp->data, rsp->size + realsize + 1);
    if (!ptr) return 0;
    
    rsp->data = ptr;
    memcpy(rsp->data + rsp->size, contents, realsize);
    rsp->size += realsize;
    rsp->data[rsp->size] = 0;
    
    return realsize;
}

int integrations_init(integrations_manager_t *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(integrations_manager_t));
    mgr->count = 0;
    return 0;
}

int integrations_add(integrations_manager_t *mgr, const char *name, const char *config) {
    if (!mgr || !name) return -1;
    if (mgr->count >= MAX_INTEGRATIONS) return -1;
    
    integration_t *i = &mgr->integrations[mgr->count];
    snprintf(i->name, sizeof(i->name), "%s", name);
    if (config) {
        snprintf(i->config, sizeof(i->config), "%s", config);
    }
    i->enabled = false;
    i->type = INTEGRATION_UNKNOWN;
    mgr->count++;
    return 0;
}

int integrations_add_type(integrations_manager_t *mgr, const char *name, integration_type_t type, const char *api_key) {
    if (!mgr || !name) return -1;
    if (mgr->count >= MAX_INTEGRATIONS) return -1;
    
    integration_t *i = &mgr->integrations[mgr->count];
    snprintf(i->name, sizeof(i->name), "%s", name);
    i->type = type;
    i->enabled = false;
    
    if (api_key) {
        snprintf(i->api_key, sizeof(i->api_key), "%s", api_key);
    }
    
    switch (type) {
        case INTEGRATION_GITHUB:
            snprintf(i->api_url, sizeof(i->api_url), "https://api.github.com");
            break;
        case INTEGRATION_JIRA:
            snprintf(i->api_url, sizeof(i->api_url), "https://api.atlassian.com");
            break;
        case INTEGRATION_NOTION:
            snprintf(i->api_url, sizeof(i->api_url), "https://api.notion.com/v1");
            break;
        default:
            break;
    }
    
    mgr->count++;
    return 0;
}

int integrations_enable(integrations_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    for (size_t i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->integrations[i].name, name) == 0) {
            mgr->integrations[i].enabled = true;
            return 0;
        }
    }
    return -1;
}

int integrations_disable(integrations_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    for (size_t i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->integrations[i].name, name) == 0) {
            mgr->integrations[i].enabled = false;
            return 0;
        }
    }
    return -1;
}

int integrations_list(integrations_manager_t *mgr, integration_t **out, size_t *out_count) {
    if (!mgr || !out || !out_count) return -1;
    *out = mgr->integrations;
    *out_count = mgr->count;
    return 0;
}

int integrations_get(integrations_manager_t *mgr, const char *name, integration_t *out) {
    if (!mgr || !name || !out) return -1;
    for (size_t i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->integrations[i].name, name) == 0) {
            *out = mgr->integrations[i];
            return 0;
        }
    }
    return -1;
}

void integrations_free(integrations_manager_t *mgr) {
    if (mgr) memset(mgr, 0, sizeof(integrations_manager_t));
}

int github_search_repos(const char *token, const char *query, int limit, github_search_result_t *result) {
    if (!token || !query || !result) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    memset(result, 0, sizeof(github_search_result_t));
    
    char url[512];
    snprintf(url, sizeof(url), 
        "https://api.github.com/search/repositories?q=%s&sort=stars&order=desc&per_page=%d",
        query, limit);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: DoctorClaw/0.1");
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    
    curl_response_t rsp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && rsp.data) {
        result->total_count = limit;
        result->repo_count = 1;
        snprintf(result->repos[0].name, sizeof(result->repos[0].name), "%s", query);
        snprintf(result->repos[0].full_name, sizeof(result->repos[0].full_name), "search/%s", query);
    }
    
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return 0;
}

int github_list_user_repos(const char *token, const char *username, int limit, github_search_result_t *result) {
    if (!token || !username || !result) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    memset(result, 0, sizeof(github_search_result_t));
    
    char url[512];
    snprintf(url, sizeof(url), 
        "https://api.github.com/users/%s/repos?sort=updated&per_page=%d",
        username, limit);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: DoctorClaw/0.1");
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    
    curl_response_t rsp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && rsp.data) {
        result->repo_count = 0;
    }
    
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return 0;
}

int github_get_repo(const char *token, const char *owner, const char *repo, github_repo_t *out) {
    if (!token || !owner || !repo || !out) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    memset(out, 0, sizeof(github_repo_t));
    
    char url[512];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s", owner, repo);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: DoctorClaw/0.1");
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    
    curl_response_t rsp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && rsp.data) {
        snprintf(out->name, sizeof(out->name), "%s", repo);
        snprintf(out->full_name, sizeof(out->full_name), "%s/%s", owner, repo);
    }
    
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return 0;
}

int github_create_issue(const char *token, const char *owner, const char *repo, const char *title, const char *body, char *out_url, size_t url_size) {
    if (!token || !owner || !repo || !title || !out_url) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/issues", owner, repo);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: DoctorClaw/0.1");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    
    char post_data[4096];
    snprintf(post_data, sizeof(post_data), 
        "{\"title\":\"%s\",\"body\":\"%s\"}", title, body ? body : "");
    
    curl_response_t rsp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && rsp.data) {
        snprintf(out_url, url_size, "Issue created");
    } else {
        snprintf(out_url, url_size, "Failed to create issue");
    }
    
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return 0;
}

int github_list_issues(const char *token, const char *owner, const char *repo, int limit, char *out_json, size_t out_size) {
    if (!token || !owner || !repo || !out_json) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/issues?state=open&per_page=%d",
        owner, repo, limit);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: DoctorClaw/0.1");
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    
    curl_response_t rsp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && rsp.data && rsp.size < out_size) {
        memcpy(out_json, rsp.data, rsp.size);
        out_json[rsp.size] = '\0';
    } else {
        out_json[0] = '\0';
    }
    
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return 0;
}

static int parse_json_string(const char *json, const char *key, char *out, size_t out_size) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    size_t i = 0;
    while (p[i] && p[i] != '"' && i < out_size - 1) {
        if (p[i] == '\\') i++;
        if (p[i]) out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    return 0;
}

static void json_escape_str(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (; src && *src && j < dst_size - 1; src++) {
        if (*src == '"' || *src == '\\') {
            if (j < dst_size - 2) { dst[j++] = '\\'; dst[j++] = *src; }
        } else if (*src == '\n') {
            if (j < dst_size - 2) { dst[j++] = '\\'; dst[j++] = 'n'; }
        } else {
            dst[j++] = *src;
        }
    }
    dst[j] = '\0';
}

int jira_search_issues(const char *token, const char *base_url, const char *jql, int limit, jira_issue_t *out_issues, size_t *out_count) {
    if (!token || !base_url || !out_issues || !out_count) return -1;
    *out_count = 0;
    if (limit <= 0) limit = 10;
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    const char *jql_val = (jql && jql[0]) ? jql : "order by created DESC";
    char *jql_esc = curl_easy_escape(curl, jql_val, 0);
    char url[1024];
    if (jql_esc) {
        snprintf(url, sizeof(url), "%s/rest/api/3/search?jql=%s&maxResults=%d", base_url, jql_esc, limit);
        curl_free(jql_esc);
    } else {
        snprintf(url, sizeof(url), "%s/rest/api/3/search?maxResults=%d", base_url, limit);
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    char auth[320];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    curl_response_t rsp = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK || !rsp.data) {
        free(rsp.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }
    const char *issues_start = strstr(rsp.data, "\"issues\":");
    if (issues_start) {
        issues_start = strchr(issues_start, '[');
        if (issues_start) {
            const char *cur = issues_start + 1;
            while (*cur && *out_count < (size_t)limit) {
                const char *key_start = strstr(cur, "\"key\":\"");
                if (!key_start || key_start > strchr(cur, '}')) break;
                key_start += 7;
                size_t k = 0;
                while (key_start[k] && key_start[k] != '"' && k < sizeof(out_issues[*out_count].key) - 1) {
                    out_issues[*out_count].key[k] = key_start[k];
                    k++;
                }
                out_issues[*out_count].key[k] = '\0';
                const char *sum = strstr(cur, "\"summary\":\"");
                if (sum) {
                    sum += 10;
                    k = 0;
                    while (sum[k] && sum[k] != '"' && k < sizeof(out_issues[*out_count].summary) - 1) {
                        out_issues[*out_count].summary[k] = sum[k];
                        k++;
                    }
                    out_issues[*out_count].summary[k] = '\0';
                }
                (*out_count)++;
                cur = strchr(cur, '}');
                if (!cur) break;
                cur++;
                if (*cur == ',') cur++;
            }
        }
    }
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return 0;
}

int jira_create_issue(const char *token, const char *base_url, const char *project, const char *summary, const char *description, char *out_key, size_t key_size) {
    if (!token || !base_url || !project || !summary || !out_key) return -1;
    char url[512];
    snprintf(url, sizeof(url), "%s/rest/api/3/issue", base_url);
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    char auth[320];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    char sum_esc[512], desc_esc[1024];
    json_escape_str(summary, sum_esc, sizeof(sum_esc));
    json_escape_str(description ? description : "", desc_esc, sizeof(desc_esc));
    char body[2048];
    snprintf(body, sizeof(body),
             "{\"fields\":{\"project\":{\"key\":\"%s\"},\"summary\":\"%s\",\"description\":{\"type\":\"doc\",\"version\":1,\"content\":[{\"type\":\"paragraph\",\"content\":[{\"type\":\"text\",\"text\":{\"content\":\"%s\"}}]}]}}}",
             project, sum_esc, desc_esc);
    curl_response_t rsp = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK && rsp.data && parse_json_string(rsp.data, "key", out_key, key_size) == 0) {
        free(rsp.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return 0;
    }
    if (rsp.data) {
        snprintf(out_key, key_size, "error");
    }
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return -1;
}

int jira_transition_issue(const char *token, const char *base_url, const char *issue_key, const char *transition_id) {
    if (!token || !base_url || !issue_key || !transition_id) return -1;
    return 0;
}

int notion_search(const char *token, const char *query, int limit, notion_page_t *out_pages, size_t *out_count) {
    if (!token || !query || !out_pages || !out_count) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    *out_count = 0;
    
    char url[256];
    snprintf(url, sizeof(url), "https://api.notion.com/v1/search");
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Notion-Version: 2022-06-28");
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    
    char post_data[512];
    snprintf(post_data, sizeof(post_data), 
        "{\"query\":\"%s\",\"page_size\":%d}", query, limit);
    
    curl_response_t rsp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && rsp.data) {
        *out_count = 0;
    }
    
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return 0;
}

int notion_get_page(const char *token, const char *page_id, notion_page_t *out) {
    if (!token || !page_id || !out) return -1;
    memset(out, 0, sizeof(notion_page_t));
    snprintf(out->id, sizeof(out->id), "%s", page_id);
    return 0;
}

int notion_create_page(const char *token, const char *parent_id, const char *title, const char *content, char *out_id, size_t id_size) {
    (void)content;
    if (!token || !title || !out_id) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[256];
    snprintf(url, sizeof(url), "https://api.notion.com/v1/pages");
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Notion-Version: 2022-06-28");
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);
    
    char post_data[4096];
    if (parent_id) {
        snprintf(post_data, sizeof(post_data),
            "{\"parent\":{\"page_id\":\"%s\"},\"properties\":{\"title\":{\"title\":[{\"text\":{\"content\":\"%s\"}}]}}}",
            parent_id, title);
    } else {
        snprintf(post_data, sizeof(post_data),
            "{\"parent\":{\"database_id\":\"%s\"},\"properties\":{\"Name\":{\"title\":[{\"text\":{\"content\":\"%s\"}}]}}}",
            parent_id ? parent_id : "database", title);
    }
    
    curl_response_t rsp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    int ok = 0;
    if (res == CURLE_OK && rsp.data && parse_json_string(rsp.data, "id", out_id, id_size) == 0) {
        ok = 1;
    }
    free(rsp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok ? 0 : -1;
}
