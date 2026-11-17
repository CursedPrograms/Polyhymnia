[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

<br>
<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" style="width: 20%;"/>
  </a>
</div>
<br>
<div align="center">
  <img alt="C++" src="https://img.shields.io/badge/c++%20-%23323330.svg?&style=for-the-badge&logo=c%2B%2B&logoColor=white"/>
  <img alt="CMake" src="https://img.shields.io/badge/cmake%20-%23323330.svg?&style=for-the-badge&logo=cmake&logoColor=white"/>
  <img alt="OpenGL" src="https://img.shields.io/badge/opengl%20-%23323330.svg?&style=for-the-badge&logo=opengl&logoColor=white"/>
  <img alt="SDL2" src="https://img.shields.io/badge/sdl2%20-%23323330.svg?&style=for-the-badge&logo=sdl&logoColor=white"/>
</div>
<div align="center">
  <img alt="PowerShell" src="https://img.shields.io/badge/PowerShell-%23323330.svg?&style=for-the-badge&logo=powershell&logoColor=white"/>
  <img alt="Batch" src="https://img.shields.io/badge/Batch-%23323330.svg?&style=for-the-badge&logo=windows&logoColor=white"/>
</div>
<br>

# Polyhymnia

**Music studio (DAW) for arranging and mixing.** Polyhymnia is Atelier's Pro Tools-style editor: an Edit window with
tracks, clips and waveforms, and a Mix window on the way. Part of
**[Atelier](https://github.com/CursedPrograms/Atelier)**. C++ port of ideas from
**[SoundCraft](https://github.com/CursedPrograms/soundcraft)** (Rust, MIT OR Apache-2.0).
**[Website](https://cursedprograms.github.io/Polyhymnia/)**

![Polyhymnia Edit window: dual rulers, track lanes and clip waveforms](images/window.jpg)

## What works so far

| Phase | Feature |
| --- | --- |
| 1 | CMake skeleton, SDL2 + OpenGL 3 + Dear ImGui, splash, 12 languages, miniaudio output device and Preferences picker |
| 2 | Session model: tracks, clips, markers, tempo and meter, `.polyhymnia` save/load |
| 3 | Playback and mix: gain, pan, mute, solo, real WAV/FLAC/MP3 decode, offline `--render`, `--audiotest` |
| 4 | Edit window: bars/beats and min:sec rulers, track lanes, clip waveforms, playhead, zoom and scroll |

## Minimum requirements

| | |
| --- | --- |
| **System** | Windows 10 or 11, 64-bit |
| **Processor** | Any 64-bit CPU |
| **Memory** | 4 GB RAM |
| **Graphics** | OpenGL 3.3 |
| **Disk** | 50 MB for the app |

## Build and run

```
cd cpp
build.bat
build\Release\Polyhymnia.exe
```

Test hooks (screenshot + log line):

```
Polyhymnia.exe --script skeleton --capture shot.bmp
Polyhymnia.exe --script session --capture shot.bmp
Polyhymnia.exe --script playback --capture shot.bmp
Polyhymnia.exe --script edit --capture shot.bmp
Polyhymnia.exe --audiotest build\Release\
Polyhymnia.exe --render project.polyhymnia out.wav
```

Install shortcuts: `install.bat` beside the built tree (copies to `dist\Polyhymnia`).

## Credits

- **[SoundCraft](https://github.com/CursedPrograms/soundcraft)** — session model, mix and UI ideas (MIT OR Apache-2.0)
- **[Dear ImGui](https://github.com/ocornut/imgui)** — UI (MIT)
- **[SDL2](https://www.libsdl.org/)** — window and input (zlib)
- **[miniaudio](https://github.com/mackron/miniaudio)** — device and decode (public domain / MIT)

---

<div align="center">
  © Cursed Entertainment 2026
</div>
<div align="center">
  <a href="https://cursed-entertainment.itch.io/">
    <img src="https://raw.githubusercontent.com/CursedPrograms/cursedentertainment/main/images/logos/logo-wide-grey.png" alt="Cursed Entertainment on itch.io" width="250"/>
  </a>
</div>