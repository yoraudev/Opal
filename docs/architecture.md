# Architecture

Opal is one process with one UI thread and a few short-lived or narrowly scoped helper threads.

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
| `src/app/` | The application object, settings, the keyboard hook, tray icon, sounds and Windows integration |
| `src/capture/` | Grabbing the desktop, listing windows, Windows.Graphics.Capture, the UI Automation snapshot, edge snapping and corner radius measurement |
| `src/overlay/` | The capture session, the per-monitor overlay windows, the capture bar and the self-timer |
| `src/render/` | GPU device lifetime, the window base class and CPU image operations |
| `src/ui/` | Theme colors, icons drawn from code and the immediate-mode widget toolkit |
| `src/thumbnail/`, `src/editor/`, `src/settingsui/` | The floating thumbnail, the markup editor and the settings window |
| `src/output/` | PNG encoding, file names and clipboard formats |
| `src/core/` | Geometry and image types, logging, interface strings and the version header template |
| `tests/` | Unit tests for the parts that need no window or GPU |

The code that does not touch windows or the GPU (`Image`, `ImageOps`, `EdgeSnap`, `CornerRadius`, `Hotkey`, `Ini`) is built as the `opal_core` library, which both `Opal.exe` and `opal_tests.exe` link.

## Threads

| Thread | Lifetime | Work |
| --- | --- | --- |
| UI thread | Process | Message loop, every window, capture sessions, rendering |
| Keyboard hook thread | Process | Owns the `WH_KEYBOARD_LL` hook and its message loop |
| UI Automation worker | One per element-mode session | Fetches accessibility trees in a multithreaded COM apartment |
| Delivery thread | One per capture | Encodes the PNG, writes the file and fills the clipboard |

The hook lives on its own thread so that Windows never removes it for being slow while the UI thread renders or encodes. It matches key presses against the configured shortcuts, swallows matches and posts `Action` indexes to the application window. Print Screen and Win+Shift+S are swallowed before Windows sees them, which is what keeps the Snipping Tool from opening. While a shortcut is being recorded in the settings, every key press goes to the settings window instead. While a capture overlay is up, Esc, Enter, Space, Tab, the arrow keys and 1 to 4 are forwarded to the overlay and swallowed, so they work even when Windows refuses the overlay keyboard focus.

When a shortcut includes the Windows key, the hook sends an unassigned virtual key after it, so that releasing the Windows key does not open the Start menu.

## Windows and rendering

Every window derives from `D2DWindow`. It creates the window with `WS_EX_NOREDIRECTIONBITMAP` and shows a DirectComposition visual whose content is a premultiplied-alpha flip swap chain, which gives per-pixel transparency for the thumbnail, the timer and the overlay. Drawing is Direct2D in physical pixels; UI code calls `BeginUiFrame`, which applies the DPI scale so widgets work in device-independent pixels.

A frame is drawn on `WM_PAINT`. `Present` waits for vertical sync, and a window that is animating invalidates itself again after each frame. Input messages update a `UiInput` snapshot that the next frame consumes.

Anything that must not run in the middle of a frame goes through `AfterFrame`, which queues it and posts a message: modal dialogs, saving from the editor, and closing. `Close()` is always deferred the same way, because destroying a window inside its own frame frees the device context that frame is drawing with.

`Gpu` creates the Direct3D 11 device, the Direct2D device and the DirectComposition device on first use. Windows hold a reference while they exist; twenty seconds after the last one is gone, the application's idle timer releases the devices and trims the working set. A lost device bumps a generation counter, and each window rebuilds its swap chain and bitmaps when it sees a new generation.

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

`Settings` holds every option with its default and is loaded from and saved to `%APPDATA%\Opal\settings.ini` through `Ini`, which writes to a temporary file and moves it into place. A missing file means a first start, on which Opal registers itself to start with Windows and opens the settings window.
