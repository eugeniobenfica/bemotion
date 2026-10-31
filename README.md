<div align="center">

<img src="assets/branding/icon.svg" width="112" alt="bemotion icon">

# bemotion

**Video editing with 2D animation at its heart.**<br>
A free, open source video editor that stays calm, fast, and friendly to people and robots alike.

[![Build](https://github.com/eugeniobenfica/bemotion/actions/workflows/build.yml/badge.svg)](https://github.com/eugeniobenfica/bemotion/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-E8B931?labelColor=141417)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-E8B931?labelColor=141417)
![Platforms](https://img.shields.io/badge/platforms-Windows%20·%20Linux%20·%20macOS-E8B931?labelColor=141417)
[![Buy Me a Coffee](https://img.shields.io/badge/support-Buy%20Me%20a%20Coffee-E8B931?labelColor=141417&logo=buymeacoffee&logoColor=E8B931)](https://buymeacoffee.com/eugeniobenfica)

<img src="docs/media/intro.gif" width="720" alt="The bemotion intro animation">

<sub>The intro above is an ordinary bemotion project — shapes, two texts and a few expressions —<br>
open <b>Welcome to bemotion</b> from the home screen and take it apart.</sub>

[The idea](#the-idea) · [Features](#features) · [User guide](docs/USER-GUIDE.md) · [Download](#download-windows) · [Getting started](#getting-started) · [Plugins](docs/PLUGINS.md) · [Contributing](CONTRIBUTING.md) · [Support ☕](#support-bemotion)

</div>

---

bemotion is a video editor in the spirit of Premiere — import footage, cut,
arrange, mix and export — with a special focus on 2D animation: every property
keyframable, a graph editor, motion paths, effects, text and shapes that move
like they would in After Effects. Drop something on the timeline, animate it,
press play.

> 🇧🇷 **Resumo:** bemotion é um editor de vídeo gratuito e de código aberto, no
> espírito do Premiere, com foco especial em animação 2D. Sem cara de cabine de
> avião: rápido, livre e amigável para pessoas e para robôs (scripts e IAs). Um
> núcleo pequeno que funciona como "engine" e como app simples — você baixa o
> essencial e instala os plugins de que precisa. bemotion é mais uma ideia do
> que uma tecnologia: não se prende a linguagens nem frameworks e foi feito para
> ser modificado e reescrito.

<p align="center">
  <img src="docs/media/editor.png" width="100%" alt="The bemotion editor: viewport, properties and timeline">
</p>

## The idea

- **A video editor first, an animator at heart.** Cut, arrange and mix footage
  like in Premiere; animate anything like in After Effects — in one place.
- **Not an airplane cockpit.** What you need is in front of you and the rest
  stays out of the way. Panels, toolbar, shortcuts and colors are yours to
  arrange.
- **Human and robot friendly.** The same project can be edited by hand, by
  scripts and by AI assistants — through the same rules, with the same undo.
  Operations, a command line and an [MCP server](docs/AUTOMATION.md) are part
  of the core, not an afterthought.
- **Fast and light.** Starts in under a second, plays back in real time, runs
  on any 64-bit PC.
- **An engine and a basic app.** A small core you can drive headless — render,
  export, edit from scripts — and a simple editor on top. Download the
  essentials; add what you need as [plugins](docs/PLUGINS.md), and turn off
  the built-in extras you never use (speech to text, tracking, Puppet...).
- **Free.** No price, no account, no subscription, no watermark. Open source
  under the MIT license — kept that way by [people who support it](#support-bemotion).
- **An idea more than a technology.** bemotion is defined by its philosophy, its
  file format, its [operations](docs/OPERATIONS.md) and its
  [architecture](docs/ARCHITECTURE.md) — not by a language or a framework. It is
  built to be modified and, when it makes sense, rewritten (it already moved from
  ImGui / OpenGL to Qt / Skia once). It will be kept going for as long as this
  philosophy is useful.

## Support bemotion

bemotion is free, and it will stay free. No ads, no paid tier, no features held
back, no data collected. That only works because it does not answer to
investors or a pricing page. It answers to the people who use it.

It is built by an independent developer, in hours that could have gone to paid
work. Each contribution turns into more of those hours: a feature finished
sooner, a bug fixed instead of postponed, a release tested on one more machine.
Everyone who opens bemotion gets the result, including the student, the small
creator and the person who could never pay for a video editor.

If bemotion saved you time, taught you something or just made editing feel
lighter, a coffee keeps it moving:

<p align="center">
<a href="https://buymeacoffee.com/eugeniobenfica"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" height="52" alt="Buy Me a Coffee"></a>
</p>

Not able to give money? That counts too. Star the repository, share something
you made with bemotion, report a bug, or [contribute](CONTRIBUTING.md) code,
plugins, presets or translations. A free tool lives as long as its community
does.

> 🇧🇷 **Apoie:** o bemotion é e continuará gratuito, sem anúncios nem versão
> paga, porque é mantido por quem usa. Cada café no
> [Buy Me a Coffee](https://buymeacoffee.com/eugeniobenfica) vira mais horas de desenvolvimento,
> com recursos mais cedo e menos bugs, para todo mundo. Também ajuda dar uma
> estrela, compartilhar e contribuir.

## Features

- **Edit like Premiere**: multi-track timeline, ripple / roll / slip, J K L,
  speed, time remap and freeze frame, markers, proxies for big footage
- **Animate like After Effects**: every property keyframable, a graph editor,
  motion paths, expressions, puppet pins and mesh warp, point tracking, 3D
  layers and a camera
- **Shapes, text and subtitles**: a Pen tool, gradients, masks, track mattes
  and 17 blending modes, letter animations, counters, `.srt` captions and
  speech to text
- **One click motion**: Quick Animations and transitions as tiles you click or
  drag onto a clip, and they stay plain keyframes
- **Color and sound**: a Color panel with LUTs and curves, chroma key, scopes;
  EQ, noise reduction, ducking and silence removal
- **Export** MP4, ProRes 4444 with transparency, WebM, GIF, PNG sequences and
  Lottie JSON
- **Yours**: Friendly, Balanced or Expert, themes, dockable panels, your own
  toolbar and shortcuts, English and Portuguese
- **Small core, optional extras**: official plugins (charts and maps, 3D
  models, a voice recorder) and the speech model are downloaded only when you
  ask, from *Settings › Plugins*
- **Robot friendly**: the command line, Lua scripts and an MCP server edit
  projects through the same checked, undoable [operations](docs/OPERATIONS.md)

Every feature, with how to use it, is in the **[user guide](docs/USER-GUIDE.md)**.

## Download (Windows)

Grab `bemotion-<version>-setup.exe` or the portable zip from the
[**latest release**](https://github.com/eugeniobenfica/bemotion/releases)
(the first ones are **betas**: everything works, rough edges included — please
[report](https://github.com/eugeniobenfica/bemotion/issues) what you find), or
from the latest successful
[build](https://github.com/eugeniobenfica/bemotion/actions/workflows/build.yml)
(*Artifacts › bemotion-windows*), or build it yourself (below).

- **Installer**: installs for your user only (no administrator rights), adds a
  Start Menu shortcut (desktop shortcut optional), opens `.bemotion` files with a
  double-click and can be removed from *Settings › Apps*. Your settings and
  projects stay when you uninstall.
- **Portable zip**: unzip anywhere (even a USB stick) and run `bemotion.exe`.
- **Updates**: off until you turn on *Settings › Interface › Updates*; then
  bemotion looks for a new version on GitHub once a day and offers it in a
  corner of the window. One click downloads it, checks it against the
  release's `SHA256SUMS.txt` and installs it (the installed version; the
  portable zip opens the release page).

Runs on 64-bit Windows 10 (1809 or later) and Windows 11, on any x86-64 CPU —
optimized code paths for newer processors are picked at runtime. The files are
not signed yet, so SmartScreen may warn on first launch (*More info › Run anyway*).
If something goes wrong, `%APPDATA%\bemotion\bemotion.log` says what happened.
Turn on *Settings › Interface › Crash reports* and a crash leaves a report in
`%APPDATA%\bemotion\crashes` (kept on your computer, never sent) to attach to
a bug report.

### Running from the sources

Double-click **`run.bat`**: it rebuilds only what changed (seconds; the first
time it configures and builds everything) and opens bemotion. `run.bat
--no-build` just opens the last build, `run.bat project.bemotion` opens a
project, `run.bat --build-only` only builds.

### Building the installer yourself

With MSYS2 set up as in *Getting started* (below) and Skia built once,
double-click **`build-release.bat`** — or, in an MSYS2 UCRT64 shell:

```bash
tools/build-release.sh
```

It builds a Release in `build-release/`, runs the tests, packages and checks the
package; the installer and the portable zip land in `dist/`.

## Getting started

bemotion is built with CMake and needs a few system libraries — **Qt 6** (≥ 6.4),
**FFmpeg** (libavformat, libavcodec, libavfilter, libswscale, libswresample),
HarfBuzz, ICU and the usual image libraries — plus **Skia**, which
`tools/skia/build-skia.sh` builds once from a pinned revision into
`third_party/skia` (it takes a while). The rest (Qt Advanced Docking System,
nlohmann/json, stb, Lua, miniaudio) is fetched on the first configure.

**Windows** — in an [MSYS2](https://www.msys2.org) **UCRT64** shell:

```bash
pacman -S --needed git patch mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,python,gn}   mingw-w64-ucrt-x86_64-{qt6-base,qt6-svg,qt6-tools,ffmpeg,whisper.cpp,harfbuzz,icu,libjpeg-turbo,libwebp,libpng,expat,zlib,freetype}
tools/skia/fetch-skia.sh || tools/skia/build-skia.sh   # Skia: the prebuilt one, or build it (~1 h)
cmake --preset dev                            # build/ (CMakePresets.json; -DSKIA_ROOT=... if Skia is elsewhere)
cmake --build build
./build/bemotion.exe
tools/package-windows.sh build dist           # installer + portable zip (needs: pacman -S mingw-w64-ucrt-x86_64-nsis)
```

**Linux** (Ubuntu 24.04):

```bash
sudo apt install ninja-build clang python3 pkg-config qt6-base-dev qt6-base-private-dev qt6-base-dev-tools qt6-qpa-plugins libqt6svg6-dev libgl-dev libxkbcommon-dev   libavcodec-dev libavformat-dev libavfilter-dev libswscale-dev libswresample-dev libavutil-dev   libharfbuzz-dev libicu-dev libfreetype-dev libfontconfig-dev libpng-dev libjpeg-dev libwebp-dev libexpat1-dev zlib1g-dev
tools/skia/fetch-skia.sh || tools/skia/build-skia.sh
cmake --preset dev && cmake --build build
./build/bemotion
```

**macOS** (Homebrew):

```bash
brew install ninja pkgconf qt ffmpeg harfbuzz icu4c freetype fontconfig libpng jpeg-turbo webp
export PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig:$(brew --prefix jpeg-turbo)/lib/pkgconfig"
tools/skia/fetch-skia.sh || tools/skia/build-skia.sh
cmake --preset dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build && ./build/bemotion
```

### Speech to text on Linux and macOS

Speech recognition is [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
linked into bemotion. MSYS2 has a package for it (Windows, above); Ubuntu has
none, so bemotion can build the same pinned version (1.9.5) itself — linked in,
nothing to install, the CPU code built for any x86-64 with AVX2, Metal on Apple
silicon:

```bash
cmake --preset dev -DBEMOTION_FETCH_WHISPER=ON   # (+ -DCMAKE_PREFIX_PATH=... on macOS)
cmake --build build
```

On the GPU on Linux, add `-DGGML_VULKAN=ON` (needs the Vulkan SDK). On macOS,
`brew install whisper.cpp` works too (found through pkg-config, no option
needed). The CI builds Linux and macOS this way. The model: run
`tools/fetch-speech-model.sh` once and point `BEMOTION_MODELS` at the folder,
or let bemotion download it into its app data the first time you transcribe.
Packages leave the model and the official plugins out;
`BEMOTION_BUNDLE_MODELS=1` and `BEMOTION_BUNDLE_PLUGINS="all"` (or a list of
ids) put them inside, for offline installs.

Run the unit tests with `ctest --test-dir build`. A Skia built elsewhere can be
used with `-DSKIA_ROOT=<dir>`.

### Rendering from the command line

```bash
bemotion assets/welcome/Welcome.bemotion --export welcome.mp4
```

More in the [user guide](docs/USER-GUIDE.md#rendering-from-the-command-line)
and [docs/AUTOMATION.md](docs/AUTOMATION.md).

## Keyboard shortcuts

Every shortcut can be changed in *Settings › Shortcuts*; *Help* (F1) shows the
current ones. The defaults are listed in the
[user guide](docs/USER-GUIDE.md#keyboard-shortcuts).

## Project layout

```
src/core     document model (layers, keyframes, easing, file format, undo) — no UI code
src/render   draws compositions with Skia (viewport, thumbnails, export), text shaping, the logo mark
src/export   video / image-sequence encoders and the export job
src/media    imported media: image / audio / video decoding (FFmpeg), audio playback and mixing
src/ui       Qt interface: theme, widgets, intro, home screen, editor window, dock panels, dialogs
src/app      application window, OS integration, settings, preset/theme libraries, plugin host
libs/bestyle the look (theme, widget style, drawing, icons, shared controls), reusable by other Qt apps
assets       fonts, official plugins (downloaded, not packaged), the Welcome project and the brand files
tools        Skia build, packaging (Windows, Linux, macOS), release, smoke test and language scripts
tests        unit tests for the core
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the pieces fit
together and [CONTRIBUTING.md](CONTRIBUTING.md) to get involved.

## Brand

The mark is a disc with a lowercase **b** cut out of it, in the accent gold
(`#E8B931`) on the interface's near-black (`#141417`). Its geometry lives in
[`src/render/LogoMark.h`](src/render/LogoMark.h) and every brand file is drawn
from it — regenerate them with `bemotion --write-icon <file.ico|png|svg>` and
`bemotion --write-mark <file.svg>`.

| | File |
| --- | --- |
| App icon | [`assets/branding/icon.svg`](assets/branding/icon.svg) · [`.ico`](assets/branding/bemotion.ico) · [`1024 px .png`](assets/branding/icon-1024.png) |
| Mark only | [`assets/branding/mark.svg`](assets/branding/mark.svg) |
| Intro / Welcome project | [`assets/welcome/Welcome.bemotion`](assets/welcome/Welcome.bemotion) |
| Social preview | [`docs/media/social-preview.png`](docs/media/social-preview.png) |

## License

bemotion is released under the [MIT License](LICENSE). The bundled Inter font is
licensed under the [SIL Open Font License](assets/fonts/Inter-OFL.txt).

<div align="center">
<sub>Made with care — and animated with bemotion.</sub>
</div>
