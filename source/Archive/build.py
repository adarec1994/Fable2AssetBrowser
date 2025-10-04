# build.py
import PyInstaller.__main__
import os

if __name__ == '__main__':
    # PyInstaller uses a different separator for add-data src:dest on Windows vs POSIX
    sep = ';' if os.name == 'nt' else ':'

    pyinstaller_args = [
        '--onefile',
        '--windowed',
        '--name=BNKExplorer',
    ]

    # Add icon if it exists
    if os.path.exists('fable.ico'):
        pyinstaller_args.append('--icon=fable.ico')

    # Only add towav.exe if it exists (optional for audio conversion)
    towav_path = os.path.join('tools', 'towav', 'towav.exe')
    if os.path.exists(towav_path):
        pyinstaller_args.append(f'--add-data={towav_path}{sep}tools/towav')
        print(f'Including towav.exe for audio conversion')
    else:
        print(f'towav.exe not found - audio conversion will be unavailable')

    # Add hidden imports that might be needed
    pyinstaller_args.extend([
        '--hidden-import=dearpygui',
        '--hidden-import=bnk_core',
        '--hidden-import=convert',
        'bnk_ui.py',
    ])

    print('--- Running PyInstaller from build script ---')
    print(f'Arguments: {" ".join(pyinstaller_args)}')
    PyInstaller.__main__.run(pyinstaller_args)
    print('--- Build script finished ---')