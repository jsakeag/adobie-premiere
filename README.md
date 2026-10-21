# Mock Premiere

A desktop video editor modeled on Adobe Premiere 1.0-2.0 (1991-1992), written in C++17. The core is headless; a thin NanoGUI app shows it. Sister project to `mock-photoshop`. See [CLAUDE.md](CLAUDE.md) for the plan and architecture, [FEATURES.md](FEATURES.md) for the feature list and [STACK.md](STACK.md) for the libraries.

**Status:** milestones 1-4 of 10 are done: importing media, the Project and Clip windows, the Construction window (placing, moving, trimming, cutting and deleting clips on A/B, Super and three audio tracks), the Preview window, saving projects, 24 transitions, the Super track with transparency keys, and rubber-band fades.

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

- **Projects:** File > New Project (Cmd+N), Open... (Cmd+O; or drop a `.mpp` file on the window), Save (Cmd+S) and Save As... (Cmd+Shift+S). A project file is JSON that refers to your media where it lies; if you move the project's folder along with its media, the files are found again. A `*` in the title bar means unsaved changes, and New, Open and Quit ask before dropping them.
- **Menus** run along the top: File, Edit, Project, Clip, Windows. Click a title to pull a menu down; Esc or a click elsewhere closes it.
- **Importing:** File > Import... (Cmd+I), the Project window's **Import...** button, or drop files on the window. Movies and sound files go through FFmpeg (QuickTime, MPEG-4, AVI, WAVE, AIFF, MP3 and more); PNG, JPEG, BMP, GIF and Targa files come in as stills.
- **Project window:** one row per file with a thumbnail, its kind, frame size, frame rate, whether it has sound, and its duration in timecode. Click a row to select it; double-click it (or press Return) to open it in the Clip window. **Clear** (or Delete) removes the selected item.
- **Clip window:** shows the open item's picture.
  - Drag along the scrub bar to move through the clip. The blue band is the range between the in and out marks.
  - The buttons go to the in mark, back one frame, play/stop, forward one frame, and to the out mark. Keys: Space plays and stops, Left and Right step, Home and End go to the marks.
  - **Mark In** (I) and **Mark Out** (O) set the range that will be used when the clip is placed in a program. The out mark keeps the frame you're on.
  - Type a timecode into the box next to the buttons and press Return to jump there.
  - Playback runs to the out mark, or to the end of the clip if you start past it. There's no sound yet (milestone 6).
- **Construction window** (along the bottom): the program, from left to right in time.
  - Tracks, top to bottom: **Video A**, **T** (transitions), **Video B**, **Super** (a picture laid over the program), and **Audio A, B and C**. Where Video A has a clip it shows; elsewhere Video B does; a Super clip goes on top of either.
  - **Double-click** a clip to open its file in the Clip window, or, on the Super track, to open its Transparency settings.
  - **Placing clips:** drag a row from the Project window onto a track. A ghost shows where it will land; its ends snap to nearby clip edges and the edit line. The clip uses the item's in and out marks. A movie with sound puts its picture on the video track and its sound on the first free audio track, and the two stay linked: they move, trim, cut and delete together. Dropping a movie on an audio track places only its sound. Or open an item in the Clip window and choose Clip > **Place at Edit Line** (P): it goes on Video A, or Video B if A is busy, and the edit line moves to its end.
  - Clips on one track can't overlap; a move, trim or placement that would overlap is refused and the status bar says why.
  - **Selection tool** (V, the arrow): click a clip to select it, Shift-click to add or remove. Drag a clip's body to move it in time or to another track of the same kind; drag either end to trim it. Delete (or Edit > Clear) removes the selected clips; Cmd+A selects all.
  - **Razor tool** (C, the scissors): click a clip to cut it in two there.
  - **The edit line** (red) marks the current frame. Click or drag in the timecode ruler to move it; with the Construction window clicked last, Left and Right step one frame, Up and Down jump to the previous or next clip edge, Home and End go to the start and end.
  - **The work area** (the yellow bar above the ruler) is what Preview and Export will use (milestone 6). Drag in the empty bar to draw one, drag its ends to change it, or drag its middle to slide it. Project > Work Area to Program covers everything.
  - **Zoom** with the magnifier buttons, = and -, or Cmd+scroll. Scroll (either direction) moves through time; so does dragging the scroll bar.
- **Super track and transparency:** a clip on Super covers the program unless it's keyed. Double-click it, or select it and choose Clip > **Transparency...**, to pick a key; the dialog previews the clip keyed over the picture beneath it at the edit line.
  - **None** (opaque), **Alpha Channel** (the file's own transparency), **Chroma** (drops a chosen colour), **Luminance** (drops dark areas), **Blue Screen** and **Green Screen** (drop pure blue or green), **Multiply** (darkens what's beneath) and **Screen** (lightens it).
  - **Similarity** sets how close to the colour (or how dark) still drops out, **Blend** softens the edge, and **Reverse key** swaps what's kept and dropped.
- **Rubber bands:** the red line across each Super and sound clip is its opacity or volume, top for 100% and bottom for 0%. Drag the line to add a handle and pull it where you want it; drag a handle to move it (between its neighbours); drag a handle well above or below the track to remove it. Between handles the level changes in a straight line. Handles belong to the clip's source, so they stay with the picture or sound when you trim.
- **Preview window:** the program at the edit line, at the project's frame size.
- **Transitions window** (top right): the 24 transitions. Each icon shows the transition on a sample "A" and "B"; hold the mouse over one to watch it play.
  - Dissolves: Cross, Additive, Dither, Non-Additive. Wipes: Wipe, Barn Doors, Band Wipe, Checkerboard, Clock Wipe, Venetian Blinds, Random Blocks, Inset. Irises: Round, Square, Diamond, Cross. Slides: Push, Slide, Split, Center Split, Center Merge, Swap. Plus Zoom and Stretch. (Premiere 1.0 also had 24; which 24 isn't documented anywhere I could find, so these are a plausible set.)
  - **Adding one:** drag it onto the **T** track. Over a stretch where Video A and Video B clips overlap, it fills the overlap and points from the clip that ends first to the other. Elsewhere it lasts one second. Transitions can't overlap each other.
  - In the T track, the arrow on a transition shows its direction: down for A to B, up for B to A. Under a transition the program plays from one track's picture to the other's; a track with no clip there reads as black. Drag a transition to move it, or its ends to change its length; Delete clears it.
  - **Settings:** double-click a transition (or Clip > Transition Settings...). Start and End set how far along the first and last frames are, with a preview of each using the real pictures (or the sample A and B). **A to B** / **B to A** sets the direction, **Reverse** runs the motion backwards (an iris closes instead of opening), and transitions that have them get a **Direction** menu (sides, corners, horizontal or vertical) and a **Border** width with a white or black colour.
- **Project > Settings...:** frame size (including Premiere 1.0's 160 x 120 and 192 x 144), frame rate (10 to 30 fps, including 29.97 drop-frame), sound format (8-bit 22 kHz as in 1.0, up to 16-bit 44.1 kHz as in 2.0), and how many frames a still lasts when placed. Changing the frame rate rescales the marks.
- **Undo:** Cmd+Z, or Edit > Undo. Undo is single-level, as in early Premiere: Cmd+Z again redoes. Every change to the project can be undone: imports, clearing, marks, placing, moving, trimming, cutting, the work area and settings.
- The status bar shows the project format, or the last error.
