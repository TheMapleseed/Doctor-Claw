#ifndef DOCTORCLAW_AGENT_H
#define DOCTORCLAW_AGENT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "providers.h"
#include "tools.h"
#include "approval.h"

#define MAX_TOOL_COUNT 64
#define MAX_MEMORY_ITEMS 1000
#define MAX_INTENT_LEN 64
#define MAX_CLASSIFICATION_CONFIDENCE 1.0

typedef enum {
    AGENT_STATE_IDLE,
    AGENT_STATE_THINKING,
    AGENT_STATE_EXECUTING,
    AGENT_STATE_WAITING_APPROVAL,
    AGENT_STATE_ERROR
} agent_state_t;

typedef enum {
    INTENT_GENERAL,
    INTENT_CODE,
    INTENT_SEARCH,
    INTENT_FILE_OP,
    INTENT_SHELL_CMD,
    INTENT_MEMORY,
    INTENT_ANALYSIS,
    INTENT_UNKNOWN
} intent_type_t;

typedef struct {
    intent_type_t type;
    char description[256];
    double confidence;
} intent_classification_t;

typedef struct {
    char name[64];
    char description[256];
    char input_schema[4096];
} tool_definition_t;

typedef struct {
    tool_definition_t tools[MAX_TOOL_COUNT];
    size_t tool_count;
    agent_state_t state;
    chat_context_t context;
    char session_id[64];
    char name[64];
    char description[256];
    char last_error[512];
    uint64_t started_at;
    size_t total_tokens;
    int tool_calls_count;
    approval_manager_t approval_manager;
    bool require_approval;
    /** Current task when in task-focus mode (attention loop). */
    char current_task[1024];
    bool task_focus_enabled;
    int max_attention_rounds;
} agent_t;

typedef struct {
    char prompt[8192];
    char system_prompt[4096];
    bool enable_thinking;
    int max_iterations;
    int timeout_seconds;
    /** When true, agent_run_task keeps looping until [TASK_COMPLETE] or max_attention_rounds. */
    bool task_focus_mode;
    /** Max "attention" rounds (continue-until-done loops). Default 5. */
    int max_attention_rounds;
} agent_runtime_config_t;

int agent_init(agent_t *agent, const config_t *config);
int agent_start(agent_t *agent, const char *message);
int agent_run_loop(agent_t *agent);
int agent_add_tool(agent_t *agent, const tool_definition_t *tool);
int agent_execute_tool(agent_t *agent, const tool_call_t *call, tool_result_t *result);
void agent_free(agent_t *agent);

int agent_classify_intent(const char *input, intent_classification_t *result);
int agent_dispatch(agent_t *agent, const char *input, char *response, size_t response_size);
/** Full chat with LLM and tools; use for API endpoints. */
int agent_chat(agent_t *agent, const char *message, char *response, size_t response_size);
/** Task-focused run: keeps working on the task until [TASK_COMPLETE] or max_attention_rounds. Use for "finish this before stopping". */
int agent_run_task(agent_t *agent, const char *task, char *response, size_t response_size);
/** Return the marker string the model must include when a task is done. For tests and clients. */
const char *agent_task_complete_marker(void);
int agent_build_prompt(agent_t *agent, const char *context, char *prompt, size_t prompt_size);
int agent_load_memory(agent_t *agent, const char *memory_id);
int agent_store_memory(agent_t *agent, const char *key, const char *value);
int agent_recall_memory(agent_t *agent, const char *query, char *result, size_t result_size);
int agent_forget_memory(agent_t *agent, const char *key);

int agent_set_config(agent_t *agent, const agent_runtime_config_t *config);
int agent_get_state(agent_t *agent, agent_state_t *state);
int agent_get_stats(agent_t *agent, size_t *tokens, int *tool_calls);

#endif
