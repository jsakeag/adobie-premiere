# CLAUDE.md: Premiere 1.0-2.0 Clone

## Project summary
A desktop video editor modeled on Adobe Premiere 1.0 (Mac, 1991) and 2.0 (Mac, 1992): Project bin, Clip window, A/B Construction window with a transition track and a superimpose track, transitions, filters, transparency keys, rubber-band fades, preview and export; then 2.0's titles, Sequence window, motion, speed, SMPTE timecode, EDL, image sequences, Filmstrip and capture. Sister repo: `../mock-photoshop`, whose structure and rules this follows. Feature list: [FEATURES.md](FEATURES.md). Libraries: [STACK.md](STACK.md).

**Current milestone:** 5
**Next step:** video filters (`FilterDef`: id, name, parameters with ranges, `apply(image, values)`) with a start and an end value per parameter blended across the clip, about 20 of them, each with a golden (start from mock-photoshop's adjustments and filters: copy, don't link); `FilterInstance` list on `Clip`, applied in `clip_picture` before fitting or after (decide: after fitting, at project size, so parameters in pixels mean frame pixels); audio filters (Echo, Pan, Backwards, Fill Left, Fill Right) stored the same way, used by the mixer in milestone 6; a Filters dialog (Clip > Filters...) listing the clip's filters with add/remove/reorder and start/end controls with previews; project file.

**Known issues:** none yet.

## Build and test
```
cmake -S . -B build && cmake --build build -j
./build/tests/mpp_tests                          # full suite
mkdir -p /tmp/mpp_shots && ./build/app/mock_premiere --selftest /tmp/mpp_shots tests/fixtures
MPP_UPDATE_GOLDEN=1 ./build/tests/mpp_tests "[golden]"   # rewrite goldens, then look at them
cmake -S . -B build -DMPP_BUILD_APP=OFF          # core and tests only, skips NanoGUI
```
Needs `brew install cmake ffmpeg`. NanoGUI and Catch2 come through FetchContent.

## Session protocol
1. Start: read this file, run the full test suite, confirm it's green.
2. Work on the current milestone only. **Every milestone includes its GUI binding**: a feature is not done until it can be used from the app, through a command, and the README says how.
3. End: run tests, run `--selftest` and look at its screenshots (macOS screen capture is blocked here; the self-test reads the app's own framebuffer). Extend the self-test for any new GUI. Say plainly what it does not cover: real mouse clicks, popups, file dialogs, key mapping, audio output. Update README.md, commit, update "Current milestone" and "Next step" above.
4. If a milestone runs well over, cut scope within it and note what was cut.

## Hard rules
1. **Core is headless.** No UI or GL includes in `core/`.
2. **Every edit is a Command** (name, parameters, execute, undo). Widgets never change the project directly.
3. **UI only reads project state and issues commands.** Anything with logic goes in `core/` so it can be tested headless.
4. **Every transition, filter and key has a test**: a golden frame, or exact values for simple ones.
5. **No feature work without a test** (unit, golden, or round trip).
6. **Rendering is deterministic.** Core is built with `-ffp-contract=off`; goldens are compared exactly. Fixture media is lossless.

## Architecture (as built, then target)
- *Built in milestone 1:* `Image`, `image_io`, `resample` (`resized`: separable tent filter on premultiplied floats; `fit_into`), `Rate` and timecode (`format_timecode`/`parse_timecode`, drop-frame), `MediaSource` with `FfmpegSource` and a still source (`open_media`), `MediaCache` (LRU frames by byte budget, whole audio per sample rate, per-source mutex), `Project` (settings, bin of `MediaItem`s with Clip-window marks), `Command`/`Editor` with whole-project snapshot undo, `bin_commands.h` (`AddItemCommand`, `RemoveItemCommand`, `SetItemMarksCommand`, `SetSettingsCommand`). App: `MenuBar` (custom drawn, dropdown is a screen child kept alive by a `ref`), `ProjectWindow` (drawn `BinList` rows with NanoVG thumbnails), `ClipWindow` (`FrameView`, `Scrubber`, transport, timecode entry), `SettingsWindow`, `--selftest`.
- `FfmpegSource` keeps a `held` frame (last at or before the request) and a `pending` one (first past it), so reading forward never seeks; a backward or far jump seeks to the keyframe before. Mono sound is resampled as mono and copied to both channels (swresample's upmix pans to -3 dB). A sound-only file's `rate` is its sample rate and `frames` its sample count.
- *Built in milestone 2:* `Project` has `clips` (id, item, track, start, in, out, link) and the work area; `Track` enum (Video A, Video B, Superimpose, Audio A-C). `timeline_commands` (`PlaceClipCommand` with linked sound on the first free audio track, `MoveClipCommand`, `TrimClipCommand`, `RazorCommand`, `DeleteClipsCommand`, `SetWorkAreaCommand`, `check_clip`) refuse overlaps. `RemoveItemCommand` removes the item's clips; `SetSettingsCommand` rescales clips by flooring both ends. `compositing` (`composite_over`, `mix`, `flattened`), `renderer` (`clip_picture`, `render_frame`: A over B over black), `project_file` (JSON `.mpp`, version 1, absolute plus relative paths), `time_scale` (`TimeScale`, zoom levels, `tick_step`, `snap_points`/`snap`/`snap_span`). App: `ConstructionView` (one drawn widget: work bar, ruler, rows, ghosts while dragging, one command on release), `ConstructionWindow` (toolbar), `PreviewWindow`, bin-to-timeline drag through `App::drag_item`, Open/Save/Save As with the unsaved prompt. Screen is 1440x900.
- *Built in milestone 3:* `transitions` (registry of 24 `TransitionDef`s; most are a signed-distance mask through `masked()`, which anti-aliases and draws the border; `edge()` starts the edge `border` pixels early so a border never shows at 0 or 1; `apply_transition` returns from/to exactly at 0/1 and handles reverse by swapping roles; `transition_progress` samples frame middles). `Transition` on `Project` (`transitions`, `transition_at`), `AddTransitionCommand`, `SetTransitionSpanCommand`, `SetTransitionSettingsCommand`, `ClearCommand` (clips and transitions; replaces `DeleteClipsCommand`), `suggest_transition_span`. `render_frame` runs the transition between `track_picture`s of A and B. App: `TransitionsWindow` (animated icon for the hovered row), `TransitionSettingsWindow`, transitions on the T row of `ConstructionView`, App selection holds clip and transition ids. Dialogs report closing through `set_closed_callback` (never read a disposed window). Goldens: `transition_<id>.png`, four panels each.
- *Built in milestone 4:* `keys` (`KeyType`, `TransparencyKey`, `keyed`, `composite_superimpose`; colour keys use RGB distance with similarity and blend, Blue/Green Screen are Chroma against pure blue/green). `Clip` has `fade` (`FadePoint`s in source position, `fade_level`) and `key`. `render_frame` = `render_beneath_super` (A/B/transition) + the Super clip (`clip_picture(..., keep_alpha)`; None flattens the source before fitting so letterboxes stay clear) faded by `fade_level`. `SetFadeCommand`, `SetKeyCommand`. App: rubber bands drawn and edited in `ConstructionView` (Drag::Fade: press on a handle or within 4 px of the line; off the track by 20 px removes), `TransparencyWindow`, double-click on a clip (`open_timeline_clip`). Goldens: `key_<name>.png`.
- NanoGUI notes: the screen subtracts (1, 2) from every cursor position before dispatch; `App::simulate_*` add it back. `preferred_size_impl` is the virtual to override; `MOD_DOUBLE_CLICK` arrives in the press modifiers; give screen-level widgets fixed sizes or `perform_layout()` shrinks them.
- `core/` (`mpp_core`, namespace `mpp`): headless.
  - `Image` (RGBA8, straight alpha), `image_io` (stb), `resample` (bilinear/fit into a frame): copied from mock-photoshop's `ps::Image` and adapted.
  - `Rate` and `timecode`: frame rate as a rational plus a drop-frame flag; SMPTE formatting and parsing (`HH:MM:SS:FF`, `HH:MM:SS;FF` for drop-frame).
  - `MediaSource`: interface for anything that yields frames and audio. `FfmpegSource` (movies, through libavformat/libavcodec/libswscale/libswresample), `StillSource` (one image), `ImageSequenceSource` (numbered files, 2.0), `TitleSource` (2.0), `FilmstripSource` (2.0). `MediaInfo`: size, rate, duration, audio present.
  - `MediaCache`: opens sources by path on demand, caches decoded frames (LRU) and whole decoded audio tracks resampled to the project rate. Thread-safe (one mutex per source).
  - `Project`: a plain value type. Settings (frame size, rate, audio rate and bits), the bin (`MediaItem`s), the timeline (`Clip`s on tracks, `Transition`s on the T track, the work area). Copying a project is cheap enough to snapshot for undo.
  - Times are in **project frames**. A clip's `in`/`out` are project-rate frames into its source; the source frame is found from the time in seconds.
  - Tracks: Video A, Transition, Video B, Superimpose, Audio A, B, C. Clips with sound placed on a video track get a linked audio clip on Audio A.
  - `Command` / `Editor`: as in mock-photoshop: `execute` snapshots the project, undo/redo swap snapshots, single-level undo (Cmd+Z toggles).
  - `transitions`: registry of `TransitionDef` (id, name, `apply(from, to, progress, params)`). `filters`: registry of video `FilterDef` (parameters with start and end values) and `AudioFilterDef`. `keys`: transparency keys for the S track.
  - `Renderer`: `render_frame(project, cache, frame)`: A/B (A wins if both have clips and there is no transition), transition, then the S track keyed and faded over it. Per clip: decode, fit to the frame, filters, motion (2.0).
  - `AudioMixer`: mixes audio tracks with volume rubber bands and audio filters for a range of samples, quantized to the project's bit depth.
  - `project_file`: JSON (`.mpp`) save/load with a format version.
  - `MovieWriter`: FFmpeg encode (H.264/AAC `.mp4`/`.mov`, QuickTime Animation `.mov` lossless).
  - 2.0: `title` (stb_truetype), `motion`, `edl` (CMX 3600), `filmstrip`, `capture` (libavdevice).
- `app/` (`mock_premiere`): NanoGUI shell, no editing logic. A menu bar (File, Edit, Project, Clip, Make, Windows) and floating windows: Project, Clip, Construction, Preview, Transitions, plus dialogs. Construction window drawn with NanoVG in one widget. Playback audio through miniaudio.
- `tests/`: Catch2 v3. Golden PNGs in `tests/golden/`, compared exactly; mismatches written to `build/tests/output/`. Fixture media in `tests/fixtures/` (lossless, generated by `tests/fixtures/make_fixtures.sh`).
- `third_party/`: stb (image, image_write, truetype), miniaudio, nlohmann/json, fonts (Roboto, Apache 2.0, from NanoGUI).

## Scope

### Milestones
- [x] **1. Media and bin:** CMake scaffold; `Image`, `image_io`, `Rate`/timecode; `FfmpegSource`, `StillSource`, `MediaCache`; Project window (import, list with thumbnails and durations), Clip window (play, scrub, set in/out). Fixtures.
- [x] **2. Construction window:** `Project` model, commands and undo; place, move, trim, razor, delete clips; linked audio; time zoom; edit line; Preview window showing the frame under the edit line; `.mpp` save/load.
- [x] **3. Transitions:** T track, 24 transitions with goldens, Transitions window (drag onto the T track), transition settings (direction, start/end %, border).
- [x] **4. Superimpose and fades:** S track compositing, transparency keys (Alpha, Chroma, Luminance, Blue Screen, Green Screen, Multiply, Screen), opacity rubber band on S, volume rubber bands on audio.
- [ ] **5. Filters:** video filters with start/end values, filter dialog; audio filters (Echo, Pan, Backwards, Fill Left, Fill Right).
- [ ] **6. Preview and export:** render the work area in the background, play it back with audio; export movie (H.264, QuickTime Animation) and single frames; full-screen "print to video"; 1.0 presets (160x120, 192x144, 8-bit audio).
- [ ] **7. Titles and Sequence window (2.0).**
- [ ] **8. Motion and speed (2.0).**
- [ ] **9. Timecode, EDL, image sequences, Filmstrip, 16-bit audio (2.0).**
- [ ] **10. Capture (2.0) and polish.** Definition of done passes headless and through the self-test.

### Out of scope
Everything from Premiere 3.0 and later: more tracks, real-time preview, easing, waveform monitor, batch capture, Trim window, dual monitors, three-point editing, multiple filter keyframes. Also multi-level undo.

## Conventions
- One concept per file; keep headers small.
- Commands hold plain parameter values.
- No raw owning pointers.
- Commit messages: `m<N>: <what changed>`.

## Definition of done
Import movies, stills and image sequences; cut them together on A/B with transitions; superimpose a keyed clip with a fade; add filters, a title and a motion path; change a clip's speed; undo the last action; save and reopen the project; render and play the work area with sound; export a movie, an EDL and a Filmstrip.

## Session log
- Session 1 (2026-10-20): plan written. Milestone 1: CMake scaffold, vendored stb/miniaudio/json/fonts, fixtures script (lossless QuickTime Animation and PCM, plus an H.264 file for seeking), core media and bin, menu bar, Project, Clip and Settings windows. 24 unit tests, 25 self-test checks; screenshots inspected.
- Session 1 (2026-10-20), milestone 2: timeline model and commands, renderer, project files, Construction and Preview windows. 40 unit test cases, 50 self-test checks; screenshots inspected.
- Session 1 (2026-10-20), milestone 3: 24 transitions with goldens and exact endpoint tests, transition commands and rendering, Transitions window, settings dialog. 49 unit test cases, 64 self-test checks; screenshots and the golden contact sheet inspected.
- Session 1 (2026-10-20), milestone 4: keys with goldens, fades, Super rendering, Transparency dialog, rubber bands. 56 unit test cases, 78 self-test checks; screenshots and key goldens inspected.
