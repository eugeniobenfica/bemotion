#pragma once

// The app's own window frame for its dialogs and secondary windows (Settings, Help, Export,
// message boxes...): no system title bar with its minimize and maximize buttons, but a header
// drawn like the rest of bemotion, with the title and a close button. Dragging the header
// moves the window; dragging an edge resizes it (when it can be resized). Installed once for
// the whole application; a top-level QDialog, or a window with the property "chrome" set to
// true, gets it when it is first shown.

namespace bemotion::ui {

void installWindowChrome();

} // namespace bemotion::ui
