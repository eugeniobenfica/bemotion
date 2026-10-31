#pragma once

// Speech to subtitles from the editor (media/Transcriber.h): pick the language, watch the
// progress (or stop it), and the clip's words land in a Subtitles layer, at their time.

#include <cstdint>
#include <functional>

class QWidget;

namespace bemotion {

class EditorContext;

namespace ui {

// A clip with sound that can be transcribed (media with audio, its file reachable).
bool canTranscribe(const EditorContext& ctx, uint64_t clipId);

// Transcribes the clip `clipId`. The captions replace those of the subtitle layer `subtitlesId`,
// or go to a new Subtitles layer when it is 0.
void transcribeClip(EditorContext& ctx, uint64_t clipId, uint64_t subtitlesId, QWidget* parent);

// Offers to download the speech model (it is not part of the packages); `then` runs once it is
// there. Settings › Plugins lists it with the other downloads.
void offerSpeechModel(QWidget* parent, std::function<void()> then);

} // namespace ui
} // namespace bemotion
