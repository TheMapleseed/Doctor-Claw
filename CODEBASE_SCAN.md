# Doctor Claw — Codebase Scan

**Scan date:** 2025-03-03  
**Scope:** All `.c` and `.h` files under `src/` and `include/`.

---

## 1. Overview

| Metric | Value |
|--------|--------|
| C source files | 34 |
| Header files | 30 |
| Total C/H files | 64 |
| Build | ✅ `make` succeeds (C23, Clang) |

**Structure:** Single binary `bin/doctorclaw`; modular layout with one directory per subsystem under `src/` (agent, config, gateway, tools, memory, cron, channels, migration, etc.) and matching headers in `include/`.

**Clean build:** The project builds with `-Wall -Wextra -Wpedantic` with zero warnings (unused parameters/variables/functions have been addressed via `(void)x`, removal, or `__attribute__((unused))` where appropriate).

---

## 2. C23 and Headers

- **C23 check:** `include/c23_check.h` enforces `__STDC_VERSION__ >= 202311L`. It is included by all public headers (config, agent, tools, etc.); `.c` files get it transitively via their `#include "module.h"`. No source file includes `c23_check.h` directly; all go through the module header. ✅
- **Include guards:** Headers use `#ifndef DOCTORCLAW_*_H` / `#define` / `#endif`. ✅

---

## 3. Safety and Best Practices

### 3.1 Buffer and string handling

- **`strcpy`:** Used in:
  - `src/tools/tools.c`: fixed string literals (`"JPEG"`, `"PNG"`, etc.) into a buffer → safe.
  - `src/skillforge/skillforge.c`: `strcpy(cfg->output_dir, "./skills")` (literal) and `strcpy(integrator->output_dir, config->output_dir)` where `config->output_dir` is `char [SKILLFORGE_MAX_OUTPUT_DIR]` (256) and destination is the same size → safe.
- **`fgets`:** All uses are bounded (e.g. `fgets(line, sizeof(line), f)`). No unbounded reads. ✅
- **`snprintf`:** Used for bounded formatting throughout; no raw `sprintf` with unbounded destinations found. ✅

### 3.2 SQL (memory.c)

- **Good:** `memory_store` and `memory_recall` use **prepared statements** and `sqlite3_bind_*` (no user input in raw SQL). ✅
- **Fixed:** `memory_delete(mem, key)` previously built SQL with `snprintf(..., "DELETE ... key = '%s'", key)` (SQL injection risk). It now uses a prepared statement and `sqlite3_bind_text` for `key`, matching store/recall. ✅

### 3.3 Shell and command execution

- **tools.c:** Shell execution uses `run_with_timeout` with a bounded `command` buffer (`SHELL_MAX_INPUT`), length checks, and `check_dangerous_command` / `check_shell_policy`. ✅
- **memory.c (Postgres path):** `run_psql_query` escapes single quotes in `sql` before passing to `popen(psql ...)`. Escaping is minimal; consider prepared statements or parameterized usage if that path is used with user input. ⚠️

---

## 4. Memory (malloc/free)

- **Allocation:** `malloc`/`calloc`/`realloc` appear in: memory.c, skillforge.c, providers.c, integrations.c, migration.c, tools.c, agent.c, util.c.
- **Pairing:** No systematic leak check was run. Visual inspection shows matching `free` in the same modules (e.g. skillforge_config_free, skillforge_free). For a full audit, run under Valgrind or ASan (e.g. `make CFLAGS="-fsanitize=address"` and exercise agent, gateway, tools).

---

## 5. Format strings and printf

- No `printf(format)` or `fprintf(stream, format)` with user-controlled `format` found. Format strings are literals or built from literals. ✅
- `cost.c` uses `PRIu64` with `<inttypes.h>` for `uint64_t` → correct. ✅

---

## 6. Other observations

- **TODO/FIXME:** Only a `DEBUG`-related `fprintf` in observability.c; no open TODO/FIXME/HACK markers. ✅
- **Agent tool dispatch:** `agent_execute_tool_by_name` passes parsed args (path, url, key, value, query) to `tools_execute`; buffers are fixed size and sscanf is bounded. ✅
- **Gateway:** Request body and response body use fixed-size buffers (`MAX_REQUEST_SIZE`, `MAX_RESPONSE_SIZE`); JSON prompt parsing is simple and bounded. ✅
- **Warnings:** Build reports several `-Wunused` (variables, parameters, functions). Consider cleaning or annotating with `(void)x` / `__attribute__((unused))` to keep a clean build. ⚠️

---

## 7. Summary

| Area | Status | Notes |
|------|--------|--------|
| Build | ✅ | C23, single binary |
| String/buffer safety | ✅ | Bounded copies, no unsafe strcpy on user input |
| SQL (SQLite store/recall) | ✅ | Prepared statements |
| SQL (memory_delete) | ✅ | Fixed: prepared statement + bind |
| Shell execution | ✅ | Bounded, checked |
| Format strings | ✅ | No user-controlled format |
| Memory (leaks) | — | Not fully audited; suggest ASan/Valgrind |
| Compiler warnings | ⚠️ | Several unused; optional cleanup |

**Done:** `memory_delete` was updated to use a prepared statement and `sqlite3_bind_text` for `key`.
