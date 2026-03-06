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

## Quick start

**Requirements:** Clang (or GCC) with C23, `libcurl`, `sqlite3`. macOS/Linux.

```bash
make

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

| Variable | Effect |
|----------|--------|
| `OPENROUTER_API_KEY` | Provider key (OpenRouter) |
| `OPENAI_API_KEY` | Provider key (OpenAI) |
| `ANTHROPIC_API_KEY` | Provider key (Anthropic) |
| `DOCTORCLAW_WORKSPACE` | Overrides workspace root (also sets `state_dir` and `data_dir`) |
| `DOCTORCLAW_CONFIG` | Config file path |
| `DOCTORCLAW_PROVIDER` / `DOCTORCLAW_MODEL` | Default provider/model |
| `DOCTORCLAW_GATEWAY_PORT` / `DOCTORCLAW_GATEWAY_HOST` | Gateway bind |
| `DOCTORCLAW_LOG_FILE` | Log file path override |

Run `doctorclaw doctor` to see which are detected.

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
│   ├── cron/
│   └── ...
├── FEATURES_AND_RECOMMENDATIONS.md
└── WHATS_LEFT.md
```

---

## Build & test

```bash
make
```

The test suite is executed as part of `make` and the build fails if any test fails.

---

## License

GPLv3. See [LICENSE](LICENSE).
