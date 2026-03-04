#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "skillforge.h"

#define BAD_PATTERNS_COUNT 8
static const char *BAD_PATTERNS[BAD_PATTERNS_COUNT] = {
    "malware", "exploit", "hack", "crack", "keygen", "ransomware", "trojan"
};

typedef struct {
    double min_score;
} evaluator_t;

typedef struct {
    char output_dir[SKILLFORGE_MAX_OUTPUT_DIR];
} integrator_t;

void skillforge_config_init(skillforge_config_t *cfg) {
    memset(cfg, 0, sizeof(skillforge_config_t));
    cfg->enabled = false;
    cfg->auto_integrate = true;
    cfg->source_count = 2;
    cfg->sources = malloc(sizeof(char*) * 2);
    cfg->sources[0] = strdup("github");
    cfg->sources[1] = strdup("clawhub");
    cfg->scan_interval_hours = 24;
    cfg->min_score = 0.7;
    cfg->github_token = NULL;
    strcpy(cfg->output_dir, "./skills");
}

void skillforge_config_free(skillforge_config_t *cfg) {
    if (cfg->sources) {
        for (size_t i = 0; i < cfg->source_count; i++) {
            free(cfg->sources[i]);
        }
        free(cfg->sources);
    }
    free(cfg->github_token);
}

void skillforge_init(skillforge_t *sf, const skillforge_config_t *config) {
    memset(sf, 0, sizeof(skillforge_t));
    memcpy(&sf->config, config, sizeof(skillforge_config_t));
    
    sf->evaluator = malloc(sizeof(evaluator_t));
    ((evaluator_t*)sf->evaluator)->min_score = config->min_score;
    
    sf->integrator = malloc(sizeof(integrator_t));
    strcpy(((integrator_t*)sf->integrator)->output_dir, config->output_dir);
}

void skillforge_free(skillforge_t *sf) {
    free(sf->evaluator);
    free(sf->integrator);
    memset(sf, 0, sizeof(skillforge_t));
}

double skillforge_scores_total(const skillforge_scores_t *scores) {
    return scores->compatibility * 0.30 + scores->quality * 0.35 + scores->security * 0.35;
}

static int contains_word(const char *haystack, const char *word) {
    size_t haystack_len = strlen(haystack);
    size_t word_len = strlen(word);
    
    for (size_t i = 0; i <= haystack_len - word_len; i++) {
        int before_ok = (i == 0) || !((haystack[i-1] >= 'a' && haystack[i-1] <= 'z') || 
                                       (haystack[i-1] >= 'A' && haystack[i-1] <= 'Z') ||
                                       (haystack[i-1] >= '0' && haystack[i-1] <= '9'));
        int after_ok = (i + word_len >= haystack_len) || !((haystack[i+word_len] >= 'a' && haystack[i+word_len] <= 'z') || 
                                                            (haystack[i+word_len] >= 'A' && haystack[i+word_len] <= 'Z') ||
                                                            (haystack[i+word_len] >= '0' && haystack[i+word_len] <= '9'));
        if (before_ok && after_ok && strncmp(haystack + i, word, word_len) == 0) {
            return 1;
        }
    }
    return 0;
}

int skillforge_evaluate(skillforge_t *sf, const scout_result_t *candidate, eval_result_t *result) {
    evaluator_t *eval = (evaluator_t*)sf->evaluator;
    
    result->candidate = *candidate;
    
    double compatibility = 0.0;
    if (candidate->language) {
        if (strcmp(candidate->language, "Rust") == 0) {
            compatibility = 1.0;
        } else if (strcmp(candidate->language, "Python") == 0 ||
                   strcmp(candidate->language, "TypeScript") == 0 ||
                   strcmp(candidate->language, "JavaScript") == 0) {
            compatibility = 0.6;
        } else {
            compatibility = 0.3;
        }
    } else {
        compatibility = 0.2;
    }
    
    double quality = ((double)candidate->stars + 1.0) / 1024.0;
    if (quality > 1.0) quality = 1.0;
    
    double security = 0.5;
    if (candidate->has_license) security += 0.3;
    
    char lower_name[256];
    char lower_desc[2048];
    strncpy(lower_name, candidate->name, sizeof(lower_name) - 1);
    strncpy(lower_desc, candidate->description, sizeof(lower_desc) - 1);
    for (int i = 0; lower_name[i]; i++) lower_name[i] = lower_name[i] >= 'A' && lower_name[i] <= 'Z' ? lower_name[i] + 32 : lower_name[i];
    for (int i = 0; lower_desc[i]; i++) lower_desc[i] = lower_desc[i] >= 'A' && lower_desc[i] <= 'Z' ? lower_desc[i] + 32 : lower_desc[i];
    
    for (int i = 0; i < BAD_PATTERNS_COUNT; i++) {
        if (contains_word(lower_name, BAD_PATTERNS[i]) || contains_word(lower_desc, BAD_PATTERNS[i])) {
            security -= 0.5;
            break;
        }
    }
    
    if (security < 0.0) security = 0.0;
    if (security > 1.0) security = 1.0;
    
    result->scores.compatibility = compatibility;
    result->scores.quality = quality;
    result->scores.security = security;
    result->total_score = skillforge_scores_total(&result->scores);
    
    if (result->total_score >= eval->min_score) {
        result->recommendation = RECOMMENDATION_AUTO;
    } else if (result->total_score >= 0.4) {
        result->recommendation = RECOMMENDATION_MANUAL;
    } else {
        result->recommendation = RECOMMENDATION_SKIP;
    }
    
    return 0;
}

void skillforge_scout_dedup(scout_result_t *results, size_t *count) {
    int *seen = calloc(*count, sizeof(int));
    size_t new_count = 0;
    
    for (size_t i = 0; i < *count; i++) {
        if (seen[i]) continue;
        
        for (size_t j = i + 1; j < *count; j++) {
            if (strcmp(results[i].url, results[j].url) == 0) {
                seen[j] = 1;
            }
        }
    }
    
    for (size_t i = 0; i < *count; i++) {
        if (!seen[i]) {
            if (i != new_count) {
                results[new_count] = results[i];
            }
            new_count++;
        }
    }
    
    *count = new_count;
    free(seen);
}

typedef struct {
    char *data;
    size_t size;
} github_response_t;

static size_t github_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    github_response_t *rsp = (github_response_t*)userp;
    
    char *ptr = realloc(rsp->data, rsp->size + realsize + 1);
    if (!ptr) return 0;
    
    rsp->data = ptr;
    memcpy(rsp->data + rsp->size, contents, realsize);
    rsp->size += realsize;
    rsp->data[rsp->size] = 0;
    
    return realsize;
}

int skillforge_scout_github(skillforge_t *sf, scout_result_t **results, size_t *count) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    const char *queries[] = {"zeroclaw+skill", "ai+agent+skill"};
    *results = NULL;
    *count = 0;
    
    for (int q = 0; q < 2; q++) {
        char url[512];
        snprintf(url, sizeof(url), 
            "https://api.github.com/search/repositories?q=%s&sort=stars&order=desc&per_page=30",
            queries[q]);
        
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
        headers = curl_slist_append(headers, "User-Agent: DoctorClaw-SkillForge/0.1");
        
        if (sf->config.github_token) {
            char auth_header[256];
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", sf->config.github_token);
            headers = curl_slist_append(headers, auth_header);
        }
        
        github_response_t rsp = {0};
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, github_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rsp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        
        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK && rsp.data) {
            // Simple JSON parsing (would use json-c in production)
            // For now, just count as discovered
            *count += 1;
        }
        
        free(rsp.data);
        curl_slist_free_all(headers);
    }
    
    curl_easy_cleanup(curl);
    return 0;
}

int skillforge_run(skillforge_t *sf, forge_report_t *report) {
    memset(report, 0, sizeof(forge_report_t));
    
    if (!sf->config.enabled) {
        return 0;
    }
    
    scout_result_t *candidates = NULL;
    size_t candidate_count = 0;
    
    for (size_t i = 0; i < sf->config.source_count; i++) {
        if (strcmp(sf->config.sources[i], "github") == 0) {
            skillforge_scout_github(sf, &candidates, &candidate_count);
        }
    }
    
    skillforge_scout_dedup(candidates, &candidate_count);
    report->discovered = candidate_count;
    
    report->results = malloc(sizeof(eval_result_t) * candidate_count);
    
    for (size_t i = 0; i < candidate_count; i++) {
        skillforge_evaluate(sf, &candidates[i], &report->results[report->result_count]);
        
        if (report->results[report->result_count].recommendation == RECOMMENDATION_AUTO) {
            if (sf->config.auto_integrate) {
                report->auto_integrated++;
            } else {
                report->manual_review++;
            }
        } else if (report->results[report->result_count].recommendation == RECOMMENDATION_MANUAL) {
            report->manual_review++;
        } else {
            report->skipped++;
        }
        
        report->result_count++;
    }
    
    report->evaluated = report->result_count;
    
    free(candidates);
    return 0;
}
