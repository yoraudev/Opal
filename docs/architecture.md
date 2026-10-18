# Architecture

Opal is one process with one UI thread and a few short-lived or narrowly scoped helper threads. The same `Opal.exe` is also its own installer and uninstaller, started in those roles by `OpalSetup.exe` or by Windows (see [Installer](#installer)).

```
keyboard hook thread ──PostMessage──> App (UI thread) ──> CaptureSession ──> OverlayWindow (one per monitor)
                                          │                    │
                                          │                    └──> CaptureResult
                                          │
                                          ├──> delivery thread ──> PNG file, clipboard
                                          └──> ThumbnailWindow, EditorWindow, SettingsWindow
```

| Directory | Role |
| --- | --- |
| `src/app/` | The application object, settings, the keyboard hook, tray icon, sounds, update checks and Windows integration |
| `src/capture/` | Grabbing the desktop, listing windows, Windows.Graphics.Capture, the UI Automation snapshot, edge snapping and corner radius measurement |
| `src/overlay/` | The capture session, the per-monitor overlay windows, the capture bar and the self-timer |
| `src/render/` | GPU device lifetime, the window base class and CPU image operations |
| `src/ui/` | Theme colors, icons drawn from code and the immediate-mode widget toolkit |
| `src/thumbnail/`, `src/editor/`, `src/settingsui/` | The floating thumbnail, the markup editor and the settings window |
| `src/output/` | PNG encoding, file names and clipboard formats |
| `src/setup/` | The installer window, install, update and uninstall steps, and the rules they share with the tests |
| `src/stub/` | `OpalSetup.exe`, which carries `Opal.exe` and starts it as the installer |
| `src/core/` | Geometry and image types, logging, interface strings and the version header template |
| `tests/` | Unit tests for the parts that need no window or GPU |
| `tools/` | The icon builder and `opal_preview`, which renders the installer pages to PNG files |

The code that does not touch windows or the GPU (`Image`, `ImageOps`, `EdgeSnap`, `CornerRadius`, `Hotkey`, `Ini`, `InstallRules`, `ReleaseInfo`) is built as the `opal_core` library, which `opal_tests.exe` links. Everything else except `main.cpp` is the `opal_app` library on top of it, which `Opal.exe` and `opal_preview.exe` link.

## Threads

| Thread | Lifetime | Work |
| --- | --- | --- |
| UI thread | Process | Message loop, every window, capture sessions, rendering |
| Keyboard hook thread | Process | Owns the `WH_KEYBOARD_LL` hook, the fallback hotkeys and its message loop |
| UI Automation worker | One per element-mode session | Fetches accessibility trees in a multithreaded COM apartment |
| Delivery thread | One per capture | Encodes the PNG, writes the file and fills the clipboard |
| Update worker | One per update check or download | Talks to GitHub over WinHTTP and posts the result to the application window |

The hook lives on its own thread so that Windows never removes it for being slow while the UI thread renders or encodes. It matches key presses against the configured shortcuts, swallows matches and posts `Action` indexes to the application window. Print Screen and Win+Shift+S are swallowed before Windows sees them, which is what keeps the Snipping Tool from opening. While a shortcut is being recorded in the settings, every key press goes to the settings window instead. While a capture overlay is up, Esc, Enter, Space, Tab, the arrow keys and 1 to 4 are forwarded to the overlay and swallowed, so they work even when Windows refuses the overlay keyboard focus.

When a shortcut includes the Windows key, the hook sends an unassigned virtual key after it, so that releasing the Windows key does not open the Start menu.

Windows removes a low-level hook without notice when it does not answer within the hook timeout, which can happen under heavy load even though the hook thread does almost nothing; the shortcuts then stop working while Opal keeps running. The hook thread defends against this in three ways. It installs a fresh hook every 60 seconds and whenever `App` reports an unlock, a resume from sleep or a display change, installing the new hook before removing the old one so no key press falls between them. It registers every shortcut a second time with `RegisterHotKey` on the same thread: a key the hook swallows never reaches the hotkey, so `WM_HOTKEY` arrives only when the hook missed a press, either because it was removed or because an administrator window has the focus. The hotkey then triggers the action itself, unless the hook already did within the last 500 ms, and reinstalls the hook. Win+Shift+S is owned by Windows and cannot be registered. Finally the thread opts out of power throttling, and the application trims its working set once per idle period instead of every few seconds, so the hook's code is rarely paged out when a key arrives.

## Windows and rendering

Every window derives from `D2DWindow`. It creates the window with `WS_EX_NOREDIRECTIONBITMAP` and shows a DirectComposition visual whose content is a premultiplied-alpha flip swap chain, which gives per-pixel transparency for the thumbnail, the timer and the overlay. Drawing is Direct2D in physical pixels; UI code calls `BeginUiFrame`, which applies the DPI scale so widgets work in device-independent pixels.

A frame is drawn on `WM_PAINT`. `Present` waits for vertical sync, and a window that is animating invalidates itself again after each frame. Input messages update a `UiInput` snapshot that the next frame consumes.

A window that runs a modal loop of its own (a context menu, a file dialog, a drag) holds a `ModalScope` for its duration. Closing the window during that time, from its own timer or from `App`, is postponed until the scope ends, because destroying it would free the object whose code is still on the stack below the modal loop.

Anything that must not run in the middle of a frame goes through `AfterFrame`, which queues it and posts a message: modal dialogs, saving from the editor, and closing. `Close()` is always deferred the same way, because destroying a window inside its own frame frees the device context that frame is drawing with.

`Gpu` creates the Direct3D 11 device, the Direct2D device and the DirectComposition device on first use. Windows hold a reference while they exist; twenty seconds after the last one is gone, the application's idle timer releases the devices and trims the working set. A lost device bumps a generation counter, and each window rebuilds its swap chain and bitmaps when it sees a new generation. `Gpu::Shutdown` releases everything, including the factories, before COM is uninitialized at exit.

`D2DWindow::Snapshot` draws a window's frame into an off-screen bitmap of a given size and DPI instead of its swap chain; `opal_preview` uses it to render the installer pages.

Windows that look like regular windows (settings, editor) remove the system title bar in `WM_NCCALCSIZE`, extend the frame into the client area for the Mica backdrop and draw their own caption buttons. They omit `WS_SYSMENU`, because DWM would otherwise draw its own caption buttons over the extended frame.

## Capture session

A `CaptureSession` lives from the shortcut until the capture is finished or cancelled.

1. The whole virtual screen is copied with `BitBlt` into one `Image`, which is the frozen desktop every overlay shows.
2. The visible top-level windows are listed front to back with their visible bounds (`DWMWA_EXTENDED_FRAME_BOUNDS`). Opal's own settings and editor windows are included; its tool windows (the hidden application window, the thumbnail and the timer) are not.
3. One topmost `OverlayWindow` is created per monitor, drawn once and then shown, so it never flashes empty content.
4. Overlays convert mouse input to virtual-screen coordinates and pass it to the session, which owns all state: the mode, the selection, the hovered window or element. Each overlay draws the part of that state that falls on its monitor.
5. On a click or Enter, the overlays are hidden and the session builds a `CaptureResult`, then hands it to `App` through a callback. `App` destroys the session from its own message handler, never from inside an overlay's.

The capture bar is drawn by the overlay of the monitor the session started on. Its glass background is the frozen desktop run through a Direct2D Gaussian blur and saturation effect, clipped to the bar's rounded rectangle. When a self-timer is set, the overlays are hidden, a small `CountdownWindow` counts down, and the desktop is grabbed again before the result is built.

### Window captures

The window under the pointer is captured with Windows.Graphics.Capture, which returns the window's own surface regardless of what covers it. The frame is cropped to the visible bounds, its alpha is forced to opaque (apps leave it undefined), and a rounded mask with the radius Windows 11 uses for that window (8 or 4 pixels times the monitor's scale, none when maximized) is applied. A one-pixel hairline and a two-layer shadow follow. When Windows.Graphics.Capture delivers no frame within 1.5 seconds, the frozen pixels inside the window's bounds are used instead.

### Element captures

Element mode combines two sources.

`ElementTree` takes a snapshot of the hovered window's UI Automation tree on a worker thread: every on-screen element in the control view with its bounding rectangle, fetched in one cached `FindAll` call. The overlay covers the screen, so `ElementFromPoint` would only find the overlay itself; the snapshot is hit-tested instead. Elements containing the pointer are sorted from smallest to largest, near-duplicates are merged, and the window itself is added as the largest candidate. The default pick is the smallest candidate at least 28 by 20 device-independent pixels.

`EdgeSnap` refines a rectangle against the frozen pixels. For each side it scores every candidate line within a band around the rough edge by the fraction of the side's middle half where neighboring pixels differ by more than a small color threshold, picks the best-scoring line nearest the rough edge, and then steps outward over a second strong line so that a border is included. Hovered elements use a band of 6 device-independent pixels, dragged selections a band of 24.

`CornerRadius` measures each corner of the snapped rectangle. It takes the inside and outside colors from the straight parts of the two adjacent edges, walks the diagonal from the corner until the pixel turns into the inside color, and converts the sub-pixel position `t` of that crossing into a radius with `r = t / (1 - 1/√2)`. Thin anti-aliased borders never reach the inside color on the diagonal, so the pixel where the coverage peaks counts as the crossing. The radius is accepted only if points just outside and just inside the arc at 22.5 and 67.5 degrees look like outside and not-outside. Radii below 2 pixels count as square, and radii of the same shape that are close to each other are set to their median.

## Output

`App::HandleResult` plays the shutter sound, shows the thumbnail with the captured image, and starts a delivery thread with a copy of it. That thread encodes the PNG once with its own WIC factory, writes the file, and puts three formats on the clipboard: `PNG`, `CF_DIBV5` with straight alpha, and `CF_DIB` flattened on white for programs that ignore alpha. Images without transparency are stored as 24-bit PNG and get only `PNG` and `CF_DIB`. When the file is written, the thumbnail learns its path, which it needs to show it in its folder or to drag it out; dragging before that writes a temporary copy.

The editor keeps markup as objects in image coordinates and redraws them over the captured bitmap every frame. Blur and pixelate store a processed patch of the covered pixels. Saving renders the image and the markup into an off-screen Direct2D bitmap of the cropped size and reads it back.

## Interface toolkit

`Ui` is an immediate-mode toolkit: each frame calls functions such as `Toggle`, `Segmented` or `Dropdown`, which draw the control and report whether it changed. Hover and press state is keyed by an id built from a string hash, animation values are smoothed per id, and dropdowns, menus and tooltips are drawn after everything else through deferred callbacks. A selection made in a popup is applied when the owning control runs in the next frame.

Interface strings are defined once in `src/core/Strings.h`, each with an English and a Turkish text; `T()` returns the one for the language chosen in the settings, or the Windows display language when it is set to follow the system.

## Settings

`Settings` holds every option with its default and is loaded from and saved to `%APPDATA%\Opal\settings.ini` through `Ini`, which writes to a temporary file and moves it into place. A missing file means a first start, on which Opal opens the settings window.

Starting with Windows is not a setting in the file. The `Opal` value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` is the source of truth, together with the value of the same name under `Explorer\StartupApproved\Run`, where Task Manager records that a startup entry was disabled. On every start `App` reads both into the switch in the settings. If the entry is enabled but points to another copy of Opal, it is moved to the running copy when that copy is the installed one or when the other file no longer exists. Turning the switch on writes the entry and clears a disabled mark; turning it off removes both values.

## Installer

`OpalSetup.exe` is a small program with `Opal.exe` embedded as a resource. It writes that copy to a new folder under `%TEMP%`, runs it with `--setup` and the arguments it was given, waits for it and deletes the folder.

`Opal.exe --setup` and `Opal.exe --uninstall` are handled in `main.cpp` before the single-instance check, so they run next to an Opal that is already running. Both log to `setup.log`. The installed copy is described by `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Opal`, whose `InstallLocation` and `DisplayVersion` tell the setup whether it is installing, updating, reinstalling or replacing a newer version. `SetupWindow` is a `D2DWindow` in the style of the settings window; its work runs on a worker thread that reports each step back to the window.

Installing or updating:

1. Every running Opal, installed or not, is asked to quit through the `--quit` command it already understands, and is terminated if it has not exited after five seconds.
2. An existing `Opal.exe` in the target folder is renamed to `Opal.exe.old`, which works even if the file is still in use, and the running setup copies itself into place. The installed Opal deletes a leftover `Opal.exe.old` when it starts. Before copying, the setup creates and deletes a temporary file in the nearest existing folder of the target. If that is refused, only this step runs in a second copy of the setup started with `--copy-to <folder>` through `ShellExecuteEx` with the `runas` verb, so Windows shows its permission prompt; everything else, including starting Opal afterwards, stays in the unelevated setup. Declining the prompt ends the setup with an explanation.
3. The uninstall entry, the Start menu and desktop shortcuts and the startup entry are written according to the choices. On an update the choices start from the current state, and the folder cannot be changed.
4. When the window closes, Opal is started from the installed folder if requested, in the background for updates.

When the installed folder cannot be written without administrator rights, `--uninstall` restarts itself elevated the same way before doing anything, so the files can be deleted.

Uninstalling removes the startup entry if it points to the installed copy, the shortcuts that point to it, the uninstall entry and optionally the settings and logs. Only `Opal.exe` and `Opal.exe.old` are deleted from the folder, and the folder itself only when it is empty, so a folder chosen by hand never loses other files. A running executable cannot delete itself, so when the uninstaller runs from the installed folder it starts a hidden `cmd.exe` that waits two seconds after the uninstaller exits, then deletes the remaining files and empty folders.

### Updates

`Updater` runs only in the installed copy. An hour-long application timer, first firing a minute after start, calls `CheckIfDue`, which starts a check when the last one (`Updates.lastCheck` in the settings) is at least a day old and the switch is on. A worker thread requests `https://api.github.com/repos/yoraudev/Opal/releases/latest` with WinHTTP, and `ReleaseInfo` parses the JSON with a small parser of its own. A release counts as an update when its version is newer than `OPAL_VERSION` and it has an `Opal-Setup-<version>.exe` asset whose download address lies under `https://github.com/yoraudev/Opal/releases/download/`. `App` announces a new version once in a tray notification, remembering it in `Updates.notified`, and adds it to the tray menu and the About page.

Installing an update downloads the asset into `%TEMP%\Opal-Update`, only over HTTPS, to a `.part` file that is renamed once its size matches the size the API reported, then starts it with `--silent`. From there the update is an ordinary silent setup: it closes this Opal, replaces it and starts the new version in the background, which deletes `%TEMP%\Opal-Update` when it starts.

The options page shows the size of the setup's own `Opal.exe` and the free space on the target's drive, read with `GetDiskFreeSpaceEx` from the nearest existing folder.

The rules that need no Windows calls, such as comparing versions, formatting sizes, naming the drive of a path, appending `Opal` to a chosen folder, reading the program path out of a startup command and parsing arguments, are in `InstallRules`, and reading a release from the GitHub API is in `ReleaseInfo`; both are covered by the unit tests.
