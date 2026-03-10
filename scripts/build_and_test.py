#!/usr/bin/env python3
"""
Doctor Claw — build and runtime test runner.

Builds the C23 binary and test binary, then runs the test suite.
Use this script for CI or local install verification.

Usage:
  python3 scripts/build_and_test.py [options]
  ./scripts/build_and_test.py [options]

Options:
  --clean           Run 'make clean' before building.
  --build-only      Build only; do not run tests.
  --test-only       Run tests only (assumes already built).
  --open-terminal   Open system terminal and run build+test there (so you see all output).
  -q, --quiet       Less output (only errors and final result).
"""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path


def _log(msg: str, *, file: object = sys.stdout) -> None:
    """Print and flush so output appears immediately in terminal."""
    print(msg, file=file, flush=True)


def _detect_os() -> tuple[str, str]:
    """Return (os_name, description) for the current system. os_name is 'darwin', 'linux', 'windows', or 'unknown'."""
    raw = platform.system()
    machine = platform.machine()
    if raw == "Darwin":
        return "darwin", f"macOS ({machine})"
    if raw == "Linux":
        return "linux", f"Linux ({machine})"
    if raw == "Windows":
        return "windows", f"Windows ({machine})"
    return "unknown", f"{raw} ({machine})"


def _open_terminal_and_run(repo_root: Path, argv: list[str]) -> int:
    """Launch the system terminal with this script (without --open-terminal). Returns 0 if launched."""
    script = repo_root / "scripts" / "build_and_test.py"
    args = [a for a in argv if a != "--open-terminal"]
    args_str = " ".join(repr(a) for a in args)
    os_name, os_desc = _detect_os()
    _log(f"Detected system: {os_desc}")
    if os_name == "darwin":
        # Write a .command file and open it — Terminal.app will run it and show all output.
        with tempfile.NamedTemporaryFile(
            mode="w",
            suffix=".command",
            delete=False,
            dir=tempfile.gettempdir(),
        ) as f:
            f.write("#!/bin/bash\n")
            f.write(f"cd {repo_root.as_posix()!r}\n")
            f.write(f"python3 {script.as_posix()!r} {args_str}\n")
            f.write('echo ""\n')
            f.write('echo "Press Enter to close."\n')
            f.write("read\n")
        os.chmod(f.name, 0o755)
        try:
            subprocess.run(["open", "-a", "Terminal.app", f.name], check=False)
        except Exception:
            os.unlink(f.name)
            raise
        _log("Opened Terminal.app — build and test output will appear there.")
        return 0
    if os_name == "linux":
        cmd_str = f"cd {repo_root.as_posix()!r} && python3 {script.as_posix()!r} {args_str}"
        cmd_str += ' ; echo "" ; echo "Press Enter to close." ; read'
        for exe in ("gnome-terminal", "xterm", "konsole", "x-terminal-emulator"):
            try:
                if "gnome" in exe:
                    subprocess.run([exe, "--", "bash", "-c", cmd_str], cwd=repo_root, check=False)
                else:
                    subprocess.run([exe, "-e", f"bash -c {cmd_str!r}"], cwd=repo_root, check=False)
                _log(f"Opened {exe} — build and test output will appear there.")
                return 0
            except FileNotFoundError:
                continue
        _log("Could not find a terminal (tried gnome-terminal, xterm, konsole). Run without --open-terminal.", file=sys.stderr)
        return 1
    if os_name == "windows":
        # Windows: start a new cmd window that runs the script
        script_win = (repo_root / "scripts" / "build_and_test.py").as_posix().replace("/", "\\")
        args_win = " ".join(args)  # e.g. --clean
        cmd = f'cd /d {repo_root!s} && python {script_win} {args_win} && echo. && pause'
        try:
            subprocess.run(["cmd", "/c", "start", "cmd", "/k", cmd], cwd=str(repo_root), check=False)
            _log("Opened cmd window — build and test output will appear there.")
            return 0
        except FileNotFoundError:
            _log("Could not start cmd. Run without --open-terminal.", file=sys.stderr)
            return 1
    _log(f"--open-terminal is not supported on {os_desc}. Use macOS, Linux, or Windows.", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build Doctor Claw and run runtime tests.",
        epilog="Default: build then run tests. Exit code 0 = success.",
    )
    parser.add_argument("--clean", action="store_true", help="Run make clean before building")
    parser.add_argument("--build-only", action="store_true", help="Build only; do not run tests")
    parser.add_argument("--test-only", action="store_true", help="Run tests only (skip build)")
    parser.add_argument("--open-terminal", action="store_true",
                        help="Open a new terminal window and run build+test there")
    parser.add_argument("-q", "--quiet", action="store_true", help="Only print errors and final result")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    if args.open_terminal:
        return _open_terminal_and_run(repo_root, sys.argv[1:])

    os.chdir(repo_root)

    def run_captured(cmd: list[str]) -> int:
        """Run with output captured; used for --quiet."""
        r = subprocess.run(cmd, cwd=repo_root, capture_output=True, text=True)
        if r.returncode != 0 or r.stderr:
            if r.stdout:
                sys.stdout.write(r.stdout)
            if r.stderr:
                sys.stderr.write(r.stderr)
        return r.returncode

    def run_streamed(cmd: list[str]) -> int:
        """Run with stdout/stderr streamed to terminal (no capture)."""
        return subprocess.run(cmd, cwd=repo_root).returncode

    # -------- Clean --------
    if args.clean:
        if not args.quiet:
            _log("\n======== CLEAN ========\n")
        code = run_streamed(["make", "clean"]) if not args.quiet else run_captured(["make", "clean"])
        if code != 0:
            return code

    # -------- Build --------
    if not args.test_only:
        if not args.quiet:
            _log("\n======== BUILD ========\n")
        # Ensure obj/ and bin/ exist (make clean removes them; make bin/... does not depend on dirs)
        code = run_streamed(["make", "dirs"]) if not args.quiet else run_captured(["make", "dirs"])
        if code != 0:
            return code
        code = (
            run_streamed(["make", "bin/doctorclaw", "bin/doctorclaw_test"])
            if not args.quiet
            else run_captured(["make", "bin/doctorclaw", "bin/doctorclaw_test"])
        )
        if code != 0:
            if not args.quiet:
                _log("\n[build_and_test] BUILD FAILED.\n")
            return code
        if not args.quiet:
            _log("\n[build_and_test] Build OK.\n")

    if args.build_only:
        if not args.quiet:
            _log("======== DONE (build only) ========\n")
        return 0

    # -------- Runtime tests --------
    test_bin = repo_root / "bin" / "doctorclaw_test"
    if not test_bin.exists():
        _log("[build_and_test] Test binary not found. Run without --test-only first.", file=sys.stderr)
        return 1
    if not args.quiet:
        _log("======== RUNTIME TESTS ========\n")
    code = run_streamed([str(test_bin)]) if not args.quiet else run_captured([str(test_bin)])
    if code != 0:
        if not args.quiet:
            _log("\n======== DONE (TESTS FAILED) ========\n")
        return code
    if not args.quiet:
        _log("\n======== DONE (all passed) ========\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
