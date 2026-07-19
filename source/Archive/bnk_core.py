from __future__ import annotations

import os
from dataclasses import dataclass
from typing import List, Iterable

try:
    from bnk_reader_correct import BNKReader
except ImportError:
    try:
        from bnk_reader_comprehensive import BNKReader
    except ImportError:
        import bnk_reader
        BNKReader = bnk_reader.BNKReader

import bnk_reader


@dataclass
class BNKItem:
    """A simple data structure to pass file info to the UI."""
    index: int
    name: str
    size: int



def list_bnk(bnk_path: str) -> List[BNKItem]:
    """
    Lists the contents of a BNK file by using the BNKReader class.
    """
    items = []
    try:
        with bnk_reader.BNKReader(bnk_path) as reader:
            for i, entry in enumerate(reader.file_entries):
                items.append(BNKItem(index=i, name=entry.name, size=entry.uncompressed_size))
    except Exception as e:
        print(f"Error using bnk_reader on '{os.path.basename(bnk_path)}': {e}")
        return []
    return items


def extract_one(bnk_path: str, index: int, out_path: str) -> str:
    """
    Extracts a single file by its index using the BNKReader class.
    """
    with bnk_reader.BNKReader(bnk_path) as reader:
        if not (0 <= index < len(reader.file_entries)):
            raise IndexError("File index out of range.")

        entry = reader.file_entries[index]
        reader.extract_file(entry.name, out_path)
        return os.path.abspath(out_path)


def extract_all(bnk_path: str, out_dir: str) -> None:
    """
    Extracts all files from the BNK using the BNKReader class.
    """
    with bnk_reader.BNKReader(bnk_path) as reader:
        reader.extract_all(out_dir)


def find_bnks(root: str, exts: Iterable[str] = (".bnk",)) -> List[str]:
    """
    Finds all files with given extensions in a directory. This function is generic.
    """
    root = os.path.abspath(root)
    hits = []
    exts_lower = tuple(e.lower() for e in exts)
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if os.path.splitext(fn)[1].lower() in exts_lower:
                hits.append(os.path.join(dirpath, fn))
    return hits