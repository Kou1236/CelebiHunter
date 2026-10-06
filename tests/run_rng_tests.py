"""Run the RNG v1.2.0 host regressions with a C99 compiler.

Use CC (or --cc) for gcc/clang, or ZIG (or --zig) for `zig cc`.
The copied C scenarios and Python scalar/native-fixture assertions keep their
original expectations. Native memory and SDK endpoints are mocked; these host
checks do not establish hardware prediction accuracy.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
RNG = ROOT / "rng"
TESTS = ROOT / "tests" / "rng"
COMMON_SOURCES = (
    "raw_runtime.c",
    "raw_service.c",
    "raw_hud.c",
    "query_completion.c",
    "input-plan/manual_input_plan.c",
    "prediction/manual_prediction.c",
    "candidate-query/query.c",
    "candidate-query/terminal_eval.c",
    "controller/manual_controller.c",
)
WAITING_SOURCES = COMMON_SOURCES + (
    "job_mailbox.c",
    "source-observer/aligned_waiting.c",
    "source-observer/source_observer.c",
    "environment.c",
    "environment_session.c",
    "source_capture.c",
    "platform/sampler.c",
    "platform/scene.c",
    "platform/source_hash.c",
    "result/result_gate.c",
    "audio/paused_audio.c",
)


def compiler_command(cc: str | None, zig: str | None) -> list[str]:
    if cc:
        if Path(cc).is_file():
            command = [cc]
        else:
            command = shlex.split(cc, posix=os.name != "nt")
            if os.name == "nt":
                command = [part.strip('"') for part in command]
        if not command:
            raise ValueError("CC must name a C99 compiler.")
        if Path(command[0]).stem.lower() == "zig" and len(command) == 1:
            command.append("cc")
        return command
    if zig:
        return [zig, "cc"]
    for name in ("cc", "gcc", "clang"):
        found = shutil.which(name)
        if found:
            return [found]
    found = shutil.which("zig")
    if found:
        return [found, "cc"]
    raise ValueError("No C99 compiler found. Set CC or ZIG, or use --cc/--zig.")


def run(command: list[str], env: dict[str, str]) -> None:
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def run_tests(compiler: list[str], build: Path) -> None:
    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    if Path(compiler[0]).stem.lower() == "zig":
        env.setdefault("ZIG_GLOBAL_CACHE_DIR", str(build / "zig-global"))
        env.setdefault("ZIG_LOCAL_CACHE_DIR", str(build / "zig-local"))
    flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror"]
    includes = ["-I" + str(RNG), "-I" + str(RNG / "prediction")]
    suffix = ".exe" if os.name == "nt" else ""
    scenarios = (
        ("test_runtime_candidate", COMMON_SOURCES, []),
        ("test_long_plan", COMMON_SOURCES + ("environment_session.c",), []),
        (
            "test_waiting_device",
            WAITING_SOURCES,
            [
                "-Wno-unused-variable",
                "-Wno-misleading-indentation",
                "-I" + str(TESTS / "mock"),
            ],
        ),
    )
    for name, sources, extra in scenarios:
        print(f"RNG: {name}", flush=True)
        executable = build / (name + suffix)
        command = compiler + flags + includes + extra
        command += [str(RNG / source) for source in sources]
        command += [str(TESTS / (name + ".c")), "-o", str(executable)]
        run(command, env)
        run([str(executable)], env)

    if os.name == "nt":
        library = build / "query.dll"
        shared_flags = ["-shared"]
    elif sys.platform == "darwin":
        library = build / "libquery.dylib"
        shared_flags = ["-dynamiclib", "-fPIC"]
    else:
        library = build / "libquery.so"
        shared_flags = ["-shared", "-fPIC"]
    command = compiler + flags + includes + shared_flags
    command += [
        str(RNG / "candidate-query/terminal_eval.c"),
        str(RNG / "candidate-query/query.c"),
        "-o",
        str(library),
    ]
    run(command, env)
    for name in ("check_terminal.py", "check_query.py"):
        print(f"RNG: {name}", flush=True)
        run([sys.executable, str(TESTS / name), str(library)], env)
    print("RNG host tests passed (5 regression groups).", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="C99 compiler command (also accepted in CC)")
    parser.add_argument("--zig", help="Zig executable path (also accepted in ZIG)")
    parser.add_argument("--build-dir", type=Path, help="Keep binaries in this directory")
    args = parser.parse_args()
    cc = args.cc
    zig = args.zig
    if not cc and not zig:
        cc = os.environ.get("CC")
        zig = os.environ.get("ZIG")
    try:
        compiler = compiler_command(cc, zig)
        if args.build_dir:
            run_tests(compiler, args.build_dir.resolve())
        else:
            with tempfile.TemporaryDirectory(prefix="celebihunter-rng-tests-") as temporary:
                run_tests(compiler, Path(temporary))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"RNG host tests failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
