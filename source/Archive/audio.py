#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Made by Matthew W, free to use and update.
"""

import argparse, os, shutil, subprocess, sys
from pathlib import Path

DEFAULT_TOWAV_DIR = Path("tools") / "towav"
TOWAV_EXE_NAMES = ["towav.exe", "towav"]

RIFF = b"RIFF"
WAVE = b"WAVE"

def find_towav(towav_dir: Path | None) -> str | None:
    if towav_dir and towav_dir.exists():
        for n in TOWAV_EXE_NAMES:
            cand = towav_dir / n
            if cand.exists():
                return str(cand)
    for n in TOWAV_EXE_NAMES:
        p = shutil.which(n)
        if p:
            return p
    here = Path(__file__).resolve().parent
    for n in TOWAV_EXE_NAMES:
        cand = here / n
        if cand.exists():
            return str(cand)
    return None

def read_u32le(b: bytes, off: int) -> int:
    return int.from_bytes(b[off:off+4], "little", signed=False)

def write_u32le(m: bytearray, off: int, val: int) -> None:
    m[off:off+4] = int(val).to_bytes(4, "little", signed=False)

def has_riff_wave(b: bytes) -> bool:
    return len(b) >= 12 and b[0:4] == RIFF and b[8:12] == WAVE

def starts_with_xma_magic(b: bytes) -> bool:
    return b.startswith(b"xma\x00")

def fix_riff_size(buf: bytearray) -> None:
    if len(buf) >= 8 and buf[0:4] == RIFF:
        size = (len(buf) - 8) & 0xFFFFFFFF
        write_u32le(buf, 4, size)

def parse_chunks(b: bytes, start: int = 12):
    chunks = []
    pos = start
    n = len(b)
    while pos + 8 <= n:
        ck_id = b[pos:pos+4]
        ck_size = read_u32le(b, pos+4)
        data_start = pos + 8
        data_end = data_start + ck_size
        data_end_padded = data_end + (ck_size & 1)
        if data_start > n:
            break
        chunks.append({
            "id": ck_id,
            "pos": pos,
            "size": ck_size,
            "data_start": data_start,
            "data_end": data_end,
            "data_end_padded": data_end_padded,
        })
        if data_end_padded <= pos:
            break
        pos = data_end_padded
    return chunks

def repair_wave(buf: bytes):
    report = {
        "stripped_prefix": 0,
        "fixed_riff_size": False,
        "chunks_fixed": [],
        "note": "",
    }

    i = 0
    while True:
        j = buf.find(RIFF, i)
        if j < 0 or j + 12 > len(buf):
            report["note"] = "No RIFF/WAVE signature found"
            return buf, report
        if buf[j+8:j+12] == WAVE:
            wave_start = j
            break
        i = j + 1

    out = bytearray(buf[wave_start:]) if wave_start != 0 else bytearray(buf)
    if wave_start:
        report["stripped_prefix"] = wave_start

    if len(out) < 12 or out[0:4] != RIFF or out[8:12] != WAVE:
        report["note"] = "Invalid/small RIFF WAVE after prefix strip"
        return bytes(out), report

    # correct RIFF size
    expected_riff_size = max(0, len(out) - 8)
    if read_u32le(out, 4) != expected_riff_size:
        write_u32le(out, 4, expected_riff_size)
        report["fixed_riff_size"] = True

    chunks = parse_chunks(out, 12)
    modified = False
    for ck in chunks:
        if ck["data_end"] > len(out):
            new_size = max(0, len(out) - (ck["pos"] + 8))
            write_u32le(out, ck["pos"]+4, new_size)
            report["chunks_fixed"].append({
                "id": ck["id"].decode("ascii", errors="replace"),
                "old_size": ck["size"],
                "new_size": new_size,
                "reason": "overran EOF",
            })
            modified = True
    if modified:
        expected_riff_size = max(0, len(out) - 8)
        write_u32le(out, 4, expected_riff_size)

    return bytes(out), report

def run_towav(towav_path: str, xma_path: Path, cwd: Path) -> bool:
    cmd = [towav_path, str(xma_path)]
    print("[towav]", " ".join(cmd))
    try:
        subprocess.check_call(cmd, cwd=str(cwd))
        return True
    except subprocess.CalledProcessError as e:
        print(f"[fail] towav (exit {e.returncode})")
        return False

def convert_one(p: Path, towav_dir: Path | None, keep: bool = False) -> None:
    out_pcm = p.with_name(p.stem + "_pcm.wav")
    if out_pcm.exists():
        print(f"[skip] {out_pcm.name} exists")
        return

    data = bytearray(p.read_bytes())
    repaired_path = None

    if starts_with_xma_magic(data) and has_riff_wave(data[4:]):
        print(f"[info] {p.name}: xma\\0-prefixed RIFF -> repair header")
        repaired_path = p.with_name(p.stem + "._repaired.wav")
        body = bytearray(data[4:])
        fix_riff_size(body)
        repaired_path.write_bytes(body)
        src_for_decode = repaired_path
    else:
        if has_riff_wave(data):
            repaired, rep = repair_wave(bytes(data))
            if repaired != bytes(data):
                repaired_path = p.with_name(p.stem + "._repaired.wav")
                repaired_path.write_bytes(repaired)
                print(f"[info] {p.name}: header fixed")
                src_for_decode = repaired_path
            else:
                src_for_decode = p
        else:
            print(f"[skip] {p.name}: not RIFF/WAVE")
            return

    towav_path = find_towav(towav_dir)
    if not towav_path:
        print("[error] towav.exe not found (expected in tools\\towav\\ or PATH)")
        return

    work_dir = (src_for_decode.parent)
    temp_xma = work_dir / (src_for_decode.stem + ".xma")
    shutil.copyfile(src_for_decode, temp_xma)

    ok = run_towav(towav_path, temp_xma, cwd=work_dir)
    try:
        temp_xma.unlink()
    except Exception:
        pass
    if not ok:
        return

    produced_wav = work_dir / (temp_xma.stem + ".wav")
    if produced_wav.exists():
        produced_wav.rename(out_pcm)
        print(f"[OK]   {p.name} -> {out_pcm.name}")
    else:
        alt = work_dir / (src_for_decode.stem + ".wav")
        if alt.exists():
            alt.rename(out_pcm)
            print(f"[OK]   {p.name} -> {out_pcm.name}")
        else:
            print("[warn] towav finished but no WAV found to move")

    if repaired_path and not keep:
        try:
            repaired_path.unlink()
        except Exception:
            pass

def convert_wav_inplace_same_name(path: Path,
                                  towav_dir: Path | None = DEFAULT_TOWAV_DIR,
                                  keep_repaired: bool = False) -> bool:
    p = Path(path)
    if not p.exists():
        print(f"[error] not found: {p}")
        return False
    if p.suffix.lower() != ".wav" :
        print(f"[skip] not a .wav: {p.name}")
        return False

    data = bytearray(p.read_bytes())
    repaired_path = None
    src_for_decode = p

    if starts_with_xma_magic(data) and has_riff_wave(data[4:]):
        repaired_path = p.with_name(p.stem + "._repaired.wav")
        body = bytearray(data[4:])
        fix_riff_size(body)
        repaired_path.write_bytes(body)
        src_for_decode = repaired_path
        print(f"[info] {p.name}: xma\\0-prefixed RIFF -> repair header")
    elif has_riff_wave(data):
        repaired, rep = repair_wave(bytes(data))
        if repaired != bytes(data):
            repaired_path = p.with_name(p.stem + "._repaired.wav")
            repaired_path.write_bytes(repaired)
            src_for_decode = repaired_path
            print(f"[info] {p.name}: header fixed")
    else:
        print(f"[skip] {p.name}: not RIFF/WAVE")
        return False

    towav_path = find_towav(towav_dir)
    if not towav_path:
        print("[error] towav.exe not found (tools\\towav or PATH)")
        return False

    work_dir = src_for_decode.parent
    temp_xma = work_dir / (src_for_decode.stem + ".xma")
    shutil.copyfile(src_for_decode, temp_xma)

    ok = run_towav(towav_path, temp_xma, cwd=work_dir)
    try:
        temp_xma.unlink(missing_ok=True)
    except Exception:
        pass
    if not ok:
        return False

    produced_wav = work_dir / (temp_xma.stem + ".wav")
    if not produced_wav.exists():
        alt = work_dir / (src_for_decode.stem + ".wav")
        if alt.exists():
            produced_wav = alt
        else:
            print("[warn] towav finished but no WAV found")
            return False

    bak = p.with_suffix(".wav.bak")
    try:
        if p.exists():
            p.replace(bak)
    except Exception:
        pass
    produced_wav.replace(p)
    try:
        if repaired_path and not keep_repaired:
            repaired_path.unlink(missing_ok=True)
    except Exception:
        pass
    try:
        if bak.exists():
            bak.unlink()
    except Exception:
        pass

    print(f"[OK]   {p.name} (converted in-place)")
    return True


def convert_all_in_dir_inplace(root: Path) -> None:
    root = Path(root)
    for p in root.rglob("*.wav"):
        try:
            convert_wav_inplace_same_name(p)
        except Exception as e:
            print(f"[error] convert failed for {p}: {e}")


def convert_selected_inplace(file_path: Path) -> bool:
    return convert_wav_inplace_same_name(Path(file_path))

def iter_files(root: Path):
    if root.is_file():
        return [root]
    files = []
    for p in root.rglob("*"):
        if p.is_file():
            files.append(p)
    return files

def main() -> int:
    ap = argparse.ArgumentParser(description="Repair XMA-in-WAV headers and decode via towav.exe (no ffmpeg/vgmstream).")
    ap.add_argument("target", help="File or folder")
    ap.add_argument("--recurse", "-r", action="store_true", help="Recurse into subfolders")
    ap.add_argument("--towav-dir", type=Path, default=DEFAULT_TOWAV_DIR, help="Folder containing towav.exe (default: tools\\towav)")
    ap.add_argument("--keep", action="store_true", help="Keep the ._repaired.wav")
    args = ap.parse_args()

    target = Path(args.target)
    if not target.exists():
        print(f"[error] not found: {target}")
        return 2

    if not find_towav(args.towav_dir):
        print("[warn] towav.exe not found yet; will try PATH at runtime")

    items = iter_files(target) if target.is_dir() else [target]
    if target.is_dir():
        for p in target.rglob("*.wav"):
            try:
                convert_wav_inplace_same_name(p, towav_dir=args.towav_dir, keep_repaired=args.keep)
            except Exception as e:
                print(f"[error] {p.name}: {e}")
        print("[all done]")
        return 0

    print(f"[info] Converting {len(items)} item(s)  |  towav dir: {args.towav_dir if args.towav_dir else '(PATH or script folder)'}")
    for p in items:
        try:
            convert_one(p, args.towav_dir, keep=args.keep)
        except Exception as e:
            print(f"[error] {p.name}: {e}")

    print("[all done]")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
