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

---

## Incomplete

### 6. **RAG in agent loop (medium)**

- **Current:** RAG index/query exist but are not used in the agent.
- **Goal:** Before or after intent, run RAG query on user message or workspace index; inject top chunks into agent context (system or user message).
- **Touchpoints:** `src/agent/agent.c` (before/after `agent_chat` / tool loop).

### 7. **Integrations: Jira / Notion (lower)**

- **Current:** Stubs return placeholder data.
- **Goal:** Real HTTP + auth to Jira/Notion APIs; fill existing structs and return success/failure.
- **Touchpoints:** `src/integrations/integrations.c`.

### 8. **Migration: `migrate run <source>`** — *partial (generic JSON)*

- **Current:** `migrate run generic /path/to/export.json` imports into memory.
- **Goal:** Additional sources if needed.

### 9. **Logging module (nice to have)** — *done (log.h/log.c, export, levels)*

- **Current:** `log.c`/`log.h` with levels, file output, `log export`; gateway/daemon wire it.
- **Goal:** Replace remaining `printf` in hot paths if desired.

### 10. **Multi-instance registry (optional)**

- **Current:** Single config; job workers use one `default_config`; `instance_id` in jobs is present but get_config is NULL.
- **Goal:** If you want multiple logical instances in one process, add an instance registry and wire `jobworker_config_fn` to it.
- **Touchpoints:** New `instance` module; daemon/gateway to create instances and pass get_config to the worker pool.

---

## Summary

| Priority | Item | Status | Effort (rough) |
|----------|------|--------|----------------|
| 1 | Tools workspace scoping | Done | — |
| 2 | Webhooks → agent (HTTP) | Done | — |
| 3 | Config more sections | Done | — |
| 4 | Observability record/export | Done | — |
| 5 | Channel listeners + webhook reply | Done | — |
| 6 | RAG in agent | Incomplete | Medium |
| 7 | Jira/Notion integrations | Done (real HTTP) | — |
| 8 | Migration run | Partial (generic) | Small |
| 9 | Logging module | Done | — |
| 10 | Multi-instance registry | Optional | Medium |
