#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static void trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    char *q = p + strlen(p);
    while (q > p && isspace((unsigned char)q[-1])) q--;
    *q = '\0';
    if (p != s)
        memmove(s, p, (size_t)(q - p) + 1);
}

static int parse_ini_line(const char *line, char *section, size_t section_len,
                          char *key, size_t key_len, char *value, size_t value_len) {
    if (!line || !section || !key || !value) return -1;
    key[0] = value[0] = '\0';
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '#' || *line == ';' || *line == '\0') return 0;
    if (*line == '[') {
        const char *end = strchr(line, ']');
        if (end && section_len > 0) {
            size_t n = (size_t)(end - line - 1);
            if (n >= section_len) n = section_len - 1;
            memcpy(section, line + 1, n);
            section[n] = '\0';
            trim(section);
        }
        return 0;
    }
    const char *eq = strchr(line, '=');
    if (!eq) return 0;
    size_t klen = (size_t)(eq - line);
    if (klen >= key_len) klen = key_len - 1;
    memcpy(key, line, klen);
    key[klen] = '\0';
    trim(key);
    const char *val = eq + 1;
    while (*val && isspace((unsigned char)*val)) val++;
    if (*val == '"') {
        val++;
        const char *vend = val;
        while (*vend && *vend != '"') { if (*vend == '\\') vend++; vend++; }
        size_t vlen = (size_t)(vend - val);
        if (vlen >= value_len) vlen = value_len - 1;
        memcpy(value, val, vlen);
        value[vlen] = '\0';
    } else {
        size_t vlen = strlen(val);
        if (vlen >= value_len) vlen = value_len - 1;
        memcpy(value, val, vlen);
        value[vlen] = '\0';
        trim(value);
    }
    return 1;
}

void config_init_defaults(config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(config_t));

    const char *home = getenv("HOME");
    if (home) {
        snprintf(cfg->paths.workspace_dir, MAX_PATH_LEN, "%s/.doctorclaw", home);
        snprintf(cfg->paths.state_dir, MAX_PATH_LEN, "%s/.doctorclaw/state", home);
        snprintf(cfg->paths.data_dir, MAX_PATH_LEN, "%s/.doctorclaw/data", home);
    }

    snprintf(cfg->provider.provider, MAX_PROVIDER_NAME_LEN, "openrouter");
    snprintf(cfg->provider.model, MAX_MODEL_NAME_LEN, "");
    cfg->provider.temperature = 0.7;
    cfg->provider.max_tokens = 4096;

    snprintf(cfg->memory.backend, 32, "sqlite");
    cfg->memory.auto_save = true;
    snprintf(cfg->memory.storage_provider, 32, "sqlite");

    cfg->heartbeat.enabled = false;
    cfg->heartbeat.interval_minutes = 5;

    snprintf(cfg->observability.backend, 32, "log");

    cfg->autonomy.enabled = true;
    cfg->autonomy.level = AUTONOMY_LEVEL_SUPERVISED;
    snprintf(cfg->autonomy.level_str, 16, "supervised");
    cfg->autonomy.workspace_only = true;
    cfg->autonomy.max_actions_per_hour = 100;
    cfg->autonomy.max_cost_per_day_cents = 10000;

    cfg->gateway.port = 8080;
    snprintf(cfg->gateway.host, 256, "0.0.0.0");

    cfg->secrets.encrypt = true;
}

int config_load(const char *path, config_t *cfg) {
    if (!cfg) return -1;

    config_init_defaults(cfg);

    char resolved_path[MAX_PATH_LEN] = {0};
    if (path && path[0]) {
        snprintf(resolved_path, sizeof(resolved_path), "%s", path);
    } else {
        const char *env_config = getenv("DOCTORCLAW_CONFIG");
        if (env_config && env_config[0]) {
            snprintf(resolved_path, sizeof(resolved_path), "%s", env_config);
        } else {
            const char *home = getenv("HOME");
            if (home)
                snprintf(resolved_path, sizeof(resolved_path), "%s/.doctorclaw/config.toml", home);
            else
                snprintf(resolved_path, sizeof(resolved_path), ".doctorclaw/config.toml");
        }
    }
    snprintf(cfg->paths.config_path, MAX_PATH_LEN, "%s", resolved_path);

    FILE *f = fopen(resolved_path, "r");
    if (f) {
        char line[1024];
        char section[64] = {0};
        char key[128];
        char value[1024];
        while (fgets(line, sizeof(line), f)) {
            int r = parse_ini_line(line, section, sizeof(section), key, sizeof(key), value, sizeof(value));
            if (r < 0) continue;
            if (key[0] == '\0') continue;
            if (strcmp(section, "paths") == 0) {
                if (strcmp(key, "workspace_dir") == 0)
                    snprintf(cfg->paths.workspace_dir, MAX_PATH_LEN, "%s", value);
                else if (strcmp(key, "state_dir") == 0)
                    snprintf(cfg->paths.state_dir, MAX_PATH_LEN, "%s", value);
                else if (strcmp(key, "data_dir") == 0)
                    snprintf(cfg->paths.data_dir, MAX_PATH_LEN, "%s", value);
            } else if (strcmp(section, "provider") == 0) {
                if (strcmp(key, "provider") == 0)
                    snprintf(cfg->provider.provider, MAX_PROVIDER_NAME_LEN, "%s", value);
                else if (strcmp(key, "model") == 0)
                    snprintf(cfg->provider.model, MAX_MODEL_NAME_LEN, "%s", value);
                else if (strcmp(key, "temperature") == 0)
                    cfg->provider.temperature = atof(value);
                else if (strcmp(key, "max_tokens") == 0)
                    cfg->provider.max_tokens = (size_t)atoi(value);
            } else if (strcmp(section, "gateway") == 0) {
                if (strcmp(key, "host") == 0)
                    snprintf(cfg->gateway.host, sizeof(cfg->gateway.host), "%s", value);
                else if (strcmp(key, "port") == 0)
                    cfg->gateway.port = (unsigned short)atoi(value);
            }
        }
        fclose(f);
    }

    config_load_from_env(cfg);
    return 0;
}

int config_load_from_env(config_t *cfg) {
    if (!cfg) return -1;
    const char *v;

    v = getenv("DOCTORCLAW_WORKSPACE");
    if (v && v[0]) {
        snprintf(cfg->paths.workspace_dir, MAX_PATH_LEN, "%s", v);
        snprintf(cfg->paths.state_dir, MAX_PATH_LEN, "%s/state", v);
        snprintf(cfg->paths.data_dir, MAX_PATH_LEN, "%s/data", v);
    }
    v = getenv("DOCTORCLAW_CONFIG");
    if (v && v[0])
        snprintf(cfg->paths.config_path, MAX_PATH_LEN, "%s", v);

    v = getenv("OPENROUTER_API_KEY");
    if (v && v[0]) {
        snprintf(cfg->provider.api_key, MAX_API_KEY_LEN, "%s", v);
        if (!cfg->provider.provider[0])
            snprintf(cfg->provider.provider, MAX_PROVIDER_NAME_LEN, "openrouter");
    }
    if (!cfg->provider.api_key[0]) {
        v = getenv("OPENAI_API_KEY");
        if (v && v[0]) {
            snprintf(cfg->provider.api_key, MAX_API_KEY_LEN, "%s", v);
            if (!cfg->provider.provider[0])
                snprintf(cfg->provider.provider, MAX_PROVIDER_NAME_LEN, "openai");
        }
    }
    if (!cfg->provider.api_key[0]) {
        v = getenv("ANTHROPIC_API_KEY");
        if (v && v[0]) {
            snprintf(cfg->provider.api_key, MAX_API_KEY_LEN, "%s", v);
            if (!cfg->provider.provider[0])
                snprintf(cfg->provider.provider, MAX_PROVIDER_NAME_LEN, "anthropic");
        }
    }
    v = getenv("DOCTORCLAW_PROVIDER");
    if (v && v[0])
        snprintf(cfg->provider.provider, MAX_PROVIDER_NAME_LEN, "%s", v);
    v = getenv("DOCTORCLAW_MODEL");
    if (v && v[0])
        snprintf(cfg->provider.model, MAX_MODEL_NAME_LEN, "%s", v);
    v = getenv("DOCTORCLAW_GATEWAY_PORT");
    if (v && v[0])
        cfg->gateway.port = (unsigned short)atoi(v);
    v = getenv("DOCTORCLAW_GATEWAY_HOST");
    if (v && v[0])
        snprintf(cfg->gateway.host, sizeof(cfg->gateway.host), "%s", v);

    return 0;
}

int config_env_summary(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return -1;
    buf[0] = '\0';
    const char *env_vars[] = {
        "DOCTORCLAW_WORKSPACE", "DOCTORCLAW_CONFIG", "DOCTORCLAW_PROVIDER", "DOCTORCLAW_MODEL",
        "DOCTORCLAW_GATEWAY_PORT", "DOCTORCLAW_GATEWAY_HOST",
        "OPENROUTER_API_KEY", "OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GITHUB_TOKEN",
        "HOME", "USER", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", NULL
    };
    size_t off = 0;
    for (int i = 0; env_vars[i] && off < buf_size - 2; i++) {
        const char *v = getenv(env_vars[i]);
        int n = snprintf(buf + off, buf_size - off, "%s%s%s", i ? " " : "",
                         env_vars[i], v && v[0] ? "(set)" : "");
        if (n > 0 && (size_t)n < buf_size - off)
            off += (size_t)n;
        else
            break;
    }
    return 0;
}

int config_save(const config_t *cfg, const char *path) {
    if (!cfg || !path) return -1;

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# Doctor Claw Configuration\n");
    fprintf(f, "# Version %s\n\n", "0.1.0");

    fprintf(f, "[paths]\n");
    fprintf(f, "workspace_dir = %s\n", cfg->paths.workspace_dir);
    fprintf(f, "state_dir = %s\n", cfg->paths.state_dir);
    fprintf(f, "data_dir = %s\n", cfg->paths.data_dir);
    fprintf(f, "\n");

    fprintf(f, "[provider]\n");
    fprintf(f, "provider = %s\n", cfg->provider.provider);
    fprintf(f, "model = %s\n", cfg->provider.model);
    fprintf(f, "temperature = %f\n", cfg->provider.temperature);
    fprintf(f, "\n");

    fprintf(f, "[gateway]\n");
    fprintf(f, "host = %s\n", cfg->gateway.host);
    fprintf(f, "port = %hu\n", cfg->gateway.port);
    fprintf(f, "\n");

    fclose(f);
    return 0;
}

const char* config_get_workspace_dir(const config_t *cfg) {
    if (!cfg) return ".";
    return cfg->paths.workspace_dir;
}
