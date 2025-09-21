# build.py
import PyInstaller.__main__
import os

if __name__ == '__main__':
    # PyInstaller uses a different separator for add-data src:dest on Windows vs POSIX
    sep = ';' if os.name == 'nt' else ':'

    pyinstaller_args = [
        '--onefile',
        '--windowed',
        '--icon=fable.ico',
        f'--add-data=Fable2Cli.exe{sep}.',
        f'--add-data=Fable2Cli.exe.config{sep}.',
        f'--add-data=Fable2Archives.dll{sep}.',
        f'--add-data=tools{os.sep}towav{os.sep}towav.exe{sep}tools/towav',
        'bnk_ui.py',
    ]

    print('--- Running PyInstaller from build script ---')
    PyInstaller.__main__.run(pyinstaller_args)
    print('--- Build script finished ---')
