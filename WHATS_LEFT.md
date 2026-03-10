# What's Left to Make Doctor Claw Fully Functional

This list is ordered by impact for a **single-process, cache-orchestrated, task-based** setup.

---

## Done

| Item | Description |
|------|-------------|
| **1. Tools workspace scoping** | `tools_set_workspace(workspace_dir)` added; called from `agent_init` and job worker before dispatch. |
| **2. Webhooks → agent** | Webhooks parse message, run agent (or enqueue job), return `{"response":"..."}` in HTTP body. Optional next: send reply via channel API. |
| **3. Config more sections** | INI parser applies `[memory]`, `[autonomy]`, `[observability]`, `[agent]`. |
| **4. Observability record/export** | `observability_global_init` / `observability_record_global`; gateway records requests and exposes `GET /metrics`. |
| **5. Channel listeners + reply** | Telegram getUpdates poll thread in daemon; gateway loads channels and calls `channels_reply_to_webhook` so agent replies are sent back to Telegram/Discord/Slack; Slack URL verification for Events API. |
| **6. RAG in agent** | `agent_chat` loads `workspace_dir/rag.idx`, runs `rag_index_query(message, 5)`, and injects "Relevant context from RAG" into the user message. `agent_run_task` does the same for the initial task message when RAG index exists. |
| **7. Jira / Notion integrations** | Jira: search_issues, create_issue, and transition_issue use real HTTP. Notion: search (parses results into notion_page_t), get_page (GET + parse), create_page (POST). |
| **8. Llama.cpp sampling** | `llama_get_logits` dlsym’d; next token is sampled via argmax over logits. Without get_logits, generation stops after prompt (no garbage repeat). |
| **9. Logging module** | `log.c`/`log.h` with levels, file output, `log export`; gateway/daemon wire it. |

---

## Incomplete / Deferred

### Pentest (deferred)

- **Current:** Runtime tests for pentest are skipped (tooling not ready). CLI `doctorclaw pentest [base_url]` still runs the suite when you have a gateway up.
- **Goal:** When tooling is ready, re-enable pentest tests and any extra checks.

### Migration: `migrate run <source>` — *expanded*

- **Current:** `migrate run generic /path/to/export.json` imports key-value JSON into memory. Same import is used for `ollama`, `claude`, and `openai` sources when path ends in `.json`.
- **Goal:** Additional source-specific parsers if needed.

### Multi-instance registry — *done*

- **Current:** `instance` module: `instance_init()`, `instance_register(id, config)`, `instance_get_config(id, &out)`, `instance_shutdown()`. Daemon registers `"default"` and passes `instance_get_config` to the job worker pool so jobs resolve `instance_id` → config. Optional: register more instances for multi-tenant.

### Logging (nice to have)

- Replace remaining `printf` in hot paths with the log module if desired.

---

## Summary

| Priority | Item | Status | Effort (rough) |
|----------|------|--------|----------------|
| 1 | Tools workspace scoping | Done | — |
| 2 | Webhooks → agent (HTTP) | Done | — |
| 3 | Config more sections | Done | — |
| 4 | Observability record/export | Done | — |
| 5 | Channel listeners + webhook reply | Done | — |
| 6 | RAG in agent | Done | — |
| 7 | Jira/Notion integrations | Done | — |
| 8 | Llama.cpp sampling | Done | — |
| 9 | Logging module | Done | — |
| 10 | Pentest | Deferred (tooling not ready) | — |
| 11 | Migration run | Expanded (generic + ollama/claude/openai JSON) | — |
| 12 | Multi-instance registry | Done (instance module + daemon get_config) | — |
| 13 | Health provider check | Done (GEMINI, LLAMA, OLLAMA_HOST) | — |
| 14 | OLLAMA_HOST in providers | Done (Ollama API base URL from env) | — |
