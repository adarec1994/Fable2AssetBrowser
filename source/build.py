# build.py
import PyInstaller.__main__
import os

if __name__ == '__main__':
    pyinstaller_args = [
        '--onefile',
        '--windowed',
        '--icon=fable.ico',
        '--add-data=Fable2Cli.exe;.',
        '--add-data=Fable2Cli.exe.config;.',
        '--add-data=Fable2Archives.dll;.',
        'bnk_ui.py'
    ]

    print("--- Running PyInstaller from build script ---")

    PyInstaller.__main__.run(pyinstaller_args)

    print("--- Build script finished ---")