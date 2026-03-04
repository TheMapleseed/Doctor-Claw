# Doctor Claw — Features & Recommendations

## Current Features (Implemented or Scaffolded)

### CLI commands
| Command        | Description                              | Status / Notes                          |
|----------------|------------------------------------------|-----------------------------------------|
| **onboard**    | Initialize workspace and config          | Creates dirs, config.toml, auth.toml, channels |
| **agent**      | Start AI agent (interactive or one-shot) | Uses OpenRouter/OpenAI, tool loop       |
| **gateway**    | HTTP + WebSocket server                  | Index, /health, /webhook, /telegram, /discord, /slack, /agent/chat, WS echo |
| **daemon**     | Long-running autonomous runtime          | Runs runtime, prints status loop        |
| **service**    | OS service lifecycle                     | list, register, start, stop, enable, disable |
| **doctor**     | Diagnostics                              | Config, auth, providers, memory, channels |
| **status**     | System status                            | Version, runtime, health components     |
| **cron**       | Scheduled tasks                          | list, add (add is placeholder)          |
| **models**     | Model catalog (display)                  | Static list of OpenAI, Anthropic, etc.  |
| **providers**  | AI providers + env vars                  | OpenRouter, OpenAI, Anthropic, Ollama, etc. |
| **channel**    | Channels (Telegram, Discord, Slack)      | list/add/remove/start/stop (list only)  |
| **integrations** | Integrations (GitHub, Jira, Notion, Slack) | List/add, display enabled              |
| **skills**     | User-defined skills                      | list, add, run, enable, disable         |
| **migrate**    | Migrate from other runtimes              | list/sources, run (not fully implemented) |
| **auth**       | Auth profiles                            | list, add, remove, active               |
| **hardware**   | USB/serial discovery                     | Scan and list devices                   |
| **peripheral** | Hardware peripherals                    | list, scan (wraps hardware)             |

### Agent & AI
- **Intent classification** (code, search, file, shell, memory, analysis)
- **Chat loop** with OpenRouter/OpenAI, tool-calling (native + XML-style)
- **Task-focused attention loop** (`agent_run_task`): treats each input as a task and keeps running (tool calls + follow-up rounds) until the model responds with `[TASK_COMPLETE]` or a max number of "attention" rounds (default 5). Used in interactive `doctorclaw agent` and optionally in `POST /agent/chat` with `"task_focus": true`.
- **Context + history** with trimming
- **Memory** (SQLite) for store/recall in agent flow
- **Cost tracking** (prompt/completion tokens, export CSV)

### Tools (agent-callable)
- **Shell** (with timeout, danger checks, policy)
- **File**: read, write, stat, list, mkdir, rm, cp, mv, exists, edit, glob, grep
- **HTTP**: browse, http_request (GET/POST, headers)
- **Browser**: browser_open, browser (automation)
- **Git**: clone, pull, commit, git_operations
- **Cron**: add, list, remove, run, cron_runs, cron_update
- **Memory**: memory_store, memory_recall, memory_forget
- **Hardware**: board_info, memory_read, memory_map
- **Notifications**: pushover
- **Delegation**: delegate/forward
- **Composio** (stub)
- **Proxy**: proxy_config
- **Schema**: schema_clean
- **Schedule**: delayed command execution

### Config (config.h / config.c)
- Paths, provider, memory, heartbeat, observability, autonomy, gateway, secrets, cost, browser, http_request, web_search, composio, reliability, agent, storage, cron, model routes, query classification
- **Config load/save**: INI-style file parsing for `[paths]`, `[provider]`, `[gateway]`; TOML-style write
- **Environment variables**: `config_load_from_env()` overlays config from env (see below). `config_env_summary()` lists detected vars (used by `doctor`).

**Environment variables (detected and used):**
| Variable | Effect |
|----------|--------|
| `DOCTORCLAW_WORKSPACE` | Overrides workspace, state, data dirs |
| `DOCTORCLAW_CONFIG` | Config file path (used when no path passed to `config_load`) |
| `DOCTORCLAW_PROVIDER` | Default provider name |
| `DOCTORCLAW_MODEL` | Default model name |
| `DOCTORCLAW_GATEWAY_PORT` | Gateway listen port |
| `DOCTORCLAW_GATEWAY_HOST` | Gateway listen host |
| `OPENROUTER_API_KEY` | API key; sets provider to openrouter if unset |
| `OPENAI_API_KEY` | Fallback API key; sets provider to openai if used |
| `ANTHROPIC_API_KEY` | Fallback API key; sets provider to anthropic if used |
| `GITHUB_TOKEN`, `HOME`, `USER`, `HTTP_PROXY`, `HTTPS_PROXY`, `NO_PROXY` | Detected in env summary (no config overlay) |

### Memory
- **Backends**: SQLite, “lucid”, markdown, postgres, none
- **Store/recall** (parameterized SQL), search_similar (stub), list, delete, clear
- **Chunking**, FTS (SQLite), embeddings (stub), response cache

### Gateway
- HTTP parsing, routes: /, /health, /webhook(s), /telegram, /discord, /slack, **/agent/chat** (real agent: `agent_chat` with LLM + tools)
- WebSocket handshake (SHA-1), frame parse/send, echo loop
- Single-fd send for WS replies

### Channels
- **Telegram**: send_message, typing, chunking
- **Discord**: webhook send, chunking
- **Slack**: send_message (API), typing
- Config-driven (bot_token, webhook_url, etc.)

### Integrations
- **GitHub**: repo search (API)
- **Jira**: search_issues, create_issue (stubs / partial)
- **Notion**: create_page (stub)
- **Slack**: token in manager

### RAG
- **Index**: init, add, add_with_embedding, query, search, save/load
- **Embeddings** (rag_compute_embedding), cosine similarity
- Chunk-based retrieval

### Security
- **Policy**: rules, allow/deny, check
- **Secrets**: store, retrieve, delete, list
- **Audit**: log, get_entries, clear
- **Pairing**: init, approve, revoke, is_paired
- **Sandbox**: config, enable/disable, exec; bubblewrap, firejail, landlock, docker sandbox
- **Detect**: container, sandbox
- **Encryption/decryption** and API key validation

### Observability
- **Metrics**: record, record_with_labels, get, reset
- **Prometheus**: init, export, scrape
- **OTLP** (config)

### Other modules
- **Approval**: request, respond, check, list_pending, clear_expired
- **Tunnel**: ngrok, Tailscale, Cloudflare, custom (start/stop/get_url)
- **Heartbeat**: init, ping, is_alive, since_last
- **Identity**: generate, load/save, sign/verify, get keys
- **Skillforge**: config, scout (GitHub/ClawHub/HuggingFace), evaluate, integrate
- **Runtime**: init/start/stop/pause/resume, WASI (stub), Docker build
- **Llama**: dlopen-based llama.cpp integration (tokens, eval, etc.)
- **Cost**: track, get_total, get_by_provider, export

---

## Recommended Additions (by priority)

**Recently implemented:** Environment variable detection and overlay; INI-style config file parsing; real `/agent/chat` (agent_chat); approval gate for shell/write/rm; cron persist + run in daemon loop; channel add + config save + start; migration generic JSON import into memory.

### High impact, fits current design

1. **Config: real TOML (or INI) parsing** ✓ (INI-style implemented)
   - **Why:** `config_load` only applies path; all other options are defaults. Autonomy, gateway, provider, memory, etc. can’t be set from file.
   - **What:** Add a small TOML/INI parser or use a single-file dependency, and populate `config_t` from the onboard/config file.

2. **Migration: implement `migrate run <source>`**
   - **Why:** Marked “not fully implemented”; useful for adopting Doctor Claw from other agents.
   - **What:** For at least one source (e.g. “generic” JSON export), implement read → normalize → write into Doctor Claw memory/config.

3. **Cron: persist and run tasks** ✓ (daemon runs cron_init + cron_run_pending)
   - **Why:** `cron add` is placeholder; no disk persistence; no scheduler loop in daemon.
   - **What:** Save cron jobs to a file (or reuse config), and in daemon mode (or a dedicated loop) evaluate schedule and execute commands.

4. **Channel: real “add” and “start”**
   - **Why:** Channel config exists in onboarding, but CLI only lists placeholders.
   - **What:** `channel add <type>` to write token/url into channels config; `channel start <name>` to run a long-lived listener (polling or webhook) for that channel.

5. **Agent: wire approval into tool execution**
   - **Why:** Autonomy config has `require_approval` and approval_manager exists, but tools don’t check it.
   - **What:** Before running high-risk tools (e.g. shell, write, rm), call `approval_check` / `approval_request` and block or prompt.

6. **Gateway: real /agent/chat** ✓ (agent_chat, JSON response)
   - **Why:** Currently echoes or returns a fixed response.
   - **What:** Instantiate agent (or reuse a pool), call agent_dispatch/agent_start with body prompt, return model output (and optionally tool results) as JSON.

### Medium impact

7. **Observability: actually export metrics**
   - **Why:** Structures and API exist; Prometheus/OTLP are not clearly wired into agent, gateway, or daemon.
   - **What:** Call `observability_record` (and labels) from key paths (e.g. request count, latency, tool calls, errors) and expose `/metrics` or push to OTLP.

8. **RAG: use in agent**
   - **Why:** RAG index and query exist but aren’t in the agent loop.
   - **What:** Before or after intent classification, run RAG query on user message (or workspace index), inject top chunks into system or user context.

9. **Integrations: implement Jira/Notion**
   - **Why:** Jira search/create and Notion create_page are stubs.
   - **What:** HTTP client + auth (e.g. API token), minimal JSON for Jira/Notion APIs, fill existing structs and return success/failure.

10. **Secrets: use in providers/channels**
    - **Why:** API keys are often env-only; security has secrets store.
    - **What:** Resolve provider/channel credentials from `security_secrets_retrieve` when env not set, and optionally write keys into secrets from `auth` or onboarding.

11. **Logging levels and rotation**
    - **Why:** Only printf and no levels; daemon/long runs can overflow stdout.
    - **What:** Small logging module (level, file optional, optional rotation) and use it instead of raw printf in gateway, agent, daemon.

### Nice to have

12. **Tests**
    - **What:** A few unit tests (e.g. config_init_defaults, memory_store/recall, auth_add/remove, tool_shell_execute with safe command) and one or two integration tests (e.g. `doctorclaw status`, `doctorclaw doctor`).

13. **Doc/help**
    - **What:** README (install, deps, `onboard` → `agent`/`gateway`), and optionally `doctorclaw <command> --help` for subcommands.

14. **Optional C23 sugar**
    - **What:** Use `nullptr` where you want clearer null pointers; use `[[maybe_unused]]` on intentionally unused parameters to reduce warnings.

15. **Daemon: real components**
    - **Why:** Daemon registers components and state; actual gateway/agent/cron aren’t started as managed components.
    - **What:** In `daemon_run`, start gateway (and optionally agent loop, cron loop), register them as components, and drive heartbeat/health from them.

---

## Summary table

| Area           | Current state              | Top recommendation                    |
|----------------|----------------------------|---------------------------------------|
| Config         | Defaults + path only       | TOML/INI parsing → full config load   |
| Agent          | Working, no approval       | Approval gate for risky tools         |
| Gateway        | Echo / placeholder chat    | Real /agent/chat with agent           |
| Cron           | Placeholder add, no run    | Persist + scheduler in daemon         |
| Channels       | List only                  | add + start listeners                 |
| Migration      | Stub                       | Implement at least one source         |
| Observability  | API only                   | Record + export (e.g. /metrics)       |
| RAG            | Standalone                 | Integrate into agent context          |
| Integrations   | Stubs                      | Jira/Notion API calls                 |
| Ops            | printf                     | Logging module + levels               |

If you tell me which area you want to tackle first (e.g. config, cron, or gateway /agent/chat), I can outline concrete steps and code touchpoints next.
