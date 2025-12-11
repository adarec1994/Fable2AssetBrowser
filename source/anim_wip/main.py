import sys
import subprocess
import os
import struct

try:
    import dearpygui.dearpygui as dpg
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "dearpygui"])
    import dearpygui.dearpygui as dpg

CONFIG_FILE = "parser_config.txt"


def load_last_path():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, 'r') as f:
                return f.read().strip()
        except:
            pass
    return ""


def save_last_path(path):
    try:
        with open(CONFIG_FILE, 'w') as f:
            f.write(path)
    except:
        pass


class TocStringParser:
    def __init__(self):
        self.strings = []
        self.status = "Ready"
        self.fps_marker = struct.pack('>I', 0x41F00000)

    def load_file(self, filepath):
        self.strings = []
        self.status = "Loading..."
        filepath = filepath.strip('"').strip()

        if not os.path.exists(filepath):
            self.status = "Error: File not found."
            return

        save_last_path(filepath)

        try:
            with open(filepath, 'rb') as f:
                data = f.read()

            if data[:8] != b'AnimBank':
                self.status = "Error: Invalid Magic (Expected 'AnimBank')"
                return

            entry_table_start = len(data)
            found_marker = False
            for i in range(28, len(data) - 4):
                if data[i:i + 4] == self.fps_marker:
                    entry_table_start = i - 8
                    found_marker = True
                    break

            if not found_marker:
                self.status = "Warning: FPS Marker not found."

            start_pos = 0x1A
            if start_pos >= entry_table_start:
                self.status = "Error: File too small."
                return

            string_block = data[start_pos:entry_table_start]
            raw_entries = string_block.split(b'\x00')

            for s in raw_entries:
                if len(s) > 0:
                    try:
                        decoded = s.decode('utf-8', errors='ignore')
                        self.strings.append(decoded)
                    except:
                        pass

            self.status = f"Success: Parsed {len(self.strings)} strings."

        except Exception as e:
            self.status = f"Exception: {str(e)}"


parser = TocStringParser()


def add_row_to_table(table_tag, count, real_id, text, color=None):
    with dpg.table_row(parent=table_tag):
        dpg.add_text(str(count))
        dpg.add_text(str(real_id))
        if color:
            dpg.add_text(text, color=color)
        else:
            dpg.add_text(text)


def refresh_tables(filter_text=""):
    tags = [
        "table_all", "table_se", "table_id", "table_bones",
        "table_particle", "table_combat", "table_ik", "table_foot", "table_other"
    ]

    for tag in tags:
        dpg.delete_item(tag, children_only=True)
        dpg.add_table_column(label="#", width_fixed=True, init_width_or_weight=40, parent=tag)
        dpg.add_table_column(label="ID", width_fixed=True, init_width_or_weight=50, parent=tag)
        dpg.add_table_column(label="String Value", width_stretch=True, parent=tag)

    if not parser.strings:
        return

    filter_text = filter_text.lower()
    counts = {tag: 1 for tag in tags}

    for i, s in enumerate(parser.strings):
        s_lower = s.lower()

        if filter_text and filter_text not in s_lower:
            continue

        color = None
        target = "table_other"

        if s_lower.startswith("se_"):
            color = (100, 200, 255)
            target = "table_se"
        elif s_lower.startswith("id_"):
            color = (255, 215, 0)
            target = "table_id"
        elif s_lower.startswith("shadow_"):
            color = (100, 255, 100)
            target = "table_bones"
        elif s_lower.startswith("particle"):
            color = (255, 100, 100)
            target = "table_particle"
        elif s_lower.startswith("combat"):
            color = (255, 140, 0)
            target = "table_combat"
        elif s_lower.startswith("ik_target"):
            color = (200, 100, 255)
            target = "table_ik"
        elif s_lower.startswith("foot"):
            color = (0, 255, 255)
            target = "table_foot"

        add_row_to_table("table_all", counts["table_all"], i, s, color)
        counts["table_all"] += 1

        add_row_to_table(target, counts[target], i, s, color)
        counts[target] += 1


def callback_load(sender, app_data):
    path = dpg.get_value("input_path")
    parser.load_file(path)
    dpg.set_value("status_text", parser.status)
    refresh_tables(dpg.get_value("search_box"))


def callback_search(sender, app_data):
    refresh_tables(app_data)


def callback_tab_change(sender, app_data):
    dpg.set_value("search_box", "")
    refresh_tables("")


def create_tab_table(tag_name):
    with dpg.child_window(border=False):
        with dpg.table(tag=tag_name, header_row=True,
                       borders_innerH=True, borders_outerH=True,
                       borders_innerV=True, borders_outerV=True,
                       row_background=True, scrollY=True):
            dpg.add_table_column(label="#", width_fixed=True, init_width_or_weight=40)
            dpg.add_table_column(label="ID", width_fixed=True, init_width_or_weight=50)
            dpg.add_table_column(label="String Value", width_stretch=True)


def main():
    dpg.create_context()

    with dpg.window(tag="Primary Window"):
        dpg.add_text("FABLE 2 STRING PARSER", color=(255, 200, 0))
        dpg.add_separator()

        dpg.add_text("TOC File Path:")

        saved_path = load_last_path()
        dpg.add_input_text(tag="input_path", default_value=saved_path, width=-1)

        dpg.add_button(label="Load & Parse Strings", callback=callback_load, width=-1, height=30)
        dpg.add_text("Ready", tag="status_text", color=(0, 255, 0))

        dpg.add_separator()

        dpg.add_text("Filter / Search:")
        dpg.add_input_text(tag="search_box", callback=callback_search, width=-1,
                           hint="Type to filter... (Clears when switching tabs)")

        dpg.add_separator()

        with dpg.tab_bar(callback=callback_tab_change):
            with dpg.tab(label="All"):
                create_tab_table("table_all")
            with dpg.tab(label="Sound Effects"):
                create_tab_table("table_se")
            with dpg.tab(label="ID"):
                create_tab_table("table_id")
            with dpg.tab(label="Bones"):
                create_tab_table("table_bones")
            with dpg.tab(label="Particle Effects"):
                create_tab_table("table_particle")
            with dpg.tab(label="Combat"):
                create_tab_table("table_combat")
            with dpg.tab(label="IK Target"):
                create_tab_table("table_ik")
            with dpg.tab(label="Footstep"):
                create_tab_table("table_foot")
            with dpg.tab(label="Other"):
                create_tab_table("table_other")

    dpg.create_viewport(title="Fable 2 Parser", width=1000, height=600)
    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("Primary Window", True)
    dpg.start_dearpygui()
    dpg.destroy_context()


if __name__ == "__main__":
    main()