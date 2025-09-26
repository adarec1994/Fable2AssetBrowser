# bnk_ui.py

"""
Made by Matthew W, free to use and update.
Refactored with nested BNK support.
"""

import os
import sys
import tempfile
import shutil
import subprocess
import threading
import time
import signal
import concurrent.futures
from typing import List, Optional
from pathlib import Path
from threading import Lock
from dearpygui import dearpygui as dpg
import bnk_core as core
import convert as mdl_converter


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
        self.progress_lock = threading.Lock()
        self.nested_bnks: dict = {}  # Store nested BNK structure

    def start_process_monitor(self):
        def monitor():
            while not self.exiting:
                if self.cancel_requested:
                    with self.lock:
                        for proc in self.active_procs[:]:
                            self._kill_process(proc)
                time.sleep(0.05)

        self.monitor_thread = threading.Thread(target=monitor, daemon=True)
        self.monitor_thread.start()

    def _kill_process(self, proc):
        try:
            if os.name == "nt":
                si = subprocess.STARTUPINFO()
                si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                subprocess.Popen(
                    ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                    shell=True, startupinfo=si,
                    creationflags=subprocess.CREATE_NO_WINDOW
                )
            else:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except:
            pass


S = State()
_LAST_SELECTED_ROW_ID = None
_CANCEL_BTN_TAG = "__bnk_cancel_btn"
_PROG_WIN = "progress_win"
_PROG_BAR = "prog_bar"
_PROG_TEXT = "prog_text"


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
                    si = subprocess.STARTUPINFO()
                    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                    subprocess.run(
                        ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                        timeout=1, startupinfo=si,
                        creationflags=subprocess.CREATE_NO_WINDOW
                    )
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
        towav_dir / "towav.exe", towav_dir / "towav",
        Path("towav.exe"), Path("towav"),
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
    except:
        pass
    return S.cancel_requested


def _run_subprocess_cancelable(cmd: List[str], cwd: Optional[str] = None, timeout: int = 30) -> bool:
    if S.cancel_requested or S.exiting:
        return False

    proc = None
    result = [None]

    def run_proc():
        nonlocal proc
        try:
            if os.name == "nt":
                si = subprocess.STARTUPINFO()
                si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                proc = subprocess.Popen(cmd, cwd=cwd, startupinfo=si,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
            else:
                proc = subprocess.Popen(cmd, cwd=cwd, preexec_fn=os.setsid)
            _register_proc(proc)
            result[0] = proc.wait(timeout=timeout)
        except:
            result[0] = -1
        finally:
            if proc:
                _unregister_proc(proc)

    thread = threading.Thread(target=run_proc, daemon=True)
    thread.start()

    while proc is None and thread.is_alive():
        time.sleep(0.001)

    while thread.is_alive():
        if _poll_cancel() or S.exiting:
            if proc:
                S._kill_process(proc)
            thread.join(timeout=0.5)
            return False
        dpg.split_frame()
        thread.join(timeout=0.01)

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


def _progress_update_threadsafe(current: int, total: int, filename: str):
    with S.progress_lock:
        if S.cancel_requested or S.exiting:
            return
        frac = current / total if total else 1.0
        try:
            dpg.set_value(_PROG_BAR, frac)
            dpg.set_value(_PROG_TEXT, f"{current}/{total}   {filename}")
        except:
            pass


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


def is_audio_file(filename: str) -> bool:
    return filename.lower().endswith('.wav')


def is_bnk_file(filename: str) -> bool:
    return filename.lower().endswith('.bnk')


def _extract_nested_bnks(bnk_path: str, temp_dir: str) -> List[str]:
    """Extract and find nested BNK files"""
    nested_bnks = []
    try:
        files = core.list_bnk(bnk_path)
        for item in files:
            if is_bnk_file(item.name):
                nested_path = os.path.join(temp_dir, item.name)
                _safe_mkdir(os.path.dirname(nested_path))
                exe = _cli_path()
                ok = _run_subprocess_cancelable(
                    [exe, "extract-one", bnk_path, str(item.index), os.path.abspath(nested_path)]
                )
                if ok and os.path.exists(nested_path):
                    nested_bnks.append(nested_path)
                    # Recursively check for more nested BNKs
                    deeper_bnks = _extract_nested_bnks(nested_path, temp_dir)
                    nested_bnks.extend(deeper_bnks)
    except:
        pass
    return nested_bnks


def _convert_wav_file(wav_path: Path) -> bool:
    if S.cancel_requested or S.exiting:
        return False
    if not wav_path.exists() or wav_path.suffix.lower() != ".wav":
        return False
    try:
        with open(wav_path, "rb") as f:
            data = f.read(min(1024, os.path.getsize(wav_path)))

        repaired_path = None
        if data.startswith(b"xma\x00RIFF"):
            repaired_path = wav_path.with_suffix(".temp.wav")
            with open(wav_path, "rb") as fi, open(repaired_path, "wb") as fo:
                fi.seek(4)
                shutil.copyfileobj(fi, fo)
            src_path = repaired_path
        else:
            src_path = wav_path

        if S.cancel_requested or S.exiting:
            if repaired_path and repaired_path.exists():
                repaired_path.unlink()
            return False

        towav = _find_towav()
        if not towav:
            if repaired_path and repaired_path.exists():
                repaired_path.unlink()
            return True

        work_dir = src_path.parent
        xma_path = work_dir / (src_path.stem + ".xma")
        shutil.copyfile(src_path, xma_path)

        if S.cancel_requested or S.exiting:
            xma_path.unlink()
            if repaired_path and repaired_path.exists():
                repaired_path.unlink()
            return False

        ok = _run_subprocess_cancelable([towav, str(xma_path)], cwd=str(work_dir))
        xma_path.unlink()

        if ok:
            produced = work_dir / (xma_path.stem + ".wav")
            if not produced.exists():
                produced = work_dir / (src_path.stem + ".wav")
            if produced.exists() and produced != wav_path:
                backup = wav_path.with_suffix(".wav.bak")
                wav_path.rename(backup)
                produced.rename(wav_path)
                backup.unlink()

        if repaired_path and repaired_path.exists():
            repaired_path.unlink()
        return ok
    except:
        return False


def _extract_file(bnk_path: str, item: core.BNKItem, base_out_dir: str, convert_audio: bool = True) -> bool:
    try:
        if S.cancel_requested or S.exiting:
            return False

        _safe_mkdir(base_out_dir)
        dst_path = os.path.join(base_out_dir, item.name)
        _safe_mkdir(os.path.dirname(dst_path))

        exe = _cli_path()
        ok = _run_subprocess_cancelable(
            [exe, "extract-one", bnk_path, str(item.index), os.path.abspath(dst_path)]
        )

        if not ok or S.cancel_requested or S.exiting:
            if os.path.exists(dst_path):
                os.remove(dst_path)
            return False

        # Convert audio if needed
        if convert_audio and is_audio_file(item.name):
            ok = _convert_wav_file(Path(dst_path))
            if not ok and os.path.exists(dst_path):
                os.remove(dst_path)

        return ok
    except:
        return False


def on_file_selected(sender, app_data, user_data):
    global _LAST_SELECTED_ROW_ID
    if _LAST_SELECTED_ROW_ID and dpg.does_item_exist(_LAST_SELECTED_ROW_ID):
        dpg.configure_item(_LAST_SELECTED_ROW_ID, default_value=False)
    dpg.configure_item(sender, default_value=True)
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
        # Show indentation for nested BNKs
        indent = ""
        if path in S.nested_bnks.values():
            for parent, nested in S.nested_bnks.items():
                if path == nested:
                    indent = "  → "
                    break

        selectable_tag = dpg.add_selectable(
            parent="bnk_list",
            label=indent + label,
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

    with dpg.table(tag="files_table", parent="right_table_container",
                   header_row=True, resizable=True,
                   policy=dpg.mvTable_SizingStretchProp,
                   borders_innerV=True, borders_outerV=True,
                   borders_outerH=True, borders_innerH=True):
        dpg.add_table_column(label="File")
        dpg.add_table_column(label="Size", width_fixed=True, init_width_or_weight=140)

        for idx, it in enumerate(S.files):
            with dpg.table_row():
                with dpg.group():
                    sel_tag = dpg.add_selectable(
                        label=os.path.basename(it.name),
                        span_columns=True,
                        user_data=idx,
                        callback=on_file_selected
                    )
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

        # Check for nested BNKs
        temp_dir = os.path.join(tempfile.gettempdir(), "nested_bnk_scan")
        _safe_mkdir(temp_dir)

        S.nested_bnks.clear()
        for bnk_path in S.bnk_paths[:]:
            nested = _extract_nested_bnks(bnk_path, temp_dir)
            for nested_bnk in nested:
                S.nested_bnks[bnk_path] = nested_bnk
                if nested_bnk not in S.bnk_paths:
                    S.bnk_paths.append(nested_bnk)

        S.bnk_paths.sort(key=lambda p: os.path.basename(p).lower())
        S.selected_bnk = ""
        S.files.clear()

        refresh_bnk_sidebar()
        refresh_file_table()
        dpg.configure_item("export_mdl_btn", show=False)
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
        _progress_update_threadsafe(1, 1, item.name)

        success = _extract_file(S.selected_bnk, item, base_out)
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

        audio_files = [f for f in S.files if is_audio_file(f.name)]
        non_audio_files = [f for f in S.files if not is_audio_file(f.name)]

        _progress_open(total, "Extracting All Files...")
        dpg.split_frame()

        extracted = 0
        extracted_lock = Lock()
        failed_files = []

        def extract_parallel(item, needs_conversion):
            nonlocal extracted
            if S.cancel_requested or S.exiting:
                return False

            success = _extract_file(S.selected_bnk, item, base_out, needs_conversion)

            with extracted_lock:
                if success:
                    extracted += 1
                else:
                    failed_files.append(item.name)
                current = extracted + len(failed_files)
                _progress_update_threadsafe(current, total, os.path.basename(item.name))

            return success

        # Process non-audio files first
        if non_audio_files and not S.cancel_requested:
            with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 2)) as executor:
                list(executor.map(lambda i: extract_parallel(i, False), non_audio_files))

        # Then process audio files
        if audio_files and not S.cancel_requested:
            with concurrent.futures.ThreadPoolExecutor(max_workers=min(4, os.cpu_count() // 2 or 1)) as executor:
                list(executor.map(lambda i: extract_parallel(i, True), audio_files))

        _progress_done()

        msg = f"Extraction {'cancelled' if S.cancel_requested else 'complete'}!\n{extracted} of {total} files extracted successfully."

        if failed_files:
            msg += f"\n\nFailed to extract {len(failed_files)} file(s)."
            if len(failed_files) <= 5:
                msg += "\nFailed files:\n" + "\n".join(f"  • {f}" for f in failed_files[:5])

        msg += f"\n\nOutput folder:\n{os.path.abspath(base_out)}"
        _show_completion_box(msg)

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
        ok = _run_subprocess_cancelable(
            [exe, "extract-one", S.selected_bnk, str(item.index), os.path.abspath(mdl_path)]
        )

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
        with dpg.group(tag="splash_group", show=True):
            dpg.add_button(tag="select_btn", label="Select Fable 2 Directory", callback=on_open_folder)

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

    with dpg.file_dialog(directory_selector=True, show=False, callback=open_folder_cb,
                         tag="open_folder_dialog", width=720, height=460):
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

    dpg.set_viewport_resize_callback(lambda *_: _center_splash_button())
    dpg.set_exit_callback(on_viewport_close)
    dpg.start_dearpygui()
    dpg.destroy_context()
    _kill_all_processes()
    sys.exit(0)


if __name__ == "__main__":
    build_ui()