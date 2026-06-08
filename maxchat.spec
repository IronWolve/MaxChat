# -*- mode: python ; coding: utf-8 -*-
# PyInstaller build spec for MaxChat.
#
#   build:   pyinstaller maxchat.spec        (run on the TARGET OS — no cross-compiling)
#   output:  dist/maxchat   (Linux)  ·  dist\maxchat.exe   (Windows)
#   verify:  dist/maxchat --selftest          (builds the UI headless, exits 0)
#
# Bundles the runtime-required OFL comic font + the example scripts. It deliberately does NOT
# bundle assets/cc-art (dev-only test art, gitignored): shipped builds carry no third-party art —
# comic mode loads art from the user's own install (Comic ▸ Comic Settings).

from PyInstaller.utils.hooks import collect_data_files

datas = [
    ("assets/fonts", "assets/fonts"),            # Comic Relief (OFL) — needed at runtime for balloons
    ("assets/wallpapers", "assets/wallpapers"),  # theme wallpapers (e.g. the Synthwave backdrop)
    ("assets/sounds", "assets/sounds"),          # default notification chime (notify.wav)
    ("assets/translations", "assets/translations"),  # compiled Qt .qm UI translations
    ("scripts-examples", "scripts-examples"),    # seeded into <config>/scripts on first run
] + collect_data_files("spellchecker", includes=["resources/*.json.gz"])

a = Analysis(
    ["maxchat/__main__.py"],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=["PySide6.QtMultimedia", "PySide6.QtMultimediaWidgets"],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=["tkinter", "pytest", "black", "ruff", "PyInstaller"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="maxchat",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    runtime_tmpdir=None,
    # Windowed app: NO console window on Windows (and Qt's stderr chatter like the FFmpeg line is
    # hidden). The --selftest still works by exit code; build.bat runs it with `start /wait`.
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon="assets/icons/maxchat.ico",  # the speech-bubble icon (DEVDOCS/tools/make_icon.py); Windows .exe only
)
