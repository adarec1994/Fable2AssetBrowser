# bnk_ui.py

"""
Made by Matthew W, free to use and update.
"""

import os
import sys
import tempfile
import shutil
import subprocess
import threading
import time
import signal
from typing import List, Optional, Tuple
from pathlib import Path
from dearpygui import dearpygui as dpg
import bnk_core as core
import convert as mdl_converter

def _ensure_console():
    return

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
        self.exiting: bool = False
        self.active_procs: List[subprocess.Popen] = []
        self.lock = threading.Lock()
        self.monitor_thread: Optional[threading.Thread] = None

    def start_process_monitor(self):
        def monitor():
            check_count = 0
            while not self.exiting:
                check_count += 1
                if self.cancel_requested:
                    with self.lock:
                        for proc in self.active_procs[:]:
                            try:
                                if os.name == "nt":
                                    subprocess.Popen(
                                        ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                                        shell=True
                                    )
                                else:
                                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                            except Exception:
                                pass
                time.sleep(0.05)
        self.monitor_thread = threading.Thread(target=monitor, daemon=True)
        self.monitor_thread.start()


S = State()
_LAST_SELECTED_ROW_ID = None

_CANCEL_BTN_TAG = "__bnk_cancel_btn"

def _register_proc(proc):
    with S.lock:
        S.active_procs.append(proc)

def _unregister_proc(proc):
    with S.lock:
        if proc in S.active_procs:
            S.active_procs.remove(proc)

def _kill_all_processes():
    with S.lock:
        for proc in S.active_procs[:]:
            try:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], timeout=1)
                else:
                    os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
                    time.sleep(0.1)
                    if proc.poll() is None:
                        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except:
                try:
                    proc.kill()
                except:
                    pass
        S.active_procs.clear()

def _find_towav() -> Optional[str]:
    base_dir = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    towav_dir = base_dir / "tools" / "towav"
    candidates = [
        towav_dir / "towav.exe",
        towav_dir / "towav",
        Path("towav.exe"),
        Path("towav"),
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    for name in ["towav.exe", "towav"]:
        p = shutil.which(name)
        if p:
            return p
    return None

def _poll_cancel():
    try:
        if dpg.does_item_exist(_CANCEL_BTN_TAG) and dpg.is_item_clicked(_CANCEL_BTN_TAG):
            on_cancel_operation()
            return True
    except Exception:
        pass
    return S.cancel_requested

def _run_subprocess_cancelable(cmd: List[str], cwd: Optional[str] = None, timeout: int = 30) -> bool:
    if S.cancel_requested or S.exiting:
        return False

    proc = None
    result = [None]
    thread_exception = [None]

    def run_proc():
        nonlocal proc
        try:
            if os.name == "nt":
                proc = subprocess.Popen(cmd, cwd=cwd, creationflags=0)
            else:
                proc = subprocess.Popen(cmd, cwd=cwd, preexec_fn=os.setsid)
            _register_proc(proc)
            result[0] = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            result[0] = -1
        except Exception as e:
            thread_exception[0] = e
            result[0] = -1
        finally:
            if proc:
                _unregister_proc(proc)

    thread = threading.Thread(target=run_proc, daemon=True)
    thread.start()

    start_time = time.time()
    while proc is None and thread.is_alive():
        if time.time() - start_time > 1:
            break
        time.sleep(0.001)

    while thread.is_alive():
        if _poll_cancel() or S.exiting:
            if proc:
                try:
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], check=False)
                    else:
                        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except Exception:
                    pass
            thread.join(timeout=0.5)
            return False
        dpg.split_frame()
        thread.join(timeout=0.01)

    if thread_exception[0]:
        return False
    return (result[0] == 0)

def _cli_path() -> str:
    base_dir = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    cands = [base_dir / "Fable2Cli.exe", Path("Fable2Cli.exe")]
    for p in cands:
        if p and p.exists():
            return str(p)
    return "Fable2Cli.exe"

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
    bw, bh = 320, 50
    x = max(0, (vw - bw) // 2)
    y = max(0, (vh - bh) // 2)
    dpg.configure_item("select_btn", width=bw, height=bh)
    dpg.set_item_pos("select_btn", (x, y))

_PROG_WIN = "progress_win"
_PROG_BAR = "prog_bar"
_PROG_TEXT = "prog_text"

def on_cancel_operation(*_):
    if S.cancel_requested:
        return
    S.cancel_requested = True
    _kill_all_processes()
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
        dpg.add_button(label="Cancel", callback=on_cancel_operation, width=-1, tag=_CANCEL_BTN_TAG)

def _progress_update(i: int, total: int, rel_path: str):
    if S.cancel_requested or S.exiting:
        return
    _poll_cancel()
    frac = i / total if total else 1.0
    dpg.set_value(_PROG_BAR, frac)
    dpg.set_value(_PROG_TEXT, f"{i}/{total}   {os.path.basename(rel_path)}")
    dpg.split_frame()

def _progress_done():
    if dpg.does_item_exist(_PROG_WIN):
        dpg.delete_item(_PROG_WIN)

def _show_splash():
    dpg.configure_item("splash_group", show=True)
    dpg.configure_item("browser_group", show=False)
    _center_splash_button()

def _show_browser():
    dpg.configure_item("splash_group", show=False)
    dpg.configure_item("browser_group", show=True)

def _safe_mkdir(path: str):
    os.makedirs(path, exist_ok=True)

def _convert_wav_file(wav_path: Path) -> bool:
    if S.cancel_requested or S.exiting:
        return False
    if not wav_path.exists() or wav_path.suffix.lower() != ".wav":
        return False
    try:
        with open(wav_path, "rb") as f:
            data = f.read(min(1024, os.path.getsize(wav_path)))
        if data.startswith(b"xma\x00RIFF"):
            repaired_path = wav_path.with_suffix(".temp.wav")
            with open(wav_path, "rb") as fi, open(repaired_path, "wb") as fo:
                fi.seek(4)
                shutil.copyfileobj(fi, fo)
            src_path = repaired_path
        else:
            src_path = wav_path
            repaired_path = None
        if S.cancel_requested or S.exiting:
            if repaired_path and repaired_path.exists():
                try: repaired_path.unlink()
                except: pass
            return False
        towav = _find_towav()
        if not towav:
            if repaired_path and repaired_path.exists():
                try: repaired_path.unlink()
                except: pass
            return True
        work_dir = src_path.parent
        xma_path = work_dir / (src_path.stem + ".xma")
        shutil.copyfile(src_path, xma_path)
        if S.cancel_requested or S.exiting:
            try: xma_path.unlink()
            except: pass
            if repaired_path and repaired_path.exists():
                try: repaired_path.unlink()
                except: pass
            return False
        ok = _run_subprocess_cancelable([towav, str(xma_path)], cwd=str(work_dir))
        try: xma_path.unlink()
        except: pass
        if S.cancel_requested or S.exiting:
            if repaired_path and repaired_path.exists():
                try: repaired_path.unlink()
                except: pass
            return False
        if ok:
            produced = work_dir / (xma_path.stem + ".wav")
            if not produced.exists():
                produced = work_dir / (src_path.stem + ".wav")
            if produced.exists() and produced != wav_path:
                backup = wav_path.with_suffix(".wav.bak")
                try:
                    wav_path.rename(backup)
                    produced.rename(wav_path)
                    backup.unlink()
                except:
                    pass
        if repaired_path and repaired_path.exists():
            try: repaired_path.unlink()
            except: pass
        return ok
    except Exception:
        return False

def _extract_and_convert_one(bnk_path: str, item: core.BNKItem, base_out_dir: str) -> bool:
    try:
        if S.cancel_requested or S.exiting:
            return False
        _safe_mkdir(base_out_dir)
        dst_path = os.path.join(base_out_dir, item.name)
        _safe_mkdir(os.path.dirname(dst_path))
        if S.cancel_requested or S.exiting:
            return False
        exe = _cli_path()
        ok = _run_subprocess_cancelable([exe, "extract-one", bnk_path, str(item.index), os.path.abspath(dst_path)])
        if S.cancel_requested or S.exiting:
            try:
                if os.path.exists(dst_path):
                    os.remove(dst_path)
            except:
                pass
            return False
        if not ok:
            return False
        if item.name.lower().endswith(".wav"):
            ok = _convert_wav_file(Path(dst_path))
            if S.cancel_requested or S.exiting:
                try:
                    if os.path.exists(dst_path):
                        os.remove(dst_path)
                except:
                    pass
                return False
            return ok
        return True
    except Exception:
        return False

def on_file_selected(sender, app_data, user_data):
    global _LAST_SELECTED_ROW_ID
    if _LAST_SELECTED_ROW_ID is not None and dpg.does_item_exist(_LAST_SELECTED_ROW_ID):
        try: dpg.configure_item(_LAST_SELECTED_ROW_ID, default_value=False)
        except Exception: pass
    try: dpg.configure_item(sender, default_value=True)
    except Exception: pass
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
        _progress_open(1, "Extracting File...")
        dpg.split_frame()
        _progress_update(1, 1, item.name)
        success = _extract_and_convert_one(S.selected_bnk, item, base_out)
        _progress_done()
        if success and not S.cancel_requested:
            _show_completion_box(f"Extraction complete!\n\nOutput folder:\n{os.path.abspath(base_out)}")
        elif S.cancel_requested:
            _show_completion_box("Extraction cancelled by user.")
        S.cancel_requested = False
    except Exception as e:
        _progress_done()
        S.cancel_requested = False
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
        extracted = 0
        for i, it in enumerate(S.files, 1):
            if _poll_cancel() or S.exiting:
                break
            _progress_update(i, total, it.name)
            for _ in range(5):
                dpg.split_frame()
                if _poll_cancel() or S.exiting:
                    break
            if S.cancel_requested or S.exiting:
                break
            if _extract_and_convert_one(S.selected_bnk, it, base_out):
                extracted += 1
            if S.cancel_requested or S.exiting:
                break
        _progress_done()
        if S.cancel_requested:
            _show_completion_box(f"Extraction cancelled.\n{extracted} of {total} files extracted.\n\nOutput folder:\n{os.path.abspath(base_out)}")
        else:
            _show_completion_box(f"Extraction complete!\n{extracted} of {total} files extracted.\n\nOutput folder:\n{os.path.abspath(base_out)}")
        S.cancel_requested = False
    except Exception as e:
        _progress_done()
        S.cancel_requested = False
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
        mdl_path = os.path.join(tmp_dir, item.name)
        exe = _cli_path()
        ok = _run_subprocess_cancelable([exe, "extract-one", S.selected_bnk, str(item.index), os.path.abspath(mdl_path)])
        if not ok:
            _error_box("Failed to extract MDL file")
            return
        out_dir = os.path.join(os.getcwd(), "exported_glb")
        mdl_converter.convert_single_mdl(mdl_path, out_dir)
        _show_completion_box(f"Export to GLB complete!\n\nOutput folder:\n{os.path.abspath(out_dir)}")
    except Exception as e:
        _error_box(f"Failed to export MDL: {e}")

def on_viewport_close():
    S.exiting = True
    S.cancel_requested = True
    _kill_all_processes()
    time.sleep(0.2)
    try:
        dpg.stop_dearpygui()
    finally:
        os._exit(0)

def build_ui():
    _ensure_console()
    S.start_process_monitor()
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
    _kill_all_processes()
    sys.exit(0)

if __name__ == "__main__":
    build_ui()
