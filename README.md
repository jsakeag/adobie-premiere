# Mock Premiere

A desktop video editor modeled on Adobe Premiere 1.0-2.0 (1991-1992), written in C++17. The core is headless; a thin NanoGUI app shows it. Sister project to `mock-photoshop`. See [CLAUDE.md](CLAUDE.md) for the plan and architecture, [FEATURES.md](FEATURES.md) for the feature list and [STACK.md](STACK.md) for the libraries.

**Status:** milestone 1 of 10 is done: importing media, the Project window, and the Clip window with playback and in/out marks.

## Build and test

Needs CMake, a C++17 compiler and FFmpeg (`brew install cmake ffmpeg`). NanoGUI and Catch2 are downloaded by CMake; stb, miniaudio, nlohmann/json and the Roboto fonts are in `third_party/`.

```
cmake -S . -B build && cmake --build build -j
./build/tests/mpp_tests
```

Add `-DMPP_BUILD_APP=OFF` to the first command to skip the NanoGUI app.

The GUI self-test drives the app through its windows and menus and saves screenshots:

```
mkdir -p /tmp/mpp_shots && ./build/app/mock_premiere --selftest /tmp/mpp_shots tests/fixtures
```

Test media lives in `tests/fixtures/`; `tests/fixtures/make_fixtures.sh` rebuilds it with the `ffmpeg` command-line tool.

## Run the app

```
./build/app/mock_premiere [files to import...]
```

- **Menus** run along the top: File, Edit, Project, Clip, Windows. Click a title to pull a menu down; Esc or a click elsewhere closes it.
- **Importing:** File > Import... (Cmd+I), the Project window's **Import...** button, or drop files on the window. Movies and sound files go through FFmpeg (QuickTime, MPEG-4, AVI, WAVE, AIFF, MP3 and more); PNG, JPEG, BMP, GIF and Targa files come in as stills.
- **Project window:** one row per file with a thumbnail, its kind, frame size, frame rate, whether it has sound, and its duration in timecode. Click a row to select it; double-click it (or press Return) to open it in the Clip window. **Clear** (or Delete) removes the selected item.
- **Clip window:** shows the open item's picture.
  - Drag along the scrub bar to move through the clip. The blue band is the range between the in and out marks.
  - The buttons go to the in mark, back one frame, play/stop, forward one frame, and to the out mark. Keys: Space plays and stops, Left and Right step, Home and End go to the marks.
  - **Mark In** (I) and **Mark Out** (O) set the range that will be used when the clip is placed in a program. The out mark keeps the frame you're on.
  - Type a timecode into the box next to the buttons and press Return to jump there.
  - Playback runs to the out mark, or to the end of the clip if you start past it. There's no sound yet (milestone 6).
- **Project > Settings...:** frame size (including Premiere 1.0's 160 x 120 and 192 x 144), frame rate (10 to 30 fps, including 29.97 drop-frame), sound format (8-bit 22 kHz as in 1.0, up to 16-bit 44.1 kHz as in 2.0), and how many frames a still lasts when placed. Changing the frame rate rescales the marks.
- **Undo:** Cmd+Z, or Edit > Undo. Undo is single-level, as in early Premiere: Cmd+Z again redoes. Imports, Clear, marks and settings changes can all be undone.
- The status bar shows the project format, or the last error.
