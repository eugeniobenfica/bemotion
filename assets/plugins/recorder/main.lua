-- Recorder: a microphone button in the timeline's toolbar.
--
-- Press it to start recording (the timeline plays, so you can speak over it);
-- press it again to stop: the recording is saved next to the project
-- (Recordings/Recording N.wav), imported, and placed where recording began.

local started_at = 0

bemotion.register_command {
  id = "record",
  name = "Record Audio",
  description = "Record from the microphone at the playhead; press again to stop",
  button = { place = "timeline", icon = "mic", toggle = true },
  run = function(doc)
    if doc.pressed then
      bemotion.microphone.start()
      started_at = doc.time
      doc.play(true)
      return
    end
    doc.play(false)
    if not bemotion.microphone.recording() then return end
    local file = bemotion.microphone.stop()
    local media = doc.apply { op = "media.import", path = file }
    doc.apply { op = "layer.add", type = "media", media = media.media, start = started_at, name = "Recording" }
  end,
}
