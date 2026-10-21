# Feature Plan

Scope: a video editor modeled on the feature set of Adobe Premiere **1.0 (Mac, Dec 1991)** through **2.0 (Mac, Sep 1992)**. Nothing from 3.0 or later is in scope.

Each feature is tagged with the version it comes from, **[1.0]** or **[2.0]**, and with a confidence level:

- ✅ **Sourced**: documented in at least one history source.
- ⚠️ **Assumed**: likely based on later versions or the Windows 1.0 release, but not confirmed for Mac 1.0/2.0.
- 🔁 **Modernized**: the original idea, adapted to today's formats or hardware.

---

## Milestone 1: Premiere 1.0 baseline

### Project window
- [x] Import media into a project bin ✅ [1.0]
- [x] Bin list shows each clip's thumbnail, name, duration and type ⚠️ [1.0]
- [x] Save and open project files ⚠️ [1.0]

### Clip window
- [x] Open a clip in its own window to play and scrub it ⚠️ [1.0]
- [x] Set in and out points on a clip before placing it ⚠️ [1.0]

### Construction window (the timeline)
Track layout follows Windows 1.0, which is the only documented early layout ⚠️:

| Track | Purpose |
|---|---|
| Video A | Main video track |
| Transition (T) | Holds transitions between A and B |
| Video B | Second video track for A/B editing |
| Superimpose (S) | Overlay track composited above A/B |
| Audio A, B, C | Three audio tracks |

- [x] Drag clips from the bin onto tracks ✅ [1.0]
- [x] Move clips by dragging, and trim them by dragging their edges ⚠️ [1.0]
- [x] A/B editing: clips on A and B overlap, and a transition on the T track blends them ✅ [1.0]
- [x] Timeline zoom (time-unit scale) ⚠️ [1.0]
- [x] Opacity and volume set with "rubber band" lines drawn on the track ⚠️ [1.0]
- [x] Work area bar marking the range to preview or render ⚠️ [1.0]

### Superimpose and transparency
- [x] Composite the S track over the A/B result ✅ [1.0]
- [x] Transparency keys for the S track: None, Alpha Channel, Chroma, Luminance, Blue Screen, Green Screen, Multiply, Screen ⚠️ [1.0]

### Transitions
- [x] 24 transitions ✅ (the count is sourced, the names are not)
- [x] Settings for each transition: direction, border and start/end percentages ⚠️
- [x] The set built, until the real list is confirmed (names from later versions; the 3D ones, such as Page Peel and Spin, were left out):
  Cross Dissolve, Additive Dissolve, Dither Dissolve, Non-Additive Dissolve, Wipe, Barn Doors, Band Wipe, Checkerboard, Clock Wipe, Venetian Blinds, Random Blocks, Inset, Iris Round, Iris Square, Iris Diamond, Iris Cross, Push, Slide, Split, Center Split, Center Merge, Swap, Zoom, Stretch

### Filters
- [x] Apply video filters to a clip ✅ [1.0]: 24 built (see the 2.0 section for the list)
- [x] Filter settings use **a start value and an end value only**, with the values blended across the clip. There are no mid-clip keyframes. ⚠️
- [ ] Plug-in interface for filters (the original was compatible with some Photoshop filters) ✅ [1.0] 🔁 *Not built: filters are compiled in, through one registry that a plug-in loader could add to later.*

### Preview and rendering
- [x] Render a preview of the work area, then play it back ✅ [1.0]
- [x] Output frame sizes: authentic presets at **160×120 (NTSC)** and **192×144 (PAL)** ✅, plus modern presets 🔁

### Audio
- [x] 8-bit audio playback and export, matching 1.0 ✅ [1.0]

### Import and export
- [x] Import video files 🔁 (originally QuickTime and VideoSpigot formats ✅)
- [x] Import still images 🔁 (originally PICT ✅)
- [x] Export the finished movie to a video file 🔁 (originally a QuickTime movie)
- [x] "Output to videotape" becomes full-screen playback of the program 🔁 [1.0]

---

## Milestone 2: Premiere 2.0 additions

### New windows
- [x] **Sequence window**: arrange clips in a simple ordered list for a rough cut, then send them to the Construction window ✅ [2.0]
- [x] **Title window**: create text titles to use as clips ✅ [2.0]
- [ ] **Capture window**: record from a camera or microphone 🔁 (originally QuickTime video and audio capture ✅ [2.0])

### Titles
- [x] Text in a chosen font, size, color and alignment ⚠️ (Roboto Regular and Bold, with an optional shadow)
- [x] Simple shapes and a background color or a transparent background ⚠️ (rectangles and ovals, filled or outlined)
- [ ] Import text and graphics from vector files 🔁 (originally imported from Illustrator ✅ [2.0]) *Not built.*

### Motion
- [ ] Animate a clip's position along a path made of keyframe points ✅ [2.0]
- [ ] Rotation and scale on those motion points ✅ [2.0]
- [ ] Linear interpolation only. Easing came in 3.0 and is out of scope.

### Speed
- [ ] Change clip speed for slow and fast motion ✅ [2.0]

### Audio upgrades
- [x] 5 audio filters ✅ (the count is sourced, the names are not): Echo, Pan, Backwards, Fill Left, Fill Right
- [x] 16-bit, 44.1 kHz sound is a project setting alongside 1.0's 8-bit

### Filters and effects expansion
- [ ] 41 video filters ✅ (the count is sourced, the names are not). *24 built so far:* Brightness & Contrast, Levels, Gamma Correction, Hue & Saturation, Color Balance, Tint, Black & White, Invert, Posterize, Threshold, Solarize, Gaussian Blur, Camera Blur, Motion Blur, Sharpen, Emboss, Find Edges, Mosaic, Add Noise, Ripple, Replicate, Crop, Horizontal Flip, Vertical Flip
- [ ] 49 "special effects", probably the expanded transition set ⚠️

### Timecode and EDL
- [x] SMPTE timecode display (`HH:MM:SS:FF`) throughout the interface ✅ [2.0]
- [x] Drop-frame timecode for 29.97 fps (`HH:MM:SS;FF`) and non-drop-frame timecode ⚠️
- [ ] EDL export of the timeline 🔁 (CMX 3600 format, which modern editors read) ✅ [2.0]

### New formats
- [ ] Import numbered image sequences (`frame001.png`, …) as one clip 🔁 (originally numbered PICT sequences ✅ [2.0])
- [ ] **Filmstrip export and import**: a clip exported as one tall image of stacked, timecode-labeled frames that can be edited in an image editor and imported back as video 🔁 (originally `.flm` ✅ [2.0])

---

## Out of scope (3.0 and later)
- More than the fixed track layout (3.0 allowed up to 99 tracks)
- Real-time or full-frame-rate preview (3.0)
- Waveform monitor, batch capture, capture calibration (3.0)
- Motion easing, 3D distortion, sub-pixel motion, field rendering (3.0)
- Trim window (4.0)
- Source/Program dual monitor, three- and four-point editing, multiple filter keyframes (5.0)

---

## To verify
- [ ] Install **Premiere 2.0.1** from [Macintosh Repository](https://www.macintoshrepository.org/167-adobe-premiere-2-0) in a classic Mac emulator ([Infinite Mac](https://infinitemac.org) or Basilisk II), then:
  - [ ] Record the real transition, video filter and audio filter lists and replace the placeholders above
  - [ ] Confirm the track layout and the transparency key types
  - [ ] Take screenshots of each window for UI reference

## Decided
- Desktop app, following the sister repo `mock-photoshop` (C++17 headless core, thin NanoGUI front end).

## Open questions
- How faithful should the UI be: a period-accurate System 7 look, or a modern interface with the same feature set?
