#ifndef DOCTORCLAW_SKILLFORGE_H
#define DOCTORCLAW_SKILLFORGE_H

#include "c23_check.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SKILLFORGE_MAX_NAME_LEN 128
#define SKILLFORGE_MAX_URL_LEN 512
#define SKILLFORGE_MAX_DESC_LEN 1024
#define SKILLFORGE_MAX_OUTPUT_DIR 256

typedef enum {
    SCOUT_SOURCE_GITHUB,
    SCOUT_SOURCE_CLAWHUB,
    SCOUT_SOURCE_HUGGINGFACE
} scout_source_t;

typedef struct {
    char name[SKILLFORGE_MAX_NAME_LEN];
    char url[SKILLFORGE_MAX_URL_LEN];
    char description[SKILLFORGE_MAX_DESC_LEN];
    uint64_t stars;
    char *language;
    char updated_at[32];
    scout_source_t source;
    char owner[128];
    bool has_license;
} scout_result_t;

typedef struct {
    double compatibility;
    double quality;
    double security;
} skillforge_scores_t;

typedef enum {
    RECOMMENDATION_AUTO,
    RECOMMENDATION_MANUAL,
    RECOMMENDATION_SKIP
} recommendation_t;

typedef struct {
    scout_result_t candidate;
    skillforge_scores_t scores;
    double total_score;
    recommendation_t recommendation;
} eval_result_t;

typedef struct {
    bool enabled;
    bool auto_integrate;
    char **sources;
    size_t source_count;
    unsigned int scan_interval_hours;
    double min_score;
    char *github_token;
    char output_dir[SKILLFORGE_MAX_OUTPUT_DIR];
} skillforge_config_t;

typedef struct {
    size_t discovered;
    size_t evaluated;
    size_t auto_integrated;
    size_t manual_review;
    size_t skipped;
    eval_result_t *results;
    size_t result_count;
} forge_report_t;

typedef struct {
    skillforge_config_t config;
    void *evaluator;
    void *integrator;
} skillforge_t;

void skillforge_config_init(skillforge_config_t *cfg);
void skillforge_config_free(skillforge_config_t *cfg);
void skillforge_init(skillforge_t *sf, const skillforge_config_t *config);
void skillforge_free(skillforge_t *sf);
int skillforge_run(skillforge_t *sf, forge_report_t *report);

double skillforge_scores_total(const skillforge_scores_t *scores);
int skillforge_evaluate(skillforge_t *sf, const scout_result_t *candidate, eval_result_t *result);

int skillforge_scout_github(skillforge_t *sf, scout_result_t **results, size_t *count);
void skillforge_scout_dedup(scout_result_t *results, size_t *count);

#endif
