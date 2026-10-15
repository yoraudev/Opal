# Opal

## Tech Stack

- **Language:** C++20
- **Platform:** Windows 10 and 11, x64 desktop application (Win32, Per-Monitor-V2 DPI aware)
- **Build:** CMake 3.28+ with presets, MSVC from Visual Studio 2026, static C runtime, one `Opal.exe`
- **Rendering:** Direct3D 11, Direct2D, DirectWrite and DirectComposition flip swap chains with per-pixel alpha
- **UI:** an immediate-mode widget toolkit in `src/ui`, no UI framework
- **Capture:** GDI `BitBlt` for the frozen desktop, Windows.Graphics.Capture through C++/WinRT for window content, UI Automation for element bounds
- **Imaging:** WIC for PNG encoding and decoding, CPU image operations in `src/render/ImageOps.cpp`
- **Input:** a `WH_KEYBOARD_LL` hook on a dedicated thread
- **Settings:** an INI file read and written by `src/app/Ini.cpp`
- **Tests:** a self-contained test runner in `tests/tests.cpp`, run through CTest
- **Assets:** the app icon is generated from `res/logo.svg` by `tools/make_icon.py` (Python with Pillow, Edge or Chrome to render the SVG)
- **CI / releases:** GitHub Actions

## Project Rules

- Every file in the repository is written in English: code, identifiers, user-facing strings, documentation and commit messages. The only exception is the Turkish column of the string table in `src/core/Strings.h`, which exists to translate the interface.
- Commit messages follow Conventional Commits: `type(scope): description`, with types such as `feat`, `fix`, `chore`, `docs`, `refactor`, `test` and `ci`. Mark breaking changes with `!` after the type. Keep the subject line short and make it the best single description of the change; put the details in the commit body, written out in full.
- Commits, pull requests and anything else published to the repository carry only the repository owner's name and email. Never add AI or tool attribution: no `Co-Authored-By` trailers, no "Generated with" lines, no assistant names as author or committer.
- Do not write comments. Every hand-written file (anything that is not auto-generated) stays free of code comments, including config files such as YAML and `.gitattributes`.
- Whenever something is added to, changed in, or removed from the project, every `.md` file that references it must be updated in the same change.
