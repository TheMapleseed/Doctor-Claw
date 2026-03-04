# Doctor Claw

**Zero overhead. Zero compromise. 100% C.**

An autonomous AI agent written in ISO C23. No interpreters, no runtimes—just a single binary that talks to OpenRouter/OpenAI, runs tools (shell, files, HTTP, memory, cron), and keeps working on a task until it’s done.

---

## What it does

- **Agent loop** — Chat with an LLM that can call tools (read/write files, run shell, HTTP, store/recall memory, manage cron).
- **Task-focused attention** — Give it a task; it keeps iterating (tool calls + follow-ups) until it responds with `[TASK_COMPLETE]` or hits the round limit. No “one reply and stop” unless you want it.
- **Gateway** — HTTP server with `/agent/chat`, webhooks, health; optional WebSocket. Use `task_focus: true` in the JSON body for the same “run until done” behavior.
- **Config from file + env** — INI-style config file; env vars override (e.g. `OPENROUTER_API_KEY`, `DOCTORCLAW_WORKSPACE`).
- **Channels, cron, migration** — Telegram/Discord/Slack scaffolding; cron persist + run in daemon; generic JSON import into memory.

---

## Quick start

**Requirements:** Clang (or GCC) with C23, libcurl, SQLite3. macOS/Linux.

```bash
# Build (requires C23)
make

# First-time setup: workspace, config, auth
./bin/doctorclaw onboard

# Run the agent (interactive; each message is a task until [TASK_COMPLETE])
export OPENROUTER_API_KEY=your_key   # or OPENAI_API_KEY
./bin/doctorclaw agent
```

In the agent prompt, type a task (e.g. “List files in the current directory and summarize them”). It will use tools and keep going until it marks the task complete or hits the attention round limit.

---

## Commands

| Command | Description |
|--------|-------------|
| `onboard` | Initialize workspace and config |
| `agent` | Interactive agent (task-focused loop) |
| `gateway` | Start HTTP server (e.g. `-p 8080`) |
| `daemon` | Long-running runtime (cron, etc.) |
| `doctor` | Diagnostics (config, env, providers, memory) |
| `status` | Version and health |
| `channel` | Channels: `list`, `add telegram\|discord\|slack`, `start` |
| `cron` | Scheduled tasks |
| `migrate` | Import from other runtimes (e.g. generic JSON → memory) |
| `auth` | Auth profiles (list, add, remove) |
| `verify-task-focus` | Verify task-focus / attention loop behavior |

---

## Task-focused mode (attention loop)

By default, the interactive agent and (optionally) the HTTP API treat each input as a **task**: the agent keeps reasoning and calling tools until it replies with `[TASK_COMPLETE]` and a summary, or until a maximum number of “attention” rounds (default 5).

- **CLI:** Every prompt in `doctorclaw agent` is task-focused.
- **HTTP:** `POST /agent/chat` with body `{"prompt": "Your task", "task_focus": true}`.
- **Check:** `./bin/doctorclaw verify-task-focus` runs a quick sanity check.

---

## Environment variables

| Variable | Effect |
|----------|--------|
| `OPENROUTER_API_KEY` | Primary API key (default provider: openrouter) |
| `OPENAI_API_KEY` | Fallback (provider: openai) |
| `ANTHROPIC_API_KEY` | Fallback (provider: anthropic) |
| `DOCTORCLAW_WORKSPACE` | Override workspace root |
| `DOCTORCLAW_CONFIG` | Config file path |
| `DOCTORCLAW_PROVIDER` / `DOCTORCLAW_MODEL` | Default provider/model |
| `DOCTORCLAW_GATEWAY_PORT` / `DOCTORCLAW_GATEWAY_HOST` | Gateway bind |

Run `doctorclaw doctor` to see which of these (and others) are detected.

---

## Gateway API

```bash
# One-shot chat
curl -X POST http://localhost:8080/agent/chat \
  -H "Content-Type: application/json" \
  -d '{"prompt": "What is 2+2?"}'

# Task-focused (run until [TASK_COMPLETE])
curl -X POST http://localhost:8080/agent/chat \
  -H "Content-Type: application/json" \
  -d '{"prompt": "List files in /tmp and summarize", "task_focus": true}'
```

Other routes: `GET /`, `GET /health`, webhook endpoints for Telegram/Discord/Slack.

---

## Project layout

```
├── Makefile
├── include/          # Headers (config, agent, tools, gateway, …)
├── src/              # Implementation
│   ├── agent/        # Agent loop, task-focus, tools wiring
│   ├── config/       # Load/save, env overlay
│   ├── gateway/      # HTTP + WebSocket
│   ├── tools/        # Shell, file, HTTP, memory, cron, …
│   ├── memory/       # SQLite store/recall
│   ├── cron/         # Scheduler, persist, run
│   └── …
├── FEATURES_AND_RECOMMENDATIONS.md   # Full feature list and roadmap
└── README.md
```

---

## Build

- **C23** — The code targets ISO C23 (`-std=c23`). The Makefile uses Clang by default.
- **Clean:** `make clean && make`
- **Check:** `make check` runs `./bin/doctorclaw --help`

---

## License

See repository license file (if present). Otherwise use at your own discretion.
