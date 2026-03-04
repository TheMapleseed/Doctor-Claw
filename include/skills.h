#ifndef DOCTORCLAW_SKILLS_H
#define DOCTORCLAW_SKILLS_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_SKILL_NAME 64
#define MAX_SKILL_DESC 256
#define MAX_SKILL_PATH 512

typedef enum {
    SKILL_TYPE_TOOL,
    SKILL_TYPE_ACTION,
    SKILL_TYPE_PIPELINE,
    SKILL_TYPE_NONE
} skill_type_t;

typedef struct {
    char name[MAX_SKILL_NAME];
    char description[MAX_SKILL_DESC];
    char path[MAX_SKILL_PATH];
    skill_type_t type;
    bool enabled;
    char entry_point[128];
    char args[512];
} skill_t;

typedef struct {
    skill_t skills[64];
    size_t skill_count;
} skills_manager_t;

int skills_manager_init(skills_manager_t *mgr);
int skills_add(skills_manager_t *mgr, const char *name, const char *description, skill_type_t type, const char *path);
int skills_remove(skills_manager_t *mgr, const char *name);
int skills_enable(skills_manager_t *mgr, const char *name);
int skills_disable(skills_manager_t *mgr, const char *name);
int skills_execute(skills_manager_t *mgr, const char *name, const char *args, char *output, size_t output_size);
int skills_list(skills_manager_t *mgr, skill_t **out_skills, size_t *out_count);
int skills_get(skills_manager_t *mgr, const char *name, skill_t *out_skill);
int skills_save(skills_manager_t *mgr, const char *path);
int skills_load(skills_manager_t *mgr, const char *path);
void skills_manager_free(skills_manager_t *mgr);

const char *skill_type_name(skill_type_t type);
skill_type_t skill_type_from_name(const char *name);

#endif
