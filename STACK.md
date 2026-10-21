# Stack

A desktop video editor built the same way as the sister repo `mock-photoshop` (`../mock-photoshop`): a C++17 core with no UI code, a thin NanoGUI front end, and a Catch2 test suite. The feature scope is in [FEATURES.md](FEATURES.md).

## Summary

| Area | Choice | How it's brought in | From mock-photoshop? |
|---|---|---|---|
| Language | C++17 | Apple clang 21 | Yes |
| Build | CMake 3.24 or later (3.31 installed) | Homebrew | Yes |
| UI | NanoGUI, OpenGL backend | FetchContent, pinned to commit `500be32` | Yes |
| Timeline and widget drawing | NanoVG (bundled with NanoGUI) | Comes with NanoGUI | Yes |
| Video and audio decode/encode | FFmpeg libraries: libavformat, libavcodec, libavutil, libswscale, libswresample | Homebrew (FFmpeg 9.0.2), found with pkg-config | No, new |
| Capture (2.0) | libavdevice (AVFoundation input on macOS) | Homebrew, found with pkg-config | No, new |
| Audio playback | miniaudio (one header) | Vendored in `third_party/` | No, new |
| Still images and Filmstrip | stb_image, stb_image_write | Vendored in `third_party/stb/` | Yes |
| Title text rendering | stb_truetype | Vendored in `third_party/stb/` | No, new |
| Project file | nlohmann/json 3.11.3 | Vendored in `third_party/nlohmann/` | Yes |
| Threading | `std::thread` plus a small pool | Standard library | Planned there, not built |
| Tests | Catch2 v3.8.1 plus golden frames | FetchContent | Yes |

Install everything Homebrew provides with `brew install cmake ffmpeg`. NanoGUI and Catch2 download during the CMake configure step.

## Layout

```
mock-premiere-pro/
  CMakeLists.txt     options MPP_BUILD_TESTS, MPP_BUILD_APP (both ON)
  core/              mpp_core static library, namespace mpp, no UI or GL includes
  app/               mock_premiere executable (NanoGUI), no editing logic
  tests/             mpp_tests (Catch2), golden/ frames, fixtures/ media
  third_party/       stb, miniaudio, nlohmann
```

`-DMPP_BUILD_APP=OFF` builds the core and tests without NanoGUI, as in mock-photoshop.

## Choices in detail

### UI: NanoGUI
- Use the same pin as mock-photoshop: `mitsuba-renderer/nanogui` at commit `500be32ddd2d8f6518c9f29136aaa422dc615a61`. Later commits break the macOS OpenGL build.
- CMake settings to reuse: `NANOGUI_BACKEND=OpenGL`, examples, Python bindings, shared build and install all off.
- Define `GL_SILENCE_DEPRECATION`, because Apple deprecates OpenGL.
- Draw the Construction window yourself with NanoVG: tracks, clips, the transition track, rubber-band lines, the work area bar and the time ruler. Making clips child widgets would hit NanoGUI's rule that drags go to the innermost widget, which mock-photoshop worked around in `LayerRow`.
- Show video frames by uploading them into a GL texture, as mock-photoshop's `CanvasView` does.
- Lessons from mock-photoshop that carry over:
  - Free NanoVG images before the NanoVG context is destroyed, never in a widget's destructor. Doing it in the destructor crashed on quit.
  - Rebuild widgets in `refresh()`, never inside their own callbacks.
  - A popup draws empty on its first frame, so screenshots of open menus need two captures.

### Media: FFmpeg libraries
- Link the libraries directly rather than calling the `ffmpeg` command-line tool. The core needs individual frames by timestamp for scrubbing and preview.
- In the core, put FFmpeg behind a small interface: `MediaSource` (open a file, read its duration and frame rate, decode the frame at a time, read audio samples) and `MovieWriter` (encode frames and audio to a file). Tests can then use fake sources.
- Use libswscale to convert decoded frames to RGBA8, and libswresample to convert audio to the project's sample format (8-bit in 1.0, 16-bit 44.1 kHz in 2.0).
- Link with `pkg_check_modules(... IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)`, the same pattern mock-photoshop uses for zstd.
- License: Homebrew's FFmpeg build includes GPL parts. That's fine for a personal project, but it matters before distributing a build.

### Capture (2.0): libavdevice
- On macOS, libavdevice's AVFoundation input handles the camera and microphone without any Objective-C code.
- The app needs a camera and microphone usage description (an `Info.plist` in an app bundle) before macOS will grant access. Leave this until the Capture milestone.

### Audio playback: miniaudio
- One vendored header, compiled once in the core, in the same style as stb.
- It's preferred over SDL3, even though SDL3 is installed, because NanoGUI already owns the window through GLFW and SDL would add a second windowing layer.
- The audio clock drives playback; video frames are shown to match it.

### Images and titles: stb
- stb_image loads still images and numbered image sequences; stb_image_write exports frames, thumbnails and Filmstrips.
- stb_truetype draws title text into frames for export. NanoVG can only draw text to the screen.
- mock-photoshop compiles stb itself because NanoGUI bundles its own copy. Do the same here.

### Project file: JSON
- A plain `.json` project that references media by path: tracks, clips, in/out points, transitions, filter settings, motion points and titles. Store a format version from the first save, as mock-photoshop does.
- miniz and zstd aren't needed unless the project later bundles media into the file.

### Threading
- Decode frames on a worker pool and keep recent ones in a frame cache keyed by clip and frame number. mock-photoshop deferred its thread pool, but video playback needs background decoding from the start.
- Rendering a preview of the work area runs in the background and reports progress to the UI.

### Tests: Catch2 and golden frames
- One golden frame per transition (at 50%) and per filter, compared exactly. Compile the core with `-ffp-contract=off` so results match on every CPU, as mock-photoshop does.
- Generate fixture media with short, synthetic clips (color bars, a moving square, a tone) so the tests don't depend on real footage.
- Add round-trip tests for the project file, Filmstrip export and import, EDL output and timecode formatting.
- Copy mock-photoshop's `--selftest` mode: it drives the app's actions, checks the timeline and saves screenshots read from the app's own framebuffer.

## Written by hand (no library)
- SMPTE timecode, including drop-frame for 29.97 fps
- EDL export in CMX 3600 format
- Filmstrip export and import: frames stacked in one tall image, each labeled with its timecode
- Transitions, video filters and audio filters
- Compositing the Superimpose track and its transparency keys

## Reused from mock-photoshop
Copy these files in rather than linking the other repo, so the two projects can change independently:
- Blend and compositing math (`core/src/blend.cpp`), for the Superimpose track
- Adjustments and filters (`adjustments.cpp`, `filters.cpp`): Brightness/Contrast, Hue/Saturation, Invert, Gaussian Blur and others, as a start on the video filter list
- The `Command` base class and `Editor`: every edit is a command, with snapshot undo
- The golden-image helpers (`tests/golden.h`, `tests/golden.cpp`)
- The `--selftest` setup and its screenshot capture (`app/selftest.cpp`)

## Not using
- **SDL3:** a second windowing layer next to GLFW (see audio playback above).
- **OpenCV:** FFmpeg already decodes and scales, and the editor has no computer-vision features.
- **The `ffmpeg` command-line tool:** fine for quick checks, but not frame-accurate enough for scrubbing.
- **miniz and zstd:** only needed if projects bundle media.
