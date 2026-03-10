# Doctor Claw

![Doctor Claw](images/DoctorClaw.jpg)

**Zero overhead. Zero compromise. 100% C.**

Doctor Claw is a **single-binary autonomous agent runtime** written in ISO **C23**. It can talk to LLM providers (OpenRouter/OpenAI/Anthropic), call tools (shell/files/HTTP/memory/cron), run behind an HTTP gateway, and operate as a long-running daemon with a shared in-process job queue.

It’s built for **“give it a task and let it keep working”**: the agent can iterate tool calls + follow-ups until it declares completion.

---

## What this is (and what it isn’t)

- **What it is**
  - **A C23 agent runtime** with a tool-calling loop.
  - **A local gateway** (`doctorclaw gateway`) exposing endpoints like `POST /agent/chat`, webhook receivers, `GET /health`, and `GET /metrics`.
  - **A daemon** (`doctorclaw daemon`) that runs a shared job cache + worker pool and periodically runs cron tasks.
  - **A workspace-scoped tool environment**: file and shell tools are scoped to `workspace_dir` (set from config/env).
  - **A “bring your own provider” client**: OpenRouter/OpenAI/Anthropic keys are detected via env.
  - **Channels**: Telegram (getUpdates poll in daemon), Discord, and Slack webhooks; agent replies are sent back to the channel when configured (`workspace_dir/channels/config.toml`).

- **What it isn’t**
  - A full TOML config system: the config file is currently parsed as a simple INI-style format even though the default filename is `config.toml`.

---

## Install

**Requirements:** Clang (or GCC) with C23, `libcurl`, `sqlite3`. macOS/Linux.

| Dependency | Purpose |
|------------|--------|
| Clang or GCC | C23 compiler (`-std=c23`) |
| libcurl      | HTTP (providers, channels, pentest) |
| sqlite3      | Memory backends, Muninn, jobcache |

**Build and run tests:**

```bash
# Option A: Make (build + run tests)
make

# Option B: Python script (build then runtime tests; good for CI)
python3 scripts/build_and_test.py
# With clean build:
python3 scripts/build_and_test.py --clean
# Build only (no tests):
python3 scripts/build_and_test.py --build-only
# Test only (after building):
python3 scripts/build_and_test.py --test-only
# Open a new terminal window and run build + tests there (all output visible):
python3 scripts/build_and_test.py --open-terminal
```

The script exits 0 on success. Output is unbuffered so you see build and runtime test output as it runs. The test suite includes **build** (all sources and test files compile and link) and **runtime startup tests** for every module: agent, channels, cost, daemon, gateway, hardware, heartbeat, identity, ids, integrations, loop_guard, migration, onboard, pentest, peripherals, providers, rag, runtime_monitor, security, service, skillforge, skills, tunnel, jobworker, plus config, memory, tools, cron, approval, auth, doctor, runtime, util, security_monitor, muninn, log, observability, health, jobcache, and llama. Use `--open-terminal` to open your system terminal (macOS Terminal.app or Linux) and run there so you get a dedicated window with full output. Use `-q` for quiet (errors and final result only).

**Verify install:** `./bin/doctorclaw --help`

---

## Quick start

```bash
# One-time: create a workspace + configs
./bin/doctorclaw onboard

# Set an API key (choose one)
export OPENROUTER_API_KEY="..."     # recommended
# export OPENAI_API_KEY="..."
# export ANTHROPIC_API_KEY="..."

# Run interactive agent
./bin/doctorclaw agent
```

---

## Commands (high level)

| Command | What it does |
|--------|--------------|
| `onboard` | Creates a local workspace, basic configs |
| `agent` | Interactive agent loop (tool-calling + task focus) |
| `gateway` | Starts HTTP server (`/agent/chat`, webhooks, `/health`, `/metrics`) |
| `daemon` | Starts long-running runtime: worker pool + gateway thread + cron loop |
| `doctor` | Diagnostics (config/env/provider/memory checks) |
| `status` | Version + basic health |
| `cron` | Cron management (persisted under `state_dir`) |
| `channel` | Channel scaffolding (Telegram/Discord/Slack) |
| `integrations` | Integration helpers (GitHub/Jira/Notion) |
| `migrate` | Migration helpers (generic JSON import → memory) |
| `auth` | Auth profile scaffolding |
| `verify-task-focus` | Sanity check for task-focus behavior |
| `log` | Logging utilities (`log export <dest_path>`) |
| `test` | Run runtime tests (on demand from CLI or model; tests exit when done) |
| `stop` | Gracefully stop the daemon (sends SIGTERM) |

Run `./bin/doctorclaw --help` for the full list.

---

## Task-focused mode (attention loop)

Doctor Claw supports a task-focused interaction style: instead of responding once, the agent can keep iterating until it emits the marker **`[TASK_COMPLETE]`**, or a max “attention” round limit is reached.

- **CLI:** `doctorclaw agent` is task-oriented.
- **HTTP:** `POST /agent/chat` supports `"task_focus": true`.

---

## Gateway API

```bash
curl -X POST "http://localhost:8080/agent/chat" \
  -H "Content-Type: application/json" \
  -d '{"prompt":"List files in /tmp and summarize them","task_focus":true}'
```

Other common endpoints include:
- `GET /` (index)
- `GET /health`
- `GET /metrics` (Prometheus-style metrics)
- `POST /webhook`, `POST /telegram`, `POST /discord`, `POST /slack`

---

## Logging (file + export)

Doctor Claw logs to **stdout**, and can also write to a **log file**.

- **Default log file (gateway/daemon):** `state_dir/doctorclaw.log`
- **Override:** set `DOCTORCLAW_LOG_FILE`

Export the current log file:

```bash
./bin/doctorclaw log export ./doctorclaw.log
```

---

## Environment variables

The binary **detects these at startup**; config file values are overridden by env when set. All keys below are in the binary's canonical env list; run `doctorclaw providers` to see the full list and which are set.

| Variable | Effect |
|----------|--------|
| **Paths & config** | |
| `DOCTORCLAW_WORKSPACE` | Overrides workspace root (also sets `state_dir` and `data_dir`) |
| `DOCTORCLAW_CONFIG` | Config file path |
| `DOCTORCLAW_PROVIDER` | Default provider (`openrouter`, `openai`, `anthropic`, `gemini`, `llama`, `ollama`, etc.) |
| `DOCTORCLAW_MODEL` | Default model name |
| `DOCTORCLAW_GATEWAY_PORT` / `DOCTORCLAW_GATEWAY_HOST` | Gateway bind |
| **Provider API keys** (first set wins for default key) | |
| `OPENROUTER_API_KEY` | OpenRouter; sets provider to `openrouter` if unset |
| `OPENAI_API_KEY` | OpenAI; sets provider to `openai` if used |
| `ANTHROPIC_API_KEY` | Anthropic; sets provider to `anthropic` if used |
| `GEMINI_API_KEY` | Google Gemini; sets provider to `gemini` if used |
| **Local / optional providers** | |
| `DOCTORCLAW_LLAMA_MODEL` | Path to GGUF model file (when `provider=llama`) |
| `OLLAMA_HOST` | Ollama host (default `localhost:11434`) |
| **Integrations** | |
| `GITHUB_TOKEN` | GitHub API and Copilot; also pre-filled in integrations manager |
| `JIRA_API_TOKEN` | Jira Bearer token; pre-filled in integrations manager |
| `NOTION_API_KEY` | Notion API key; pre-filled in integrations manager |
| **Runtime** | |
| `DOCTORCLAW_SHELL` | Shell for tool execution (default `/bin/sh`) |
| `DOCTORCLAW_LOG_FILE` | Log file path override |
| `DOCTORCLAW_HEALTH_SECRET` | Optional secret for `GET /health` |
| `DOCTORCLAW_RUN_STARTUP_TESTS` | Set to `1` to run runtime tests once at daemon startup |
| `DOCTORCLAW_TEST_BIN` | Path to `doctorclaw_test` binary (for `doctorclaw test` and startup tests) |
| **Proxies** | `HTTP_PROXY`, `HTTPS_PROXY`, `NO_PROXY` (used by HTTP client where applicable) |

Run `doctorclaw doctor` or `doctorclaw providers` to see which are detected.

**Runtime tests:** You can run the test suite on demand via the CLI or from the model (e.g. shell tool): `doctorclaw test`. The test process runs to completion, shuts down gracefully (cron, security_monitor, runtime, etc.), and exits; the caller (CLI or daemon) keeps running. To run tests once on every daemon startup (when not starting from a compile), set `DOCTORCLAW_RUN_STARTUP_TESTS=1`.

---

## Integrations (Jira / Notion)

`src/integrations/integrations.c` contains integration helpers:

- **Jira**
  - Search issues: `/rest/api/3/search` (JQL is URL-encoded)
  - Create issue: `/rest/api/3/issue`
- **Notion**
  - Create page: parses the returned `id` and returns it to the caller

These are minimal implementations intended to be called by higher-level tools/flows.

---

## Muninn (cognitive memory)

Doctor Claw includes a **Muninn-style cognitive memory** backend (C23, SQLite-backed), inspired by [MuninnDB](https://github.com/scrypster/muninndb). Use it as the memory backend for recency- and frequency-aware recall and Hebbian associations.

- **Backend name:** `muninn` — e.g. create with `memory_create("muninn", workspace_dir, &mem)` or set in config.
- **Storage:** `workspace_dir/muninn.db`.
- **Concepts:** `memory_store` writes an **engram** (concept = key, content = value). `memory_recall(key)` runs **ACTIVATE**: full-text match + temporal priority (Ebbinghaus-style decay + access count) and returns the top match; co-returned engrams strengthen associations (Hebbian).
- **Direct API:** `include/muninn.h` — `muninn_init`, `muninn_write`, `muninn_read`, `muninn_activate`, `muninn_reinforce` / `muninn_contradict`, `muninn_soft_delete`, `muninn_list_vaults` for custom flows.

---

## Memory management

**What manages memory:** The **memory** module (`include/memory.h`, `src/memory/memory.c`) is the single place that manages persistent agent/knowledge storage. It is backend-agnostic; you choose a backend by name or config.

- **Backends:** SQLite (`memory.db`), Muninn (`muninn.db`), Lucid, Markdown, Postgres, or none. Storage paths are under `workspace_dir` or `data_dir` from config.
- **Agent use:** The agent calls `agent_memory_init(backend, workspace_dir)` (which uses `memory_init` / `memory_create`), then `memory_store` / `memory_recall` for tool-backed memory. The agent also keeps in-process **chat context** (conversation history) and **RAG** state where applicable.
- **Config:** `workspace_dir`, `state_dir`, and `data_dir` come from config (or `DOCTORCLAW_WORKSPACE`); the memory layer uses them for DB paths and does not allocate process-wide singletons beyond the `memory_t` handles you create and pass around.

---

## Runtime monitoring (alerts and agent awareness)

When the gateway is running, **runtime monitoring** keeps the system aware of security and penetration events and notifies both the user and the model.

- **Events:** Security rejections (rate limit, injection, path traversal), port-probe detection (many connections from one IP in a short window), and errors.
- **User notification:** If channels (Telegram/Discord/Slack) are configured, alerts are sent there (e.g. "⚠️ Doctor Claw: Rate limit — From: 1.2.3.4").
- **Agent awareness:** Recent events are injected into the agent context so the model sees them (e.g. "System runtime alerts: [18:45] Rate limit from 1.2.3.4; …").
- **Port probes:** Each connection is counted per IP; if the same IP opens many connections within 60 seconds, a port-probe event is raised and the user/agent are notified.

Control: `runtime_monitor_set_notify_user(false)` to disable channel alerts; `runtime_monitor_set_agent_aware(false)` to stop injecting alerts into the agent. API: `include/runtime_monitor.h`.

---

## Project layout

```
├── Makefile
├── include/
├── src/
│   ├── agent/
│   ├── config/
│   ├── gateway/
│   ├── integrations/
│   ├── log/
│   ├── memory/
│   ├── muninn/
│   ├── cron/
│   └── ...
├── FEATURES_AND_RECOMMENDATIONS.md
└── WHATS_LEFT.md
```

---

## Build & test

- **Make:** `make` builds the binary and runs the runtime test suite; the build fails if any test fails.
- **Script:** `python3 scripts/build_and_test.py` builds then runs tests with clear **BUILD** and **RUNTIME TESTS** sections; supports `--clean`, `--build-only`, `--test-only`, `--open-terminal` (open system terminal for full output), and `-q`.

---

## License

GPLv3. See [LICENSE](LICENSE).
