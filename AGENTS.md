# Polyhymnia: notes for coding agents

Polyhymnia is Atelier's music studio (a DAW): a C++ port of **SoundCraft** (`D:\cc\_src\soundcraft`, Rust, MIT OR
Apache-2.0, so its code and ideas may be ported; credit it in comments as "after SoundCraft (crates/...)"). It works
like Pro Tools: an Edit window (tracks, clips, waveforms, the transport) and a Mix window (faders, pan, sends,
plugins), recording, MIDI and bouncing. It's named after the Greek muse of hymns, beside Atelier's song maker Hymnal:
Hymnal makes songs, Polyhymnia arranges and mixes them.

Read SoundCraft's own `AGENTS.md`, `README.md`, `ROADMAP.md` and `docs/` first: they describe what every part does.

## The stack (as the other Atelier apps)

C++17, CMake, SDL2 + OpenGL 3 + Dear ImGui (the window), **miniaudio** (the audio device, and decoding WAV, FLAC and
MP3), nlohmann-style JSON or a small own reader for project files. Copy the setup from Atelier apps already here:

- `D:\cc\Mosaic-Mask\cpp`: CMakeLists.txt that fetches SDL2 and Dear ImGui, miniaudio, the splash screen
  (`src/splash.*`), language packs (`src/lang.*`, `lang/`), build.bat / build.ps1 / build.sh and install.bat /
  install.ps1 / install.sh.
- `D:\cc\Chimera\cpp`: a bigger Atelier window: menus, tool strip, panels, undo, `--script NAME --capture shot.bmp`
  test hooks.

The look: Atelier's greys with the Atelier blue `#007bb7` for controls, solid colours, no gradients or shadows,
controls on the left, every setting a slider. 12 languages (English plus ja, zh, es, ko, fr, de, ru, sr, af, Yucatec
Maya, Pennsylvania Dutch), a splash screen, opens empty (Preferences can reopen the last project).

## The phases (do them in order; commit at the end of each step that builds and works)

1. **The skeleton.** CMake project, the window in Atelier's look, splash, language packs, build and install
   scripts, `Polyhymnia.exe`. The audio device opens (miniaudio), with a device picker in Preferences.
2. **The session model** (`crates/model`, `crates/time`): a project of tracks; a track has clips; a clip points at
   an audio source with its start, offset and length, gain and fades; markers; tempo and meter. Saved and opened as
   a `.polyhymnia` project file (text), with the audio files beside it.
3. **Playback** (`crates/playback`, `crates/mix`, `crates/engine`): play, stop, loop, the playhead; tracks mixed
   with gain, pan, mute and solo, on the audio thread, lock-free from the window.
4. **The Edit window** (`crates/ui-egui`): the timeline with rulers (bars and beats, minutes and seconds), tracks
   with their waveforms (peak files cached), clips you can drag, trim and split, zoom and scroll, the transport bar,
   File > Import Audio (or drop files), undo for everything.
5. **Editing**: Shuffle, Slip, Spot and Grid modes; Smart, Selector, Grabber, Trim tools; cut, copy, paste,
   duplicate, separate, heal, consolidate, nudge; fades and crossfades in five shapes; clip gain.
6. **The Mix window**: fader, pan, mute, solo, meters, sends, busses, aux inputs, the master; built-in effects from
   `crates/dsp` (EQ, compressor, reverb, delay...), each setting a slider.
7. **Recording**: arm tracks, input monitoring, punch in and out.
8. **Automation** (`crates/automation`): volume, pan, mute and plugin parameters drawn and recorded.
9. **MIDI** (`crates/midi`): MIDI tracks, a piano roll, the built-in instruments.
10. **Bounce and export**: mixdown to WAV / FLAC / MP3.
11. **Plugins**: VST3 and CLAP hosting (`crates/vst3-host`, `crates/clap-host`).
12. **Video** (`crates/video`): a video track to score to.

## Testing

Every feature gets a test hook: `Polyhymnia.exe --script NAME --capture shot.bmp` (drives the feature as a user
would, prints a line, saves a screenshot and quits), and for audio `--render project.polyhymnia out.wav` renders the
mix offline so it can be checked (length, peak level, that a known tone comes out where expected). Build, run the
test, look at the screenshot, then commit.

## Port it properly (this overrides speed)

Polyhymnia is a port of SoundCraft, not something inspired by it. For every feature:

- **Read the original first.** Open the SoundCraft files that do it and port them function by function: the same logic,
  the same edge cases, the same settings and limits, names in C++ style. Note at each function where it came from:
  `// after SoundCraft (crates/.../file.rs fn_name)`.
- **Port its tests too.** Every `#[test]` in the files you port becomes a C++ test (`cpp/tests`, built by CMake and
  run by `Polyhymnia.exe --selftest`). A feature is done only when the original's tests pass in C++, plus the screenshot
  check. Run all the tests before every commit; never delete or weaken a test to make it pass.
- **Nothing simplified.** No placeholders, stubs, "first cut", "simplified" or "not a full ..." versions. If SoundCraft
  does it, Polyhymnia does it. If something truly can't be ported (a Rust-only dependency, another platform), write why
  under "Cursor asks" in REVIEW.md and port everything around it.
- **Say what was ported.** Each commit message names the SoundCraft files and functions it ports and how many of their
  tests pass (e.g. "edit.rs heal, join_gain_env; 4/4 tests").
- When a phase is done, compare it with SoundCraft file by file and list anything left in REVIEW.md before moving on.

## House rules

- Comments say what and why, in plain words. 4-space indents. A short comment on each function.
- MSVC traps: `near`, `far`, `small`, `min`, `max` are Windows macros (define NOMINMAX; don't use them as names).
- Don't write `\n` inside C++ string literals through a shell heredoc; edit files directly.
- Commit each finished step to `main` with a full-sentence message ending in a blank line and
  `Co-Authored-By: Cursor Agent <noreply@cursor.com>`. Push to GitHub once a phase works
  (`git push -u origin main`; the repo is https://github.com/CursedPrograms/Polyhymnia). If the remote isn't there yet,
  commit and carry on; don't create the repo yourself.
- Don't commit build output (`build/`, `dist/`): add a .gitignore first.
- Read `REVIEW.md` in this folder before each new step: Claude reviews every commit and writes fixes there. Do
  everything under "Open" first.
- If you're blocked, write why at the bottom of REVIEW.md under "Cursor asks" and stop.
- Installer shortcuts (Start Menu, desktop, Linux .desktop Name=) are named "Atelier - <Name>" so the suite sorts together; installing and uninstalling also remove the old plain-named shortcut.

## The repo (once phase 1 works, and kept up to date after each phase)

Make it look like the user's other Atelier repos (copy from `D:\cc\Mosaic-Mask`, `D:\cc\Chimera`, `D:\cc\Arachne`):

- **README.md**: what Polyhymnia is, a screenshot of the window (`images/`), what works so far and what's coming, minimum
  requirements (a table, as Arachne's), how to build and install, credits (after its Craft original, with its
  licence), and the Cursed footer: `---`, then centred "© Cursed Entertainment 2026" and the itch.io logo link
  (`logo-wide-grey.png`, width 250), as in the other READMEs. Never write "our" / "ours".
- **A GitHub Pages site**: `index.html` at the root, `site/style.css`, `site/site.js` (CSS and JS in their own files,
  never inline), using the main CSS from the cursedentertainment repo as the others do, solid colours, no gradients
  or shadows, works on phones, the shared footer (RIFT's `site/footer.*`), a QR code pointing at the page.
- `.github/FUNDING.yml` (copy KIDA-Robot-v01's) and `link.bf` (the same file as the other repos).
- **The description and topics**: set them with `gh repo edit CursedPrograms/Polyhymnia --description "..." --add-topic ...`
  (topics like atelier, cpp, the kind of app, imgui, sdl2, windows) and turn on Pages from main, root
  (`gh api repos/CursedPrograms/Polyhymnia/pages -X POST -f "source[branch]=main" -f "source[path]=/"`).
