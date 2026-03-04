#include "skills.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static const char *type_names[] = {
    "tool",
    "action",
    "pipeline",
    NULL
};

int skills_manager_init(skills_manager_t *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(skills_manager_t));
    return 0;
}

int skills_add(skills_manager_t *mgr, const char *name, const char *description, skill_type_t type, const char *path) {
    if (!mgr || !name || !path) return -1;
    if (mgr->skill_count >= 64) return -1;
    
    skill_t *skill = &mgr->skills[mgr->skill_count];
    snprintf(skill->name, sizeof(skill->name), "%s", name);
    if (description) {
        snprintf(skill->description, sizeof(skill->description), "%s", description);
    }
    snprintf(skill->path, sizeof(skill->path), "%s", path);
    skill->type = type;
    skill->enabled = true;
    skill->entry_point[0] = '\0';
    skill->args[0] = '\0';
    
    mgr->skill_count++;
    return 0;
}

int skills_remove(skills_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->skill_count; i++) {
        if (strcmp(mgr->skills[i].name, name) == 0) {
            for (size_t j = i; j < mgr->skill_count - 1; j++) {
                mgr->skills[j] = mgr->skills[j + 1];
            }
            mgr->skill_count--;
            return 0;
        }
    }
    return -1;
}

int skills_enable(skills_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->skill_count; i++) {
        if (strcmp(mgr->skills[i].name, name) == 0) {
            mgr->skills[i].enabled = true;
            return 0;
        }
    }
    return -1;
}

int skills_disable(skills_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->skill_count; i++) {
        if (strcmp(mgr->skills[i].name, name) == 0) {
            mgr->skills[i].enabled = false;
            return 0;
        }
    }
    return -1;
}

int skills_execute(skills_manager_t *mgr, const char *name, const char *args, char *output, size_t output_size) {
    if (!mgr || !name || !output) return -1;
    
    skill_t *skill = NULL;
    for (size_t i = 0; i < mgr->skill_count; i++) {
        if (strcmp(mgr->skills[i].name, name) == 0) {
            skill = &mgr->skills[i];
            break;
        }
    }
    
    if (!skill) return -1;
    if (!skill->enabled) return -1;
    
    pid_t pid = fork();
    if (pid < 0) return -1;
    
    if (pid == 0) {
        if (args) {
            execl(skill->path, skill->path, args, NULL);
        } else {
            execl(skill->path, skill->path, NULL);
        }
        exit(1);
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        snprintf(output, output_size, "Skill '%s' executed successfully", name);
    } else {
        snprintf(output, output_size, "Skill '%s' failed with exit code %d", name, WEXITSTATUS(status));
    }
    
    return 0;
}

int skills_list(skills_manager_t *mgr, skill_t **out_skills, size_t *out_count) {
    if (!mgr || !out_skills || !out_count) return -1;
    *out_skills = mgr->skills;
    *out_count = mgr->skill_count;
    return 0;
}

int skills_get(skills_manager_t *mgr, const char *name, skill_t *out_skill) {
    if (!mgr || !name || !out_skill) return -1;
    
    for (size_t i = 0; i < mgr->skill_count; i++) {
        if (strcmp(mgr->skills[i].name, name) == 0) {
            *out_skill = mgr->skills[i];
            return 0;
        }
    }
    return -1;
}

int skills_save(skills_manager_t *mgr, const char *path) {
    if (!mgr || !path) return -1;
    
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "# DoctorClaw Skills\n\n");
    
    for (size_t i = 0; i < mgr->skill_count; i++) {
        skill_t *s = &mgr->skills[i];
        fprintf(f, "[skill]\n");
        fprintf(f, "name = %s\n", s->name);
        fprintf(f, "description = %s\n", s->description);
        fprintf(f, "type = %s\n", skill_type_name(s->type));
        fprintf(f, "path = %s\n", s->path);
        fprintf(f, "enabled = %s\n", s->enabled ? "true" : "false");
        fprintf(f, "\n");
    }
    
    fclose(f);
    return 0;
}

int skills_load(skills_manager_t *mgr, const char *path) {
    if (!mgr || !path) return -1;
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[1024];
    char current_name[MAX_SKILL_NAME] = {0};
    char current_desc[MAX_SKILL_DESC] = {0};
    char current_path[MAX_SKILL_PATH] = {0};
    skill_type_t current_type = SKILL_TYPE_TOOL;
    bool current_enabled = true;
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '[') continue;
        
        char key[128], val[512];
        if (sscanf(line, "%127[^=] = %511[^\n]", key, val) == 2) {
            char *k = key;
            while (*k == ' ') k++;
            
            if (strcmp(k, "name") == 0) {
                snprintf(current_name, sizeof(current_name), "%s", val);
            } else if (strcmp(k, "description") == 0) {
                snprintf(current_desc, sizeof(current_desc), "%s", val);
            } else if (strcmp(k, "type") == 0) {
                current_type = skill_type_from_name(val);
            } else if (strcmp(k, "path") == 0) {
                snprintf(current_path, sizeof(current_path), "%s", val);
            } else if (strcmp(k, "enabled") == 0) {
                current_enabled = (strcmp(val, "true") == 0);
            }
        }
    }
    
    if (current_name[0] && current_path[0]) {
        skills_add(mgr, current_name, current_desc, current_type, current_path);
        if (!current_enabled) {
            skills_disable(mgr, current_name);
        }
    }
    
    fclose(f);
    return 0;
}

void skills_manager_free(skills_manager_t *mgr) {
    if (mgr) {
        memset(mgr, 0, sizeof(skills_manager_t));
    }
}

const char *skill_type_name(skill_type_t type) {
    if (type >= 0 && type < SKILL_TYPE_NONE) {
        return type_names[type];
    }
    return "unknown";
}

skill_type_t skill_type_from_name(const char *name) {
    if (!name) return SKILL_TYPE_NONE;
    
    for (int i = 0; type_names[i]; i++) {
        if (strcmp(name, type_names[i]) == 0) {
            return (skill_type_t)i;
        }
    }
    return SKILL_TYPE_NONE;
}
