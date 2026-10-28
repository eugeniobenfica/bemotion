# bestyle

bemotion's look as a library for any Qt 6 Widgets app in C++: the theme
(palettes and color tokens), the widget style, a vector icon set and the
controls bemotion's panels are made of. bemotion itself is built with it.

It depends on Qt Widgets and nlohmann/json (themes are JSON) and on nothing
of bemotion.

## Use it

```cmake
add_subdirectory(path/to/bestyle)
target_link_libraries(my_app PRIVATE bestyle)
```

```cpp
#include "bestyle/BeStyle.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bestyle::install(bestyle::theme::darkPalette()); // style + palette, once

    auto* opacity = new bestyle::DragValue;           // drag to scrub, click to type
    opacity->setRange(0, 100);
    opacity->setFormat("%.0f%%");
    auto* row = new bestyle::LabeledRow("Opacity", opacity);
    // ...
}
```

`examples/gallery.cpp` shows every control and switches themes while it runs
(`-DBESTYLE_BUILD_EXAMPLES=ON`).

## What is in it

| Header | What |
| --- | --- |
| `BeStyle.h` | `install`, `setTranslator` / `translate`, `Appearance` (the "look changed" signal); includes everything below |
| `Theme.h` | `Palette` (dark, light, midnight, JSON in and out), the tokens `theme::kText`, `kAccent`, `kField`… and `theme::apply` |
| `Style.h` | the `QProxyStyle`: Fusion with outlined check boxes and radio buttons, wheels that do not change unfocused fields |
| `Draw.h` | a small drawing vocabulary over `QPainter` (`Rgba`, `Pt`, lines, rounded rects, arcs, text), always anti-aliased |
| `Icons.h` | about 130 vector icons (`Icon`, `drawIcon`, `makeIcon`, `menuIcon`); no files to ship |
| `Widgets.h` | `IconButton`, `Tile`, `DragValue`, `Switch`, `segmented`, `FlowLayout`, `ElidedLabel`, `ResponsiveGrid`, `LabeledRow`, `CategoryHeader` / `CategoryBody`, `ChoiceRow`, `setTone`, `sectionLabel`, `accentButton` |
| `Metrics.h` | spacing and sizes (a 4 px step, panel margins, row and button heights) |

## Rules that keep it looking right

- **Colors are tokens read at paint time** (`theme::kText`, not `#E8E8EC`):
  themes change while the app runs. A label is colored with
  `setTone(label, Tone::TextDim)`, never a style sheet with a baked color.
- **Text goes through `translate`**: an app with translations calls
  `setTranslator` once; the controls get the English text.
- **After changing fonts or sizes, emit `Appearance::get().changed()`**
  (`theme::apply` already does): toned labels and custom-drawn widgets follow.
- **Spacing in steps of `metrics::kStep`** (4 px).
- `Icon::Logo` is your app's mark: `setLogoPainter` (a disc until then).
- `theme::dockStyleSheet()` styles Qt Advanced Docking System panels, for an
  app that docks with it.

## Shared between projects

bestyle lives in [its own repository](https://github.com/eugeniobenfica/bestyle) and each
app (bemotion, Artisan…) carries it as a git subtree in `libs/bestyle`: the code
is in the app's repository, so building needs nothing extra. Change it in any
app, commit, then `tools/bestyle.sh push`; in the other apps
`tools/bestyle.sh pull`. `tools/bestyle.sh status` shows what differs.

A new app takes it with

```bash
git subtree add --prefix libs/bestyle https://github.com/eugeniobenfica/bestyle.git main --squash
```

and copies `tools/bestyle.sh` from one of the others.
