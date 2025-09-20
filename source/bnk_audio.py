# bnk_audio.py
# Lightweight WAV playback for BNK entries.
# Windows uses built-in winsound; non-Windows falls back to simpleaudio if available.

import os
import tempfile
import threading
from typing import Optional
import bnk_core as core  # uses Fable2Cli to extract a single file

# Backend probes
_BACKEND = None
try:
    import winsound  # type: ignore
    _BACKEND = "winsound"
except Exception:
    try:
        import simpleaudio as sa  # type: ignore
        _BACKEND = "simpleaudio"
    except Exception:
        _BACKEND = None


def _san(s: str) -> str:
    # very simple sanitizer for cache filenames
    return "".join(c if c.isalnum() or c in "._-[]" else "_" for c in s)


class AudioPlayer:
    def __init__(self):
        self._cache_dir = os.path.join(tempfile.gettempdir(), "bnk_audio_cache")
        os.makedirs(self._cache_dir, exist_ok=True)
        self._lock = threading.Lock()
        self._current_path: Optional[str] = None
        self._sa_play_obj = None  # simpleaudio.PlayObject if used

    def _ensure_backend(self):
        # Verify simpleaudio is importable/ready before we try to play.
        if _BACKEND != "simpleaudio":
            print("[ERROR] simpleaudio not available. Install it with: pip install simpleaudio")
            raise RuntimeError("simpleaudio not available")

    def _extract_to_cache(self, bnk_path: str, index: int, rel_name: str) -> str:
        bname = os.path.splitext(os.path.basename(bnk_path))[0]
        # unique but readable cache file name
        out_name = f"{_san(bname)}_{index:05d}_{_san(os.path.basename(rel_name))}"
        out_path = os.path.join(self._cache_dir, out_name)
        core.extract_one(bnk_path, index, out_path)  # will overwrite if exists
        return out_path

    def stop(self):
        with self._lock:
            if _BACKEND == "winsound":
                try:
                    winsound.PlaySound(None, winsound.SND_PURGE)  # type: ignore
                except Exception:
                    pass
            elif _BACKEND == "simpleaudio" and self._sa_play_obj is not None:
                try:
                    self._sa_play_obj.stop()
                except Exception:
                    pass
            self._sa_play_obj = None

    def play_wav_from_bnk(self, bnk_path: str, index: int, rel_name: str):
        """Extracts entry; decodes via ffmpeg if needed; plays via simpleaudio.
           Never raises; returns True on success, False on failure."""
        import traceback
        try:
            print(f"[AUDIO] play_wav_from_bnk called with BNK='{bnk_path}', index={index}, name='{rel_name}'")
            self._ensure_backend()

            cached = self._extract_to_cache(bnk_path, index, rel_name)
            playable_wav = self._decode_to_pcm_wav(cached)

            if not playable_wav:
                print("[ERROR] Audio error: decode returned no playable WAV")
                return False

            print(f"[AUDIO] Attempting simpleaudio playback: '{playable_wav}'")
            with self._lock:
                self.stop()
                try:
                    wave_obj = sa.WaveObject.from_wave_file(playable_wav)
                    self._current_play_obj = wave_obj.play()  # non-blocking
                    print("[AUDIO] simpleaudio play() issued (async).")
                    return True
                except Exception as e:
                    print("[ERROR] Exception during simpleaudio playback:", e)
                    traceback.print_exc()
                    return False
        except Exception as e:
            print("[ERROR] Playback exception:", e)
            traceback.print_exc()
            return False

