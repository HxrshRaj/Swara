"""Thin wrapper around the compiled C++ DSP engine binary.

Approach: subprocess, not a Python/C++ binding (pybind11 etc). Chosen
because the engine is a self-contained streaming CLI that already does
exactly what the API needs (read WAV, run the effect chain in fixed-size
blocks, write WAV, emit a latency-stats JSON) -- there's no shared mutable
state or per-sample call overhead that would justify an in-process binding.
A subprocess call also keeps the API process isolated from a crash or hang
in the native code, and lets the engine be built once and reused unchanged
across platforms (Windows dev machine, Linux container) without a Python
extension build step for each.
"""
import json
import os
import platform
import shutil
import subprocess
import tempfile
import uuid
from dataclasses import dataclass
from typing import Optional


class EngineError(RuntimeError):
    pass


def _default_engine_path() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(here, "..", ".."))
    exe_name = "swara_engine.exe" if platform.system() == "Windows" else "swara_engine"
    return os.path.join(repo_root, "engine", "build", exe_name)


ENGINE_PATH = os.environ.get("SWARA_ENGINE_PATH", _default_engine_path())

VALID_EFFECTS = {"lowpass", "highpass", "delay", "noisegate"}


@dataclass
class ProcessResult:
    output_path: str
    stats: dict


def engine_available() -> bool:
    if not os.path.isfile(ENGINE_PATH):
        return False
    # os.X_OK on Windows only checks existence, which is fine there; on
    # POSIX it correctly checks the executable bit.
    return os.access(ENGINE_PATH, os.X_OK)


def run_engine(
    input_path: str,
    effects: list[str],
    buffer_size: int = 512,
    params: Optional[dict] = None,
    benchmark_sizes: Optional[list[int]] = None,
) -> ProcessResult:
    if not effects:
        raise EngineError("At least one effect must be selected")
    for e in effects:
        if e not in VALID_EFFECTS:
            raise EngineError(f"Unknown effect '{e}'. Valid effects: {sorted(VALID_EFFECTS)}")
    if not os.path.isfile(ENGINE_PATH):
        raise EngineError(
            f"DSP engine binary not found at {ENGINE_PATH}. Build it with "
            f"'make' in the engine/ directory first."
        )

    params = params or {}
    work_dir = tempfile.mkdtemp(prefix="swara_")
    output_path = os.path.join(work_dir, f"{uuid.uuid4().hex}.wav")
    stats_path = os.path.join(work_dir, "stats.json")

    cmd = [
        ENGINE_PATH,
        "--input", input_path,
        "--output", output_path,
        "--effects", ",".join(effects),
        "--buffer-size", str(buffer_size),
        "--stats-out", stats_path,
    ]

    flag_map = {
        "lp_cutoff": "--lp-cutoff",
        "lp_order": "--lp-order",
        "hp_cutoff": "--hp-cutoff",
        "hp_order": "--hp-order",
        "delay_ms": "--delay-ms",
        "delay_feedback": "--delay-feedback",
        "delay_mix": "--delay-mix",
        "gate_frame": "--gate-frame",
        "gate_threshold_db": "--gate-threshold-db",
        "gate_floor_db": "--gate-floor-db",
        "gate_knee_db": "--gate-knee-db",
    }
    for key, flag in flag_map.items():
        if key in params and params[key] is not None:
            cmd += [flag, str(params[key])]

    if benchmark_sizes:
        cmd += ["--benchmark-sizes", ",".join(str(s) for s in benchmark_sizes)]

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired as e:
        shutil.rmtree(work_dir, ignore_errors=True)
        raise EngineError("DSP engine timed out") from e

    if proc.returncode != 0:
        shutil.rmtree(work_dir, ignore_errors=True)
        raise EngineError(f"DSP engine failed (exit {proc.returncode}): {proc.stderr.strip()}")

    try:
        with open(stats_path, "r") as f:
            stats = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError) as e:
        shutil.rmtree(work_dir, ignore_errors=True)
        raise EngineError(f"DSP engine produced no valid stats: {e}") from e

    if not os.path.isfile(output_path):
        shutil.rmtree(work_dir, ignore_errors=True)
        raise EngineError("DSP engine did not produce an output file")

    return ProcessResult(output_path=output_path, stats=stats)
