import base64
import os
import shutil
import tempfile

from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from .engine_bridge import EngineError, ENGINE_PATH, VALID_EFFECTS, engine_available, run_engine

app = FastAPI(title="Swara DSP API", description="Real-time audio DSP processing backend")

_cors_origins = os.environ.get("SWARA_CORS_ORIGINS", "*")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"] if _cors_origins == "*" else _cors_origins.split(","),
    allow_methods=["*"],
    allow_headers=["*"],
)

MAX_UPLOAD_BYTES = 25 * 1024 * 1024  # 25 MB


class ProcessResponse(BaseModel):
    audio_base64: str
    stats: dict
    effects: list[str]
    buffer_size: int


@app.get("/api/health")
def health():
    return {
        "status": "ok",
        "engine_available": engine_available(),
        "engine_path": ENGINE_PATH,
    }


@app.get("/api/effects")
def list_effects():
    return {
        "effects": sorted(VALID_EFFECTS),
        "defaults": {
            "lp_cutoff": 4000.0, "lp_order": 4,
            "hp_cutoff": 300.0, "hp_order": 4,
            "delay_ms": 300.0, "delay_feedback": 0.35, "delay_mix": 0.35,
            "gate_frame": 1024, "gate_threshold_db": 10.0,
            "gate_floor_db": -26.0, "gate_knee_db": 6.0,
        },
    }


@app.post("/api/process", response_model=ProcessResponse)
async def process_audio(
    file: UploadFile = File(...),
    effects: str = Form(..., description="Comma-separated effect names, e.g. 'lowpass,delay'"),
    buffer_size: int = Form(512),
    lp_cutoff: float | None = Form(None),
    lp_order: int | None = Form(None),
    hp_cutoff: float | None = Form(None),
    hp_order: int | None = Form(None),
    delay_ms: float | None = Form(None),
    delay_feedback: float | None = Form(None),
    delay_mix: float | None = Form(None),
    gate_frame: int | None = Form(None),
    gate_threshold_db: float | None = Form(None),
    gate_floor_db: float | None = Form(None),
    gate_knee_db: float | None = Form(None),
    benchmark: bool = Form(False),
):
    if not file.filename.lower().endswith(".wav"):
        raise HTTPException(400, "Only .wav files are supported")

    effect_list = [e.strip() for e in effects.split(",") if e.strip()]

    work_dir = tempfile.mkdtemp(prefix="swara_upload_")
    input_path = os.path.join(work_dir, "input.wav")
    try:
        size = 0
        with open(input_path, "wb") as f:
            while chunk := await file.read(1024 * 1024):
                size += len(chunk)
                if size > MAX_UPLOAD_BYTES:
                    raise HTTPException(413, "File too large (max 25 MB)")
                f.write(chunk)

        params = {
            "lp_cutoff": lp_cutoff, "lp_order": lp_order,
            "hp_cutoff": hp_cutoff, "hp_order": hp_order,
            "delay_ms": delay_ms, "delay_feedback": delay_feedback, "delay_mix": delay_mix,
            "gate_frame": gate_frame, "gate_threshold_db": gate_threshold_db,
            "gate_floor_db": gate_floor_db, "gate_knee_db": gate_knee_db,
        }

        benchmark_sizes = [64, 128, 256, 512, 1024, 2048] if benchmark else None

        try:
            result = run_engine(
                input_path, effect_list, buffer_size=buffer_size,
                params=params, benchmark_sizes=benchmark_sizes,
            )
        except EngineError as e:
            raise HTTPException(500, str(e))

        with open(result.output_path, "rb") as f:
            audio_bytes = f.read()
        shutil.rmtree(os.path.dirname(result.output_path), ignore_errors=True)

        return ProcessResponse(
            audio_base64=base64.b64encode(audio_bytes).decode("ascii"),
            stats=result.stats,
            effects=effect_list,
            buffer_size=buffer_size,
        )
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)
