#include "agent.h"
#include "providers.h"
#include "memory.h"
#include "tools.h"
#include "rag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <regex.h>
#include <stdbool.h>

#define MAX_ITERATIONS 50
#define MAX_TOOL_CALLS 16
#define MAX_TOOL_ARGS 4096

typedef enum {
    DISPATCHER_NATIVE,
    DISPATCHER_XML
} dispatcher_type_t;

typedef struct {
    char name[128];
    char arguments[MAX_TOOL_ARGS];
    char tool_call_id[64];
} parsed_tool_call_t;

typedef struct {
    char output[8192];
    bool success;
    char tool_call_id[64];
} dispatcher_tool_result_t;

static dispatcher_type_t g_dispatcher_type = DISPATCHER_NATIVE;

static __attribute__((unused)) void dispatcher_set_type(dispatcher_type_t type) {
    g_dispatcher_type = type;
}

static int parse_xml_tool_calls(const char *response, parsed_tool_call_t *calls, size_t *call_count) {
    if (!response || !calls || !call_count) return -1;
    
    *call_count = 0;
    
    regex_t re;
    regmatch_t matches[4];
    
    if (regcomp(&re, "<tool_call>\\s*([^<]+)\\s*</tool_call>", REG_EXTENDED) != 0) {
        return -1;
    }
    
    const char *ptr = response;
    while (*ptr && *call_count < MAX_TOOL_CALLS) {
        if (regexec(&re, ptr, 4, matches, 0) == 0) {
            size_t match_len = matches[1].rm_eo - matches[1].rm_so;
            if (match_len < MAX_TOOL_ARGS) {
                char tool_xml[MAX_TOOL_ARGS] = {0};
                memcpy(tool_xml, ptr + matches[1].rm_so, match_len);
                
                char args[MAX_TOOL_ARGS] = {0};
                
                regex_t name_re, args_re;
                if (regcomp(&name_re, "\"name\"\\s*:\\s*\"([^\"]+)\"", REG_EXTENDED) == 0) {
                    if (regexec(&name_re, tool_xml, 2, matches, 0) == 0) {
                        size_t nlen = matches[1].rm_eo - matches[1].rm_so;
                        if (nlen < sizeof(calls[*call_count].name)) {
                            memcpy(calls[*call_count].name, tool_xml + matches[1].rm_so, nlen);
                        }
                    }
                    regfree(&name_re);
                }
                
                if (regcomp(&args_re, "\"arguments\"\\s*:\\s*(\\{[^}]+\\})", REG_EXTENDED) == 0) {
                    if (regexec(&args_re, tool_xml, 2, matches, 0) == 0) {
                        size_t alen = matches[1].rm_eo - matches[1].rm_so;
                        if (alen < MAX_TOOL_ARGS) {
                            memcpy(args, tool_xml + matches[1].rm_so, alen);
                        }
                    }
                    regfree(&args_re);
                }
                
                snprintf(calls[*call_count].arguments, MAX_TOOL_ARGS, "%s", args);
                (*call_count)++;
            }
            ptr += matches[0].rm_eo;
        } else {
            break;
        }
    }
    
    regfree(&re);
    return 0;
}

static int parse_native_tool_calls(const char *json_response, parsed_tool_call_t *calls, size_t *call_count) {
    if (!json_response || !calls || !call_count) return -1;
    
    *call_count = 0;
    
    const char *tool_calls_start = strstr(json_response, "\"tool_calls\"");
    if (!tool_calls_start) return -1;
    
    const char *arr_start = strchr(tool_calls_start, '[');
    if (!arr_start) return -1;
    
    const char *arr_end = strchr(arr_start, ']');
    if (!arr_end) return -1;
    
    char tools_section[8192] = {0};
    size_t section_len = arr_end - arr_start + 1;
    if (section_len < sizeof(tools_section)) {
        memcpy(tools_section, arr_start, section_len);
    }
    
    regex_t re;
    regmatch_t matches[3];
    
    if (regcomp(&re, "\"function\"\\s*:\\s*\\{[^}]*\"name\"\\s*:\\s*\"([^\"]+)\"[^}]*\"arguments\"\\s*:\\s*(\\{[^}]+\\})", REG_EXTENDED) != 0) {
        return -1;
    }
    
    const char *ptr = tools_section;
    while (*ptr && *call_count < MAX_TOOL_CALLS) {
        if (regexec(&re, ptr, 3, matches, 0) == 0) {
            size_t name_len = matches[1].rm_eo - matches[1].rm_so;
            size_t args_len = matches[2].rm_eo - matches[2].rm_so;
            
            if (name_len < sizeof(calls[*call_count].name) && args_len < MAX_TOOL_ARGS) {
                memcpy(calls[*call_count].name, ptr + matches[1].rm_so, name_len);
                memcpy(calls[*call_count].arguments, ptr + matches[2].rm_so, args_len);
                calls[*call_count].arguments[args_len] = '\0';
                (*call_count)++;
            }
            ptr += matches[2].rm_eo;
        } else {
            break;
        }
    }
    
    regfree(&re);
    return 0;
}

static int dispatcher_parse_tool_calls(const char *response, parsed_tool_call_t *calls, size_t *call_count) {
    if (g_dispatcher_type == DISPATCHER_XML) {
        return parse_xml_tool_calls(response, calls, call_count);
    }
    return parse_native_tool_calls(response, calls, call_count);
}

static void dispatcher_format_tool_result(const char *tool_name, const char *tool_result, bool success, char *output, size_t output_size) {
    const char *status = success ? "ok" : "error";
    snprintf(output, output_size, 
        "<tool_result>\n"
        "  <tool>%s</tool>\n"
        "  <status>%s</status>\n"
        "  <output>%s</output>\n"
        "</tool_result>",
        tool_name, status, tool_result);
}

static memory_t g_memory = {0};
static bool g_memory_initialized = false;

int agent_memory_init(const char *backend, const char *workspace_dir) {
    (void)backend;
    if (g_memory_initialized) return 0;
    
    int result = memory_init(&g_memory, MEMORY_BACKEND_SQLITE, workspace_dir);
    if (result == 0) {
        g_memory_initialized = true;
    }
    return result;
}

static int agent_load_memory_context(const char *user_message, char *context, size_t context_size) {
    if (!g_memory_initialized || !context) return -1;
    
    context[0] = '\0';
    
    memory_item_t items[5] = {0};
    
    char search_query[256] = {0};
    snprintf(search_query, sizeof(search_query), "recent_%s", user_message);
    
    int result = memory_recall(&g_memory, search_query, &items[0]);
    
    if (result == 0 && items[0].value[0]) {
        size_t offset = 0;
        offset += snprintf(context + offset, context_size - offset, "Relevant context:\n");
        
        for (size_t i = 0; i < 5 && items[i].value[0]; i++) {
            size_t len = strlen(items[i].value);
            if (offset + len + 10 < context_size) {
                offset += snprintf(context + offset, context_size - offset, "- %s\n", items[i].value);
            }
        }
    }
    
    return 0;
}

int agent_store_in_memory(const char *key, const char *value) {
    if (!g_memory_initialized) return -1;
    
    memory_item_t item = {0};
    snprintf(item.key, sizeof(item.key), "%s", key);
    snprintf(item.value, sizeof(item.value), "%s", value);
    item.category = MEMORY_CATEGORY_SHORT_TERM;
    item.timestamp = (uint64_t)time(NULL);
    
    return memory_store(&g_memory, &item);
}

#define MAX_HISTORY_MESSAGES 50
#define COMPACTION_KEEP_RECENT 20

static void agent_trim_history(chat_context_t *context) {
    if (!context || context->message_count <= MAX_HISTORY_MESSAGES) {
        return;
    }
    
    size_t system_count = 0;
    for (size_t i = 0; i < context->message_count; i++) {
        if (strcmp(context->messages[i].role, "system") == 0) {
            system_count++;
        }
    }
    
    size_t non_system_count = context->message_count - system_count;
    if (non_system_count <= MAX_HISTORY_MESSAGES) {
        return;
    }
    
    size_t drop_count = non_system_count - COMPACTION_KEEP_RECENT;
    if (drop_count <= 0) return;
    
    message_t *new_messages = malloc(sizeof(message_t) * (system_count + COMPACTION_KEEP_RECENT + 8));
    if (!new_messages) return;
    
    size_t new_count = 0;
    
    for (size_t i = 0; i < context->message_count && new_count < system_count; i++) {
        if (strcmp(context->messages[i].role, "system") == 0) {
            memcpy(&new_messages[new_count++], &context->messages[i], sizeof(message_t));
        }
    }
    
    size_t copied = 0;
    for (size_t i = context->message_count; i > 0 && copied < COMPACTION_KEEP_RECENT; i--) {
        if (strcmp(context->messages[i - 1].role, "system") != 0) {
            memcpy(&new_messages[new_count++], &context->messages[i - 1], sizeof(message_t));
            copied++;
        }
    }
    
    if (context->messages) {
        free(context->messages);
    }
    
    context->messages = new_messages;
    context->message_count = new_count;
    context->message_capacity = new_count + 8;
}
#define SYSTEM_PROMPT "You are Doctor Claw, an autonomous AI assistant. You can execute tools to help users. Always be helpful, honest, and safe."

#define TASK_COMPLETE_MARKER "[TASK_COMPLETE]"
#define DEFAULT_MAX_ATTENTION_ROUNDS 5

static const char *g_default_tools = "[\n"
"  {\n"
"    \"type\": \"function\",\n"
"    \"function\": {\n"
"      \"name\": \"shell\",\n"
"      \"description\": \"Execute a shell command and return the output\",\n"
"      \"parameters\": {\n"
"        \"type\": \"object\",\n"
"        \"properties\": {\n"
"          \"command\": {\"type\": \"string\", \"description\": \"The shell command to execute\"}\n"
"        },\n"
"        \"required\": [\"command\"]\n"
"      }\n"
"    }\n"
"  },\n"
"  {\n"
"    \"type\": \"function\",\n"
"    \"function\": {\n"
"      \"name\": \"read_file\",\n"
"      \"description\": \"Read contents of a file\",\n"
"      \"parameters\": {\n"
"        \"type\": \"object\",\n"
"        \"properties\": {\n"
"          \"path\": {\"type\": \"string\", \"description\": \"Path to the file to read\"}\n"
"        },\n"
"        \"required\": [\"path\"]\n"
"      }\n"
"    }\n"
"  },\n"
"  {\n"
"    \"type\": \"function\",\n"
"    \"function\": {\n"
"      \"name\": \"http_get\",\n"
"      \"description\": \"Make an HTTP GET request\",\n"
"      \"parameters\": {\n"
"        \"type\": \"object\",\n"
"        \"properties\": {\n"
"          \"url\": {\"type\": \"string\", \"description\": \"URL to fetch\"}\n"
"        },\n"
"        \"required\": [\"url\"]\n"
"      }\n"
"    }\n"
"  },\n"
"  {\n"
"    \"type\": \"function\",\n"
"    \"function\": {\n"
"      \"name\": \"memory_store\",\n"
"      \"description\": \"Store information in persistent memory\",\n"
"      \"parameters\": {\n"
"        \"type\": \"object\",\n"
"        \"properties\": {\n"
"          \"key\": {\"type\": \"string\", \"description\": \"Memory key\"},\n"
"          \"value\": {\"type\": \"string\", \"description\": \"Value to store\"}\n"
"        },\n"
"        \"required\": [\"key\", \"value\"]\n"
"      }\n"
"    }\n"
"  },\n"
"  {\n"
"    \"type\": \"function\",\n"
"    \"function\": {\n"
"      \"name\": \"memory_recall\",\n"
"      \"description\": \"Recall information from persistent memory\",\n"
"      \"parameters\": {\n"
"        \"type\": \"object\",\n"
"        \"properties\": {\n"
"          \"query\": {\"type\": \"string\", \"description\": \"Query to search memory\"}\n"
"        },\n"
"        \"required\": [\"query\"]\n"
"      }\n"
"    }\n"
"  }\n"
"]";

static __attribute__((unused)) const char *INTENT_KEYWORDS[] = {
    "code", "function", "class", "implement", "write", "create", "build",
    "search", "find", "look", "query",
    "read", "write", "edit", "delete", "copy", "move", "file", "directory",
    "run", "execute", "command", "bash", "shell", "terminal",
    "remember", "recall", "forget", "store", "memory",
    "analyze", "explain", "review", "check"
};

int agent_init(agent_t *agent, const config_t *config) {
    if (!agent || !config) return -1;
    
    memset(agent, 0, sizeof(agent_t));
    tools_set_workspace(config->paths.workspace_dir);
    strncpy(agent->workspace_dir, config->paths.workspace_dir, sizeof(agent->workspace_dir) - 1);
    agent->workspace_dir[sizeof(agent->workspace_dir) - 1] = '\0';
    
    snprintf(agent->name, sizeof(agent->name), "DoctorClaw");
    snprintf(agent->description, sizeof(agent->description), "Autonomous AI assistant");
    
    agent->state = AGENT_STATE_IDLE;
    agent->tool_count = 0;
    agent->require_approval = config->autonomy.require_approval;
    approval_manager_init(&agent->approval_manager);
    
    chat_context_init(&agent->context);
    chat_context_add_message(&agent->context, "system", SYSTEM_PROMPT);
    
    srand((unsigned int)time(NULL));
    snprintf(agent->session_id, sizeof(agent->session_id), "%08x%08x", rand(), rand());
    agent->started_at = (uint64_t)time(NULL);
    
    return 0;
}

int agent_classify_intent(const char *input, intent_classification_t *result) {
    if (!input || !result) return -1;
    
    memset(result, 0, sizeof(intent_classification_t));
    result->type = INTENT_UNKNOWN;
    result->confidence = 0.0;
    
    char *lower = strdup(input);
    if (!lower) return -1;
    
    for (size_t i = 0; lower[i]; i++) {
        lower[i] = (char)tolower(lower[i]);
    }
    
    if (strstr(lower, "code") || strstr(lower, "function") || strstr(lower, "class") ||
        strstr(lower, "implement") || strstr(lower, "write") || strstr(lower, "create")) {
        result->type = INTENT_CODE;
        result->confidence = 0.8;
    } else if (strstr(lower, "search") || strstr(lower, "find") || strstr(lower, "look")) {
        result->type = INTENT_SEARCH;
        result->confidence = 0.85;
    } else if (strstr(lower, "read") || strstr(lower, "file") || strstr(lower, "edit") ||
               strstr(lower, "delete") || strstr(lower, "copy") || strstr(lower, "move")) {
        result->type = INTENT_FILE_OP;
        result->confidence = 0.9;
    } else if (strstr(lower, "run") || strstr(lower, "execute") || strstr(lower, "command") ||
               strstr(lower, "bash") || strstr(lower, "shell")) {
        result->type = INTENT_SHELL_CMD;
        result->confidence = 0.85;
    } else if (strstr(lower, "remember") || strstr(lower, "recall") || strstr(lower, "store")) {
        result->type = INTENT_MEMORY;
        result->confidence = 0.9;
    } else if (strstr(lower, "analyze") || strstr(lower, "explain") || strstr(lower, "review")) {
        result->type = INTENT_ANALYSIS;
        result->confidence = 0.75;
    } else {
        result->type = INTENT_GENERAL;
        result->confidence = 0.5;
    }
    
    snprintf(result->description, sizeof(result->description), "Intent: %s (%.2f)",
             result->type == INTENT_CODE ? "code" :
             result->type == INTENT_SEARCH ? "search" :
             result->type == INTENT_FILE_OP ? "file operation" :
             result->type == INTENT_SHELL_CMD ? "shell command" :
             result->type == INTENT_MEMORY ? "memory" :
             result->type == INTENT_ANALYSIS ? "analysis" : "general",
             result->confidence);
    
    free(lower);
    return 0;
}

int agent_dispatch(agent_t *agent, const char *input, char *response, size_t response_size) {
    if (!agent || !input || !response) return -1;
    
    intent_classification_t intent = {0};
    agent_classify_intent(input, &intent);
    
    agent->state = AGENT_STATE_THINKING;
    
    switch (intent.type) {
        case INTENT_CODE:
            snprintf(response, response_size, "[Dispatched to code handler] %s", input);
            break;
        case INTENT_SEARCH:
            snprintf(response, response_size, "[Dispatched to search handler] %s", input);
            break;
        case INTENT_FILE_OP:
            snprintf(response, response_size, "[Dispatched to file handler] %s", input);
            break;
        case INTENT_SHELL_CMD:
            snprintf(response, response_size, "[Dispatched to shell handler] %s", input);
            break;
        case INTENT_MEMORY:
            snprintf(response, response_size, "[Dispatched to memory handler] %s", input);
            break;
        case INTENT_ANALYSIS:
            snprintf(response, response_size, "[Dispatched to analysis handler] %s", input);
            break;
        default:
            snprintf(response, response_size, "[Dispatched to general handler] %s", input);
            break;
    }
    
    agent->tool_calls_count++;
    agent->state = AGENT_STATE_IDLE;
    
    return 0;
}

int agent_build_prompt(agent_t *agent, const char *context, char *prompt, size_t prompt_size) {
    if (!agent || !prompt) return -1;
    
    size_t offset = 0;
    
    offset += snprintf(prompt + offset, prompt_size - offset,
        "You are Doctor Claw, an autonomous AI assistant.\n\n");
    
    if (context) {
        offset += snprintf(prompt + offset, prompt_size - offset,
            "Context:\n%s\n\n", context);
    }
    
    offset += snprintf(prompt + offset, prompt_size - offset,
        "Available tools:\n");
    
    for (size_t i = 0; i < agent->tool_count && offset < prompt_size - 100; i++) {
        offset += snprintf(prompt + offset, prompt_size - offset,
            "- %s: %s\n", agent->tools[i].name, agent->tools[i].description);
    }
    
    return 0;
}

static __attribute__((unused)) int agent_think(agent_t *agent, const char *user_message, char *response, size_t resp_size) {
    chat_context_add_message(&agent->context, "user", user_message);
    
    provider_type_t ptype = PROVIDER_OPENROUTER;
    const char *model = "openai/gpt-4o-mini";
    const char *api_key = getenv("OPENROUTER_API_KEY");
    
    if (!api_key) {
        api_key = getenv("OPENAI_API_KEY");
        ptype = PROVIDER_OPENAI;
        model = "gpt-4o-mini";
    }
    
    if (!api_key) {
        snprintf(response, resp_size, "No API key configured. Set OPENROUTER_API_KEY or OPENAI_API_KEY");
        return -1;
    }
    
    chat_response_t resp = {0};
    int result = provider_chat_completion(ptype, model, api_key, &agent->context, &resp);
    
    if (result == 0 && resp.content[0]) {
        size_t copy_len = strlen(resp.content);
        if (copy_len >= resp_size) {
            copy_len = resp_size - 1;
        }
        memcpy(response, resp.content, copy_len);
        response[copy_len] = '\0';
        
        chat_context_add_message(&agent->context, "assistant", response);
        agent->total_tokens += resp.tokens_used;
    } else {
        snprintf(response, resp_size, "Error: Failed to get response from provider");
    }
    
    return 0;
}

static __attribute__((unused)) int agent_execute_tool_call(agent_t *agent, const char *tool_call_json, char *result, size_t result_size) {
    (void)agent;
    char tool_name[64] = {0};
    char params[4096] = {0};
    
    if (sscanf(tool_call_json, "{\"tool\":\"%63[^\"]\",\"params\":\"%4095[^\"]\"}", tool_name, params) == 2) {
        tool_result_t tr = {0};
        int res = tools_execute(tool_name, params, &tr);
        
        if (res == 0) {
            snprintf(result, result_size, "%s", tr.result);
        } else {
            snprintf(result, result_size, "Error: %s", tr.error);
        }
    } else {
        snprintf(result, result_size, "Error: Could not parse tool call");
    }
    
    return 0;
}

static int agent_execute_tool_by_name(const char *name, const char *args_json, char *result, size_t result_size) {
    tool_result_t tr = {0};
    
    char path[1024] = {0};
    char url[2048] = {0};
    char key[256] = {0};
    char value[4096] = {0};
    char query[1024] = {0};
    
    if (strcmp(name, "shell") == 0) {
        if (sscanf(args_json, "{\"command\":\"%1023[^\"]\"}", path) == 1) {
            tools_execute("shell", path, &tr);
        } else {
            snprintf(result, result_size, "Error: Could not parse shell command");
            return -1;
        }
    } else if (strcmp(name, "read_file") == 0) {
        if (sscanf(args_json, "{\"path\":\"%1023[^\"]\"}", path) == 1) {
            tools_execute("read_file", path, &tr);
        } else {
            snprintf(result, result_size, "Error: Could not parse file path");
            return -1;
        }
    } else if (strcmp(name, "http_get") == 0) {
        if (sscanf(args_json, "{\"url\":\"%2047[^\"]\"}", url) == 1) {
            tools_execute("http_get", url, &tr);
        } else {
            snprintf(result, result_size, "Error: Could not parse URL");
            return -1;
        }
    } else if (strcmp(name, "memory_store") == 0) {
        if (sscanf(args_json, "{\"key\":\"%255[^\"]\",\"value\":\"%4095[^\"]\"}", key, value) == 2) {
            char store_cmd[4608] = {0};
            snprintf(store_cmd, sizeof(store_cmd), "{\"key\":\"%s\",\"value\":\"%s\"}", key, value);
            tools_execute("memory_store", store_cmd, &tr);
        } else {
            snprintf(result, result_size, "Error: Could not parse memory store args");
            return -1;
        }
    } else if (strcmp(name, "memory_recall") == 0) {
        if (sscanf(args_json, "{\"query\":\"%1023[^\"]\"}", query) == 1) {
            tools_execute("memory_recall", query, &tr);
        } else {
            snprintf(result, result_size, "Error: Could not parse memory recall query");
            return -1;
        }
    } else {
        snprintf(result, result_size, "Error: Unknown tool '%s'", name);
        return -1;
    }
    
    if (tr.result[0]) {
        snprintf(result, result_size, "%s", tr.result);
    } else if (tr.error[0]) {
        snprintf(result, result_size, "Error: %s", tr.error);
    } else {
        snprintf(result, result_size, "Tool executed successfully");
    }
    
    return 0;
}

static int agent_think_with_tools(agent_t *agent, const char *user_message, char *response, size_t resp_size) {
    char memory_context[4096] = {0};
    if (g_memory_initialized) {
        agent_load_memory_context(user_message, memory_context, sizeof(memory_context));
    }
    
    char enriched_message[8192] = {0};
    if (memory_context[0]) {
        snprintf(enriched_message, sizeof(enriched_message),
            "%s\n\nRelevant memory context:\n%s",
            user_message, memory_context);
        chat_context_add_message(&agent->context, "user", enriched_message);
    } else {
        chat_context_add_message(&agent->context, "user", user_message);
    }
    
    if (g_memory_initialized) {
        char mem_key[256] = {0};
        snprintf(mem_key, sizeof(mem_key), "user_msg_%lu", (unsigned long)time(NULL));
        agent_store_in_memory(mem_key, user_message);
    }
    
    provider_type_t ptype = PROVIDER_OPENROUTER;
    const char *model = "openai/gpt-4o-mini";
    const char *api_key = getenv("OPENROUTER_API_KEY");
    
    if (!api_key) {
        api_key = getenv("OPENAI_API_KEY");
        ptype = PROVIDER_OPENAI;
        model = "gpt-4o-mini";
    }
    
    if (!api_key) {
        snprintf(response, resp_size, "Error: No API key configured. Set OPENROUTER_API_KEY or OPENAI_API_KEY");
        return -1;
    }
    
    int iteration = 0;
    int max_iterations = MAX_ITERATIONS;
    
    while (iteration < max_iterations) {
        chat_response_t resp = {0};
        
        if (g_dispatcher_type == DISPATCHER_XML) {
            char prompt_with_tools[16384] = {0};
            snprintf(prompt_with_tools, sizeof(prompt_with_tools),
                "%s\n\nYou have access to tools. When you need to use a tool, "
                "respond with XML format:\n<tool_call>{\"name\": \"tool_name\", \"arguments\": {...}}</tool_call>",
                agent->context.messages[0].content);
            
            chat_context_t xml_context = {0};
            xml_context.messages = agent->context.messages;
            xml_context.message_count = agent->context.message_count;
            
            int result = provider_chat_completion(ptype, model, api_key, &xml_context, &resp);
            (void)result;
        } else {
            int result = provider_chat_with_tools(ptype, model, api_key, &agent->context, g_default_tools, &resp);
            (void)result;
        }
        
        if (resp.content[0] == '\0') {
            snprintf(response, resp_size, "Error: Failed to get response from provider");
            return -1;
        }
        
        size_t tool_call_count = 0;
        parsed_tool_call_t parsed_calls[MAX_TOOL_CALLS] = {0};
        
        if (resp.tool_calls.call_count > 0) {
            for (size_t i = 0; i < resp.tool_calls.call_count && i < MAX_TOOL_CALLS; i++) {
                snprintf(parsed_calls[tool_call_count].name, sizeof(parsed_calls[tool_call_count].name), 
                    "%s", resp.tool_calls.calls[i].name);
                snprintf(parsed_calls[tool_call_count].arguments, sizeof(parsed_calls[tool_call_count].arguments),
                    "%s", resp.tool_calls.calls[i].arguments);
                tool_call_count++;
            }
        } else if (g_dispatcher_type == DISPATCHER_XML) {
            dispatcher_parse_tool_calls(resp.content, parsed_calls, &tool_call_count);
        }
        
        if (tool_call_count > 0) {
            printf("[Agent] Executing %zu tool call(s)...\n", tool_call_count);
            tools_set_approval_context(&agent->approval_manager, agent->require_approval);
            for (size_t i = 0; i < tool_call_count; i++) {
                const char *tool_name = parsed_calls[i].name;
                const char *tool_args = parsed_calls[i].arguments;
                
                printf("[Agent] Tool: %s\n", tool_name);
                
                char tool_result[8192] = {0};
                agent_execute_tool_by_name(tool_name, tool_args, tool_result, sizeof(tool_result));
                
                char tool_msg[12288] = {0};
                if (g_dispatcher_type == DISPATCHER_XML) {
                    dispatcher_format_tool_result(tool_name, tool_result, true, tool_msg, sizeof(tool_msg));
                } else {
                    snprintf(tool_msg, sizeof(tool_msg), 
                        "Tool '%s' returned: %s", tool_name, tool_result);
                }
                
                chat_context_add_message(&agent->context, "tool", tool_msg);
                agent->tool_calls_count++;
            }
            
            iteration++;
            agent_trim_history(&agent->context);
            continue;
        }
        
        size_t copy_len = strlen(resp.content);
        if (copy_len >= resp_size) {
            copy_len = resp_size - 1;
        }
        memcpy(response, resp.content, copy_len);
        response[copy_len] = '\0';
        
        chat_context_add_message(&agent->context, "assistant", response);
        agent->total_tokens += resp.tokens_used;
        
        agent_trim_history(&agent->context);
        
        return 0;
    }
    
    snprintf(response, resp_size, "Error: Max iterations (%d) exceeded", max_iterations);
    return -1;
}

int agent_start(agent_t *agent, const char *message) {
    if (!agent || !message) return -1;
    
    agent->state = AGENT_STATE_THINKING;
    
    char response[16384] = {0};
    int result = agent_think_with_tools(agent, message, response, sizeof(response));
    
    if (result == 0) {
        printf("[Agent] Response: %s\n", response);
    }
    
    agent->state = AGENT_STATE_IDLE;
    return result;
}

int agent_chat(agent_t *agent, const char *message, char *response, size_t response_size) {
    if (!agent || !message || !response || response_size == 0) return -1;
    agent->state = AGENT_STATE_THINKING;
    char augmented_message[8192];
    size_t aug_len = 0;
    if (agent->workspace_dir[0]) {
        char rag_path[512];
        snprintf(rag_path, sizeof(rag_path), "%s/rag.idx", agent->workspace_dir);
        rag_index_t rag = {0};
        if (rag_index_load(&rag, rag_path) == 0) {
            rag_result_t rag_result = {0};
            if (rag_index_query(&rag, message, 5, &rag_result) == 0 && rag_result.chunk_count > 0) {
                aug_len = (size_t)snprintf(augmented_message, sizeof(augmented_message), "Relevant context from RAG:\n");
                for (size_t i = 0; i < rag_result.chunk_count && aug_len < sizeof(augmented_message) - 256; i++) {
                    aug_len += (size_t)snprintf(augmented_message + aug_len, sizeof(augmented_message) - aug_len,
                        "[%zu] %s\n", i + 1, rag_result.chunks[i]);
                }
                aug_len += (size_t)snprintf(augmented_message + aug_len, sizeof(augmented_message) - aug_len, "\nUser: %s", message);
                rag_index_free(&rag);
                int r = agent_think_with_tools(agent, augmented_message, response, response_size);
                agent->state = AGENT_STATE_IDLE;
                return r;
            }
            rag_index_free(&rag);
        }
    }
    aug_len = 0;
    (void)aug_len;
    int r = agent_think_with_tools(agent, message, response, response_size);
    agent->state = AGENT_STATE_IDLE;
    return r;
}

static bool response_indicates_task_complete(const char *response) {
    if (!response) return false;
    return strstr(response, TASK_COMPLETE_MARKER) != NULL;
}

const char *agent_task_complete_marker(void) {
    return TASK_COMPLETE_MARKER;
}

int agent_run_task(agent_t *agent, const char *task, char *response, size_t response_size) {
    if (!agent || !task || !response || response_size == 0) return -1;

    agent->state = AGENT_STATE_THINKING;
    agent->task_focus_enabled = true;
    snprintf(agent->current_task, sizeof(agent->current_task), "%s", task);
    int max_rounds = agent->max_attention_rounds > 0 ? agent->max_attention_rounds : DEFAULT_MAX_ATTENTION_ROUNDS;

    static const char task_instruction[] =
        "TASK (complete this fully before marking done):\n\n%s\n\n"
        "Use tools as needed. Stay focused on this task. "
        "When you have fully completed it, your final reply must include exactly " TASK_COMPLETE_MARKER " and a brief summary. "
        "Do not say " TASK_COMPLETE_MARKER " until the task is actually done.";
    static const char continue_instruction[] =
        "Continue with the task. If you have fully completed it, respond with " TASK_COMPLETE_MARKER " and a brief summary. "
        "Otherwise continue with the next steps.";

    char task_message[2048];
    snprintf(task_message, sizeof(task_message), task_instruction, task);

    int round = 0;
    int result = 0;
    while (round < max_rounds) {
        const char *user_msg = (round == 0) ? task_message : continue_instruction;
        result = agent_think_with_tools(agent, user_msg, response, response_size);
        if (result != 0) {
            agent->task_focus_enabled = false;
            agent->state = AGENT_STATE_IDLE;
            return result;
        }
        if (response_indicates_task_complete(response)) {
            agent->task_focus_enabled = false;
            agent->state = AGENT_STATE_IDLE;
            return 0;
        }
        round++;
        printf("[Agent] Attention round %d/%d – task not yet complete, continuing.\n", round, max_rounds);
    }

    agent->task_focus_enabled = false;
    agent->state = AGENT_STATE_IDLE;
    return 0;
}

int agent_run_loop(agent_t *agent) {
    if (!agent) return -1;
    
    char input[4096] = {0};
    
    printf("[Agent] Starting interactive loop (type 'exit' to quit)\n");
    printf("[Agent] Task-focused mode: each message is a task; agent runs until [TASK_COMPLETE] or max rounds.\n");
    printf("[Agent] Available tools: shell, read_file, http_get, memory_store, memory_recall\n\n");
    
    while (1) {
        printf("\n> ");
        if (!fgets(input, sizeof(input), stdin)) break;
        
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;
        
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            break;
        }
        
        char response[16384] = {0};
        int result = agent_run_task(agent, input, response, sizeof(response));
        
        if (result == 0) {
            printf("\n%s\n", response);
        } else {
            printf("\nError: %s\n", response);
        }
    }
    
    return 0;
}

int agent_add_tool(agent_t *agent, const tool_definition_t *tool) {
    if (!agent || !tool || agent->tool_count >= MAX_TOOL_COUNT) {
        return -1;
    }
    
    memcpy(&agent->tools[agent->tool_count++], tool, sizeof(tool_definition_t));
    return 0;
}

int agent_execute_tool(agent_t *agent, const tool_call_t *call, tool_result_t *result) {
    if (!agent || !call || !result) return -1;
    
    return tools_execute(call->name, call->arguments, result);
}

void agent_free(agent_t *agent) {
    if (!agent) return;
    tools_set_approval_context(NULL, false);
    approval_manager_free(&agent->approval_manager);
    chat_context_free(&agent->context);
    memset(agent, 0, sizeof(agent_t));
}

int agent_load_memory(agent_t *agent, const char *memory_id) {
    if (!agent || !memory_id) return -1;
    printf("[Agent] Loading memory: %s\n", memory_id);
    return 0;
}

int agent_store_memory(agent_t *agent, const char *key, const char *value) {
    if (!agent || !key || !value) return -1;
    printf("[Agent] Stored memory: %s = %s\n", key, value);
    return 0;
}

int agent_recall_memory(agent_t *agent, const char *query, char *result, size_t result_size) {
    if (!agent || !query || !result) return -1;
    snprintf(result, result_size, "Recalled: %s", query);
    return 0;
}

int agent_forget_memory(agent_t *agent, const char *key) {
    if (!agent || !key) return -1;
    printf("[Agent] Forgot memory: %s\n", key);
    return 0;
}

int agent_set_config(agent_t *agent, const agent_runtime_config_t *config) {
    if (!agent || !config) return -1;
    if (config->max_attention_rounds > 0)
        agent->max_attention_rounds = config->max_attention_rounds;
    printf("[Agent] Config updated\n");
    return 0;
}

int agent_get_state(agent_t *agent, agent_state_t *state) {
    if (!agent || !state) return -1;
    *state = agent->state;
    return 0;
}

int agent_get_stats(agent_t *agent, size_t *tokens, int *tool_calls) {
    if (!agent) return -1;
    if (tokens) *tokens = agent->total_tokens;
    if (tool_calls) *tool_calls = agent->tool_calls_count;
    return 0;
}
