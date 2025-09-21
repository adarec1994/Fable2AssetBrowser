# bnk_core.py

"""
Made by Matthew W, free to use and update.
"""

import os
import subprocess
import shutil
import sys
from dataclasses import dataclass
from typing import List, Iterable

CLI_NAME = "Fable2Cli.exe"


@dataclass
class BNKItem:
    index: int
    size: int
    name: str


def _resolve_cli() -> str:
    if hasattr(sys, '_MEIPASS'):
        bundled_path = os.path.join(sys._MEIPASS, CLI_NAME)
        if os.path.isfile(bundled_path):
            return bundled_path

    here = os.path.abspath(os.path.dirname(__file__))
    local = os.path.join(here, CLI_NAME)
    if os.path.isfile(local):
        return local

    found = shutil.which(CLI_NAME)
    if found:
        return found
    raise FileNotFoundError(f"{CLI_NAME} not found in bundle, next to script, or in PATH")


def _run_cli(args: List[str]) -> subprocess.CompletedProcess:
    cli = _resolve_cli()

    creation_flags = 0
    if sys.platform == "win32":
        creation_flags = subprocess.CREATE_NO_WINDOW

    p = subprocess.run([cli] + args, capture_output=True, text=True, encoding="utf-8", creationflags=creation_flags)

    if p.returncode != 0:
        err = p.stderr.strip() or f"{CLI_NAME} failed: {args}"
        raise RuntimeError(err)
    return p


def list_bnk(bnk_path: str) -> List[BNKItem]:
    p = _run_cli(["list", bnk_path])
    out: List[BNKItem] = []
    for line in p.stdout.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t", 3)
        if len(parts) < 3:
            continue
        out.append(BNKItem(index=int(parts[0]), size=int(parts[1]), name=parts[2]))
    return out


def extract_one(bnk_path: str, index: int, out_path: str) -> str:
    out_path = os.path.abspath(out_path)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    _run_cli(["extract-one", bnk_path, str(index), out_path])
    return out_path


def extract_all(bnk_path: str, out_dir: str) -> None:
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    _run_cli(["extract-all", bnk_path, out_dir])


def find_bnks(root: str, exts: Iterable[str] = (".bnk",)) -> List[str]:
    root = os.path.abspath(root)
    hits: List[str] = []
    exts_lc = tuple(e.lower() for e in exts)
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if os.path.splitext(fn)[1].lower() in exts_lc:
                hits.append(os.path.join(dirpath, fn))
    return hits