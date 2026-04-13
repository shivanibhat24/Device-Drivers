"""
Whisper-based voice input for chess-arm-tournament.
Records from microphone and transcribes chess move commands.
"""
import threading
import tempfile
import numpy as np
import scipy.io.wavfile as wav
import sounddevice as sd
import whisper

_model = None
_model_lock = threading.Lock()


def load_model(size: str = "base"):
    """Load Whisper model (call once at startup)."""
    global _model
    with _model_lock:
        if _model is None:
            print(f"[Whisper] Loading '{size}' model — first run may download it...")
            _model = whisper.load_model(size)
            print("[Whisper] Model ready ✓")


def record_audio(duration: float = 3.0, sample_rate: int = 16000) -> np.ndarray:
    """Record from default microphone."""
    print(f"[🎤] Listening for {duration}s...")
    audio = sd.rec(
        int(duration * sample_rate),
        samplerate=sample_rate,
        channels=1,
        dtype="float32"
    )
    sd.wait()
    print("[🎤] Recording done")
    return audio.flatten()


def transcribe(audio: np.ndarray, sample_rate: int = 16000) -> str:
    """Transcribe a numpy audio array → text."""
    global _model
    if _model is None:
        load_model()
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        wav.write(f.name, sample_rate, (audio * 32767).astype(np.int16))
        result = _model.transcribe(f.name, language="en", fp16=False)
        text = result["text"].strip()
        print(f"[Whisper] Transcribed: '{text}'")
        return text


def listen_and_transcribe(duration: float = 3.0) -> str:
    """One-shot convenience: record then transcribe."""
    audio = record_audio(duration)
    return transcribe(audio)


def listen_async(callback, duration: float = 3.0):
    """
    Non-blocking: record in background thread, call callback(text) when done.
    Useful so the GUI doesn't freeze.
    """
    def _worker():
        text = listen_and_transcribe(duration)
        callback(text)
    threading.Thread(target=_worker, daemon=True).start()
