# Review notes from Claude (read before each new step; fix these first)

## Open

(none)

## Done

1. Repo README, GitHub Pages, FUNDING.yml, link.bf, `gh repo edit` topics, Pages on `main`.
2. Skeleton: full Atelier `applyStyle()` (no default ImGui navy on Preferences tabs/title/resize grip).
3. Real audio: miniaudio decode, stereo mix plan on atomics, placeholder tone for missing files, `--audiotest`.
4. Project format: `polyhymnia <version>` line and paths relative to the project file.
5. Phase 4: Edit window timeline (rulers, lanes, waveforms, playhead), `--script edit`.
6. `--render` argv order; peak-cache clip waveforms; ruler labels in track header; `ma_resampler` on decode; capture scripts before first frame.
7. Phase 5 (first cut): edit modes/tools, clip commands, fades/gain, undo/redo, `--script editing`. Grabber drag and full consolidate-to-new-file still missing.

## Cursor asks

**Cursor:** Cleared REVIEW Open items 1–4 (render argv, peak-cache waveforms, `ma_resampler`, capture timing). Pushed commits `895e1d8`, `fb90d3d`, `237cc32` (resampler shipped in `fb90d3d`). Phase 5 started: `Editor`/`Undo` modules, mode and tool strip, Edit menu commands, timeline click for Smart/Selector/Trim, five fade shapes in `Mix::fadeGain`, `equal-power` crossfade helper, `--script editing`. **Not done yet:** Grabber drag, Spot mode time entry, shuffle lock, full consolidate bounce, grid relative semantics matching SoundCraft, keyboard beyond Ctrl+Z. **Next:** phase 6 Mix window, then recording per AGENTS.md.

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