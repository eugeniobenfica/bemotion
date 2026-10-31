#pragma once

// Official plugins are not part of the packages (docs/PLUGINS.md): a project that uses one this
// installation does not have offers it, and it comes from bemotion's GitHub repository, from the
// tag of this version (app/Downloads.h), only when the user clicks.

#include <string>
#include <vector>

class QWidget;

namespace bemotion {

struct Composition;

namespace ui {

// The plugins (ids) the composition's objects come from that are not installed here.
std::vector<std::string> missingPlugins(const Composition& comp);

// Asks whether to download `ids` (when `ask`), then downloads and installs those the official
// catalog has, with a progress window; says what could not be installed.
void installOfficialPlugins(QWidget* parent, std::vector<std::string> ids, bool ask);

} // namespace ui
} // namespace bemotion
