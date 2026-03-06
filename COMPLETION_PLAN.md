# Doctor Claw — Completion Plan

This document tracks work to **complete** the Doctor Claw implementation: wiring stubbed CLI paths, integrating components, and filling gaps so the system is usable end-to-end.

---

## Done

| Item | Description |
|------|--------------|
| **Cron CLI** | `doctorclaw cron list` loads and lists tasks; `cron add <id> <expr> <cmd>` and `cron remove <id>` call the real cron module and persist to disk. `cron_list_tasks()` exposed in `cron.h`. |
| **Daemon + Gateway** | In daemon mode, the gateway runs in a pthread so HTTP (e.g. `/agent/chat`, `/health`, webhooks) is available while the daemon cron loop runs. Config is loaded and gateway port/host come from config. |

---

## In progress / Next

### 1. Gateway webhooks → agent (high impact)

### 2. Gateway webhooks → agent (high impact)
- **Current:** `/telegram`, `/discord`, `/slack` return static JSON; they don’t run the agent or reply to the channel.
- **Goal:** On POST with a message body, run the agent on the message and (optionally) send the reply back via the channel API.
- **Touchpoints:** `src/gateway/gateway.c` handler for each webhook: parse message → `agent_run_task` or `agent_chat` → call channel send (if configured).

### 2. Config: more sections from file (medium)
- **Current:** INI parser applies `[paths]`, `[provider]`, `[gateway]`.
- **Goal:** Support `[memory]`, `[autonomy]`, `[observability]` etc. so full config can be file-driven.
- **Touchpoints:** `src/config/config.c` `parse_ini_line` / apply logic.

### 3. Observability: record + export (medium)
- **Current:** Metrics API exists; agent/gateway don’t record; no `/metrics` endpoint.
- **Goal:** Record request counts, latency, tool calls in gateway/agent; expose Prometheus `/metrics` from gateway.
- **Touchpoints:** `observability_record*` in gateway and agent; new route in `gateway.c`.

### 4. RAG in agent loop (medium)
- **Current:** RAG index/query exist but aren’t used in the agent.
- **Goal:** Before or after intent, run RAG query on user message or workspace index; inject top chunks into context.
- **Touchpoints:** `src/agent/agent.c`: call RAG query, append to system or user message.

### 5. Integrations: Jira / Notion (lower)
- **Current:** Stubs return placeholder data.
- **Goal:** Real HTTP calls to Jira/Notion APIs with auth; fill structs and return success/failure.
- **Touchpoints:** `src/integrations/integrations.c`.

### 6. Channel listeners (lower)
- **Current:** `channel_start_all` calls `channel_connect` synchronously; no long-lived polling or webhook listeners.
- **Goal:** Optional background threads (or non-blocking loop) so Telegram/Discord/Slack actually receive messages and can trigger the agent.
- **Touchpoints:** `src/channels/channels.c`, daemon or gateway to start listeners.

### 7. Logging module (nice to have)
- **Current:** printf everywhere.
- **Goal:** Log levels, optional file output, optional rotation.
- **Touchpoints:** New `log.c`/`log.h`, replace printf in gateway, agent, daemon.

---

## Reference

- **Feature inventory:** `FEATURES_AND_RECOMMENDATIONS.md`
- **Codebase scan:** `CODEBASE_SCAN.md`
