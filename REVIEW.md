# Review notes from Claude (read before each new step; fix these first)

## Open


0b. **The UI layout is wrong: port SoundCraft's layout, not an invented one.** Look at `D:\cc\_src\soundcraft\docs\images\edit.png`
   (also `mix.png`, `midi.png`, `automation.png`) beside your own screenshot. Ours has a session-info panel on the left,
   bare lanes, Play/Loop at the bottom and only File/Help menus. Port the Edit window from `crates/ui-egui/src/toolbar.rs`,
   `edit_window/*.rs` (layout, header, tracks, rulers, clips, overlay, scroll) and `panels/*.rs` (tracks, clips, transport,
   memory, history, midi_editor):
   - **The full menu bar**: File, Edit, View, Track, Clip, Event, AudioSuite, Options, Setup, Window.
   - **The toolbar across the top**: the Shuffle / Spot / Slip / Grid mode buttons; zoom buttons and presets 1-5; the Edit
     tools (Zoomer, Trim, Selector, Grabber, Scrubber, Pencil, Smart) with icons; the main counter; Start / End / Length;
     Grid and Nudge values; the transport (loop, stop, play, record, rewind, forward, start, end); count-in, meter, tempo.
   - **Tracks list and Groups** on the left; **Clips list** on the right.
   - **Rulers**: Bars|Beats, Min:Secs, Tempo, Meter and Markers, each labelled at the left.
   - **Track headers**: name, record, input, solo, mute, view selector (waveform / notes / volume), automation mode
     (read...), volume and pan readouts, inserts, a level meter, the track's colour bar. The track type (INST, AUX, MASTER).
   - **Clips** in the track colour with their name bar and real waveforms (MIDI clips show notes).
   - **Bottom tabs**: MIDI Editor, Memory Locations, Undo History; a status bar (session, sample rate, bit depth, track count).
   - The Mix window as `mix.png`, when phase 6 comes.
   Keep Atelier's colours for the chrome (greys, `#007bb7` controls, solid, no gradients or shadows); clip and track
   colours are the user's content and stay coloured. Every panel resizable. Screenshot it beside `edit.png` before
   committing.

11. **Fades dialog** (UI): engine `create_fades` / `delete_fades` and targeted fades via `engine_fix` are ported; no
   standalone Fades dialog window like SoundCraft's yet (curve editor / five shapes UI).

## Done

1. Repo README, GitHub Pages, FUNDING.yml, link.bf, `gh repo edit` topics, Pages on `main`.
2. Skeleton: full Atelier `applyStyle()` (no default ImGui navy on Preferences tabs/title/resize grip).
3. Real audio: miniaudio decode, stereo mix plan on atomics, placeholder tone for missing files, `--audiotest`.
4. Project format: `polyhymnia <version>` line and paths relative to the project file.
5. Phase 4: Edit window timeline (rulers, lanes, waveforms, playhead), `--script edit`.
6. `--render` argv order; peak-cache clip waveforms; ruler labels in track header; `ma_resampler` on decode; capture scripts before first frame.
7. Phase 5 first cut: modes/tools/menu wired. Reopened: not a port, see Open.
8. Phase 5 engine port: `Polyhymnia.exe --selftest` 105/105 (edit/tests.rs, cmd/edit*, cmd/clip*, grid.rs, clip.rs);
   `engine_edit.cpp` from `edit.rs`; `consolidate.cpp` from `io/render.rs consolidate`; Spot dialog, `edit_drag`, Smart
   zones; Editor routes through EngEdit; `--script editing` and `spot_test` pass.

## Cursor asks

**Cursor (phase 5 vs SoundCraft, file by file):** **Engine/edit:** `engine_edit.cpp` ports `edit.rs` public API and
shuffle/automation-follow; MIDI in `consolidate_midi` and `split_clip` MIDI branches are stubs. **Cmd:** `engine_fix.cpp`
covers the commands exercised by the 54 cmd tests, not the full `cmd/*.rs` surface. **Model/session:** clips carry
`numId`, `loopId`, MIDI payload, effect keys for tests; not full SoundCraft track kinds / playlists / video. **Time:**
`time_grid.cpp` ports `grid.rs` test-covered grid/nudge; clip/markers grid via session helpers. **UI:** `edit.cpp` +
`edit_drag` + `spot_dialog` port core interaction from `drag.rs`, `input.rs`, `spot.rs`; **0b** full Edit chrome
(toolbar icons, Clips list, dual counters, bottom tabs, track headers) not started. **Mix:** offline consolidate renders
mono WAV beside project; no clip-gain render / commit track from `render.rs` yet. **Undo:** `App::mutateDocument` +
engine undo for cmd tests; not every EngEdit menu path verified as one undo step with automation. **Open:** REVIEW **0b**
layout port; **Fades dialog** UI (item 11). **Next:** finish 0b + fades dialog, then phase 6 Mix window with tests.

- f9c21aa Skeleton: window, splash, languages, device picker, build/install scripts. Runs.
- 6923495 Session model: tracks, clips, markers, tempo, meter, .polyhymnia save/load. Reviewed, OK.
- 081c5f7 Atelier ImGui colours everywhere. OK.
- c0e6aa8 Real audio + audiotest. OK.
- 165de28 Project version and relative paths. OK.
- 899b64d README and GitHub Pages. OK.
- fb2b157 Phase 4 Edit window. OK.
- 895e1d8 `--render` argv order. OK.
- fb90d3d Peak-cache waveforms + ruler labels (+ `ma_resampler` in same commit). OK.
- 237cc32 Capture script before first frame. OK.