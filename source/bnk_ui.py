# bnk_ui.py
"""
Requires: dearpygui

Made by Matthew W, free to use and update.
"""

import os
import tempfile
import shutil
import subprocess
from typing import List, Optional, Tuple
from pathlib import Path
from dearpygui import dearpygui as dpg
import bnk_core as core
import convert as mdl_converter
import audio


class State:
    def __init__(self):
        self.root_dir: str = ""
        self.bnk_paths: List[str] = []
        self.bnk_filter: str = ""
        self.selected_bnk: str = ""
        self.files: List[core.BNKItem] = []
        self.selected_file_index: int = -1
        self.hide_tooltips: bool = False
        self.cancel_requested: bool = False


S = State()

_LAST_SELECTED_ROW_ID = None


def _error_box(msg: str):
    dpg.set_value("error_text", str(msg))
    dpg.configure_item("error_modal", show=True)


def _show_completion_box(message: str):
    dpg.set_value("completion_text", message)
    dpg.configure_item("completion_modal", show=True)


def _filtered_bnk_paths() -> List[str]:
    if not S.bnk_filter.strip():
        return S.bnk_paths
    q = S.bnk_filter.lower()
    return [p for p in S.bnk_paths if q in os.path.basename(p).lower()]


def _center_splash_button():
    if not dpg.does_item_exist("select_btn"):
        return
    vw = dpg.get_viewport_client_width()
    vh = dpg.get_viewport_client_height()
    bw, bh = dpg.get_item_rect_size("select_btn")
    x = max(0, int((vw - bw) * 0.5))
    y = max(0, int((vh - bh) * 0.5))
    dpg.set_item_pos("select_btn", [x, y])


_PROG_WIN = "progress_win"
_PROG_BAR = "prog_bar"
_PROG_TEXT = "prog_text"


def on_cancel_operation(*_):
    S.cancel_requested = True
    _progress_done()


def _progress_open(total: int, title: str):
    S.cancel_requested = False
    if dpg.does_item_exist(_PROG_WIN):
        dpg.delete_item(_PROG_WIN)
    with dpg.window(label=title, modal=True, no_close=True, no_resize=False,
                    width=520, height=140, tag=_PROG_WIN):
        dpg.add_text(f"Preparing… 0/{total}", tag=_PROG_TEXT)
        dpg.add_progress_bar(tag=_PROG_BAR, default_value=0.0, width=-1)
        dpg.add_spacer(height=6)
        dpg.add_button(label="Cancel", callback=on_cancel_operation, width=-1)


def _progress_update(i: int, total: int, rel_path: str):
    if S.cancel_requested:
        return
    frac = i / total if total else 1.0
    dpg.set_value(_PROG_BAR, frac)
    dpg.set_value(_PROG_TEXT, f"{i}/{total}   {os.path.basename(rel_path)}")
    dpg.split_frame()


def _progress_done():
    if dpg.does_item_exist(_PROG_WIN):
        dpg.delete_item(_PROG_WIN)


def _center_splash_button():
    if not dpg.does_item_exist("select_btn"):
        return
    vw = dpg.get_viewport_client_width()
    vh = dpg.get_viewport_client_height()
    bw, bh = 320, 50
    x = max(0, (vw - bw) // 2)
    y = max(0, (vh - bh) // 2)
    dpg.configure_item("select_btn", width=bw, height=bh)
    dpg.set_item_pos("select_btn", (x, y))


def _show_splash():
    dpg.configure_item("splash_group", show=True)
    dpg.configure_item("browser_group", show=False)
    _center_splash_button()


def _show_browser():
    dpg.configure_item("splash_group", show=False)
    dpg.configure_item("browser_group", show=True)


def _ffmpeg_path() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "bin", "ffmpeg.exe"),
        shutil.which("ffmpeg.exe"),
        shutil.which("ffmpeg"),
    ]
    for p in candidates:
        if p and os.path.exists(p):
            return p
    return ""


def _safe_mkdir(path: str):
    os.makedirs(path, exist_ok=True)


def _maybe_fix_xma_riff(src_path: str) -> str:
    try:
        with open(src_path, "rb") as f:
            head = f.read(12)
        if head.startswith(b"xma\x00RIFF"):
            tmp_dir = os.path.join(tempfile.gettempdir(), "bnk_convert_cache")
            os.makedirs(tmp_dir, exist_ok=True)
            fixed = os.path.join(tmp_dir, os.path.basename(src_path) + "_stripped.xwav")
            if not (os.path.exists(fixed) and os.path.getmtime(fixed) >= os.path.getmtime(src_path)):
                with open(src_path, "rb") as fi, open(fixed, "wb") as fo:
                    fi.seek(4, 0)
                    shutil.copyfileobj(fi, fo)
            return fixed
    except Exception:
        pass
    return src_path


def _probe_xma_layout(path: str) -> Tuple[int, Optional[str], int]:
    try:
        with open(path, "rb") as f:
            if f.read(4) != b"RIFF":
                return (0, None, 0)
            f.seek(8)
            if f.read(4) != b"WAVE":
                return (0, None, 0)
            while True:
                tag = f.read(4)
                if not tag or len(tag) < 4:
                    break
                size_b = f.read(4)
                if len(size_b) < 4:
                    break
                size = int.from_bytes(size_b, "little", signed=False)
                if tag == b"fmt ":
                    data = f.read(size)
                    if len(data) < 18:
                        return (0, None, 0)
                    wFormatTag = int.from_bytes(data[0:2], "little")
                    nChannels = int.from_bytes(data[2:4], "little")
                    layout_name = "mono" if nChannels == 1 else ("stereo" if nChannels == 2 else None)
                    mask = 0
                    if wFormatTag == 0x0166 and len(data) >= 18 + 36:
                        p = 18
                        p += 2
                        mask = int.from_bytes(data[p:p + 4], "little");
                        p += 4
                    return (nChannels, layout_name, mask)
                else:
                    f.seek(size, 1)
    except Exception:
        pass
    return (0, None, 0)


def _convert_to_pcm(src_path: str, dst_wav_path: str) -> bool:
    ff = _ffmpeg_path()
    if not ff:
        return False

    _safe_mkdir(os.path.dirname(dst_wav_path))

    fixed = _maybe_fix_xma_riff(src_path)
    chans, layout_name, mask = _probe_xma_layout(fixed)
    ch_args: List[str] = []
    if chans == 1:
        ch_args = ["-ac", "1", "-channel_layout", "mono"]
    elif chans == 2:
        ch_args = ["-ac", "2", "-channel_layout", "stereo"]
    elif chans > 2:
        ch_args = ["-ac", str(chans)]
        if mask:
            ch_args += ["-channel_layout", f"0x{mask:x}"]

    def _run(cmd):
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0 or not (os.path.exists(dst_wav_path) and os.path.getsize(dst_wav_path) > 0):
            return False
        return True

    if _run([ff, "-y", "-hide_banner", "-loglevel", "error",
             *ch_args, "-i", src_path, "-acodec", "pcm_s16le", dst_wav_path]):
        return True

    if _run([ff, "-y", "-hide_banner", "-loglevel", "error",
             *ch_args, "-i", fixed, "-acodec", "pcm_s16le", dst_wav_path]):
        return True

    if _run([ff, "-y", "-hide_banner", "-loglevel", "error",
             "-f", "xwma", *ch_args, "-i", fixed, "-acodec", "pcm_s16le", dst_wav_path]):
        return True

    return False


def _extract_and_convert_one(bnk_path: str, item: core.BNKItem, base_out_dir: str) -> bool:
    try:
        _safe_mkdir(base_out_dir)
        dst_path = os.path.join(base_out_dir, item.name)
        _safe_mkdir(os.path.dirname(dst_path))

        core.extract_one(bnk_path, item.index, dst_path)

        if item.name.lower().endswith(".wav"):
            try:
                ok = audio.convert_wav_inplace_same_name(Path(dst_path))
                return ok
            except Exception:
                return False

        return True
    except Exception:
        return False


def on_file_selected(sender, app_data, user_data):
    global _LAST_SELECTED_ROW_ID
    if _LAST_SELECTED_ROW_ID is not None and dpg.does_item_exist(_LAST_SELECTED_ROW_ID):
        try:
            dpg.configure_item(_LAST_SELECTED_ROW_ID, default_value=False)
        except Exception:
            pass
    try:
        dpg.configure_item(sender, default_value=True)
    except Exception:
        pass
    _LAST_SELECTED_ROW_ID = sender

    idx = user_data
    dpg.set_value("sel_file_idx", idx)

    is_mdl = False
    if idx is not None and 0 <= idx < len(S.files):
        item = S.files[idx]
        if item.name.lower().endswith(".mdl"):
            is_mdl = True

    if dpg.does_item_exist("export_mdl_btn"):
        dpg.configure_item("export_mdl_btn", show=is_mdl)


def refresh_bnk_sidebar():
    if dpg.does_item_exist("bnk_list"):
        dpg.delete_item("bnk_list", children_only=True)

    for path in _filtered_bnk_paths():
        label = os.path.basename(path)
        selectable_tag = dpg.add_selectable(
            parent="bnk_list",
            label=label,
            span_columns=True,
            user_data=path,
            callback=on_pick_bnk
        )
        if not S.hide_tooltips:
            with dpg.tooltip(selectable_tag):
                dpg.add_text(path)

    dpg.configure_item("bnk_filter", show=bool(S.bnk_paths))


def refresh_file_table():
    if dpg.does_item_exist("files_table"):
        dpg.delete_item("files_table")

    # [ADDED] reset previous row selection tracker when rebuilding the table
    global _LAST_SELECTED_ROW_ID
    _LAST_SELECTED_ROW_ID = None

    with dpg.table(tag="files_table",
                   parent="right_table_container",
                   header_row=True,
                   resizable=True,
                   policy=dpg.mvTable_SizingStretchProp,
                   borders_innerV=True, borders_outerV=True,
                   borders_outerH=True, borders_innerH=True):
        dpg.add_table_column(label="File")
        dpg.add_table_column(label="Size", width_fixed=True, init_width_or_weight=140)

        for idx, it in enumerate(S.files):
            with dpg.table_row():
                with dpg.group():
                    sel_tag = dpg.add_selectable(label=os.path.basename(it.name),
                                                 span_columns=True,
                                                 user_data=idx,
                                                 callback=on_file_selected)
                    if not S.hide_tooltips:
                        with dpg.tooltip(sel_tag):
                            dpg.add_text(it.name)
                dpg.add_text(str(it.size))

    dpg.set_value("sel_file_idx", -1)


def on_open_folder(*_):
    dpg.show_item("open_folder_dialog")


def open_folder_cb(sender, app_data):
    try:
        sel = app_data.get("file_path_name", "")
        if not sel:
            return
        S.root_dir = sel
        S.bnk_paths = core.find_bnks(sel)
        if not S.bnk_paths:
            _error_box("No .bnk files found in that folder.")
            return

        # Sort by the displayed filename (basename), case-insensitive
        S.bnk_paths.sort(key=lambda p: os.path.basename(p).lower())

        S.selected_bnk = ""
        S.files.clear()

        refresh_bnk_sidebar()
        refresh_file_table()
        dpg.configure_item("export_mdl_btn", show=False)

        if dpg.does_item_exist("status_text"):
            dpg.configure_item("status_text", show=False)
        if dpg.does_item_exist("sel_text"):
            dpg.configure_item("sel_text", show=False)

        _show_browser()
    except Exception as e:
        _error_box(e)


def on_pick_bnk(sender, app_data, user_data):
    if not app_data:
        return
    try:
        S.selected_bnk = str(user_data)
        S.files = core.list_bnk(S.selected_bnk)
        S.files.sort(key=lambda it: it.name.lower())
        refresh_file_table()
        dpg.configure_item("export_mdl_btn", show=False)
    except Exception as e:
        _error_box(e)


def on_filter_change(sender, app_data):
    S.bnk_filter = app_data or ""
    refresh_bnk_sidebar()


def on_toggle_tooltips(sender, app_data):
    S.hide_tooltips = app_data
    refresh_bnk_sidebar()
    if S.selected_bnk:
        refresh_file_table()


def on_extract_selected(*_):
    try:
        idx = dpg.get_value("sel_file_idx")
        if idx is None or idx < 0 or idx >= len(S.files):
            _error_box("No file selected.")
            return
        if not S.selected_bnk:
            _error_box("No BNK selected.")
            return

        item = S.files[idx]
        base_out = os.path.join(os.getcwd(), "extracted")
        _extract_and_convert_one(S.selected_bnk, item, base_out)

        _show_completion_box(f"Extraction complete!\n\nOutput folder:\n{os.path.abspath(base_out)}")

    except Exception as e:
        _error_box(e)


def on_extract_all(*_):
    try:
        if not S.selected_bnk:
            _error_box("No BNK selected.")
            return
        if not S.files:
            _error_box("No files to extract in this BNK.")
            return

        base_out = os.path.join(os.getcwd(), "extracted")
        _safe_mkdir(base_out)

        total = len(S.files)
        _progress_open(total, "Extracting All Files...")
        dpg.split_frame()

        for i, it in enumerate(S.files, 1):
            if S.cancel_requested:
                break
            _progress_update(i, total, it.name)
            _extract_and_convert_one(S.selected_bnk, it, base_out)

        _progress_done()
        if S.cancel_requested:
            _show_completion_box("Extraction cancelled by user.")
        else:
            _show_completion_box(f"Extraction complete!\n\nOutput folder:\n{os.path.abspath(base_out)}")

    except Exception as e:
        _progress_done()
        _error_box(e)


def on_export_selected_mdl(*_):
    try:
        idx = dpg.get_value("sel_file_idx")
        if idx is None or idx < 0 or idx >= len(S.files):
            _error_box("No file selected.")
            return

        item = S.files[idx]
        if not item.name.lower().endswith(".mdl"):
            _error_box("The selected file is not a .mdl file.")
            return

        tmp_dir = os.path.join(tempfile.gettempdir(), "mdl_export_tmp")
        _safe_mkdir(tmp_dir)

        mdl_path = core.extract_one(S.selected_bnk, item.index, os.path.join(tmp_dir, item.name))

        out_dir = os.path.join(os.getcwd(), "exported_glb")
        mdl_converter.convert_single_mdl(mdl_path, out_dir)

        _show_completion_box(f"Export to GLB complete!\n\nOutput folder:\n{os.path.abspath(out_dir)}")

    except Exception as e:
        _error_box(f"Failed to export MDL: {e}")


def on_viewport_close():
    S.cancel_requested = True


def build_ui():
    dpg.create_context()
    dpg.create_viewport(title="BNK Explorer", width=1024, height=640)

    with dpg.theme() as global_theme:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_style(dpg.mvStyleVar_FrameRounding, 5)
            dpg.add_theme_style(dpg.mvStyleVar_WindowRounding, 5)
            dpg.add_theme_style(dpg.mvStyleVar_ChildRounding, 5)
            dpg.add_theme_style(dpg.mvStyleVar_PopupRounding, 5)
            dpg.add_theme_style(dpg.mvStyleVar_GrabRounding, 5)
            dpg.add_theme_style(dpg.mvStyleVar_TabRounding, 5)
            dpg.add_theme_style(dpg.mvStyleVar_ScrollbarRounding, 5)

    dpg.bind_theme(global_theme)

    dpg.setup_dearpygui()

    with dpg.value_registry():
        dpg.add_int_value(tag="sel_file_idx", default_value=-1)

    with dpg.window(tag="main", label="BNK Explorer", no_collapse=True, width=1000, height=600):
        dpg.add_text("No folder loaded", tag="status_text", show=False)
        dpg.add_text("Selected: (none)", tag="sel_text", show=False)

        with dpg.group(tag="splash_group", show=True):
            dpg.add_button(tag="select_btn",
                           label="Select Fable 2 Directory",
                           callback=on_open_folder)

        with dpg.group(tag="browser_group", horizontal=True, show=False):
            with dpg.child_window(tag="left_panel", width=360, height=-1, border=True):
                dpg.add_input_text(tag="bnk_filter", hint="Filter", callback=on_filter_change, width=-1, show=False)
                with dpg.child_window(tag="bnk_list", border=False, width=-1, height=-1):
                    pass

            with dpg.child_window(tag="right_panel", width=-1, height=-1, border=True):
                with dpg.child_window(tag="extract_box", autosize_x=True, height=80, no_scrollbar=True, border=True):
                    with dpg.group(horizontal=True):
                        dpg.add_button(label="Extract Selected", callback=on_extract_selected)
                        dpg.add_button(label="Extract All", callback=on_extract_all)
                        dpg.add_button(label="Export Selected MDL to GLB", callback=on_export_selected_mdl,
                                       tag="export_mdl_btn", show=False)
                    dpg.add_checkbox(label="Hide Paths Tooltip", callback=on_toggle_tooltips)

                with dpg.child_window(tag="right_table_container", width=-1, height=-1, border=False):
                    refresh_file_table()

    with dpg.file_dialog(directory_selector=True,
                         show=False,
                         callback=open_folder_cb,
                         tag="open_folder_dialog",
                         width=720, height=460):
        pass

    with dpg.window(tag="error_modal", modal=True, show=False, no_title_bar=True,
                    no_resize=True, no_move=True, width=640, height=200):
        dpg.add_text("Error", color=(255, 120, 120))
        dpg.add_separator()
        dpg.add_text("", tag="error_text", wrap=600)
        dpg.add_spacer(height=10)
        dpg.add_button(label="Close", width=-1, callback=lambda: dpg.configure_item("error_modal", show=False))

    with dpg.window(tag="completion_modal", modal=True, show=False, no_title_bar=True,
                    no_resize=True, no_move=True, width=640, height=200):
        dpg.add_text("Operation Status", color=(120, 255, 120))
        dpg.add_separator()
        dpg.add_text("", tag="completion_text", wrap=600)
        dpg.add_spacer(height=10)
        dpg.add_button(label="OK", width=-1, callback=lambda: dpg.configure_item("completion_modal", show=False))

    dpg.show_viewport()
    dpg.set_primary_window("main", True)

    _center_splash_button()

    def _on_resize(sender, app_data):
        _center_splash_button()

    dpg.set_viewport_resize_callback(_on_resize)
    dpg.set_exit_callback(on_viewport_close)

    dpg.start_dearpygui()
    dpg.destroy_context()


if __name__ == "__main__":
    build_ui()
