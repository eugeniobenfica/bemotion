-- 3D Models for bemotion: imported models (glTF / GLB, OBJ, STL) drawn into the 2D scene.
--
-- The model file is a "file" parameter: the user picks it, bemotion reads it
-- (the "files" permission) and bemotion.load_mesh turns it into a mesh, natively
-- (quick even for big models; a .gltf's .bin files are read from beside it),
-- simplified to the object's Detail. It is made once per file version and
-- settings (bemotion.cached); g.mesh projects, lights and paints it every frame.
-- Turning, zoom, light and color are ordinary parameters, so they keyframe like
-- any property. See docs/PLUGINS.md.
--
-- Supported: triangles (and polygons, fanned) with their normals, vertex
-- colors and glTF material colors. Not supported: textures, compressed meshes
-- (Draco), skinning and morph targets.

-- Detail: at most this many triangles (0: all). Fewer draw faster; a model rarely shows more.
local DETAIL = { Low = 15000, Medium = 50000, High = 150000, Full = 0 }

-- --- loading -------------------------------------------------------------------------------

-- The mesh of a model file (made once per file version, orientation and detail), or nil and why.
local function model_mesh(file, up, detail)
  local limit = DETAIL[detail] or DETAIL.Medium
  local entry = bemotion.cached(file.path .. "|" .. file.version .. "|" .. up .. "|" .. limit, function()
    local mesh, err = bemotion.load_mesh(file, { up = up, max_triangles = limit })
    if mesh then return { mesh = mesh } end
    return { error = err or "the file could not be read" }
  end)
  return entry.mesh, entry.error
end

-- --- the object ------------------------------------------------------------------------------

local function placeholder(o, g, line1, line2, danger)
  local w, h = o.width, o.height
  local color = danger and "#E5604D" or "#FFFFFF"
  g.rect(-w / 2, -h / 2, w, h, bemotion.with_alpha(color, 0.05), 18)
  g.rect_outline(-w / 2, -h / 2, w, h, bemotion.with_alpha(color, 0.35), 3, 18)
  -- A small cube outline.
  local s, cy = math.min(w, h) * 0.12, -math.min(w, h) * 0.12
  local d = s * 0.45
  local c = bemotion.with_alpha(color, 0.6)
  g.polyline({ -s, cy - s + d, s - d, cy - s + d, s - d, cy + s, -s, cy + s }, 4, c, true)
  g.polyline({ -s, cy - s + d, -s + d, cy - s, s, cy - s, s - d, cy - s + d }, 4, c)
  g.line(s, cy - s, s, cy + s - d, 4, c)
  g.line(s, cy + s - d, s - d, cy + s, 4, c)
  local size = math.max(14, math.min(w, h) * 0.06)
  g.text(line1, 0, s * 0.6 + size * 1.4, size, bemotion.with_alpha(color, 0.9), "center", true)
  if line2 then g.text(line2, 0, s * 0.6 + size * 2.9, size * 0.7, bemotion.with_alpha(color, 0.6), "center") end
end

bemotion.register_object {
  id = "model",
  name = "3D Model",
  description = "An imported 3D model (glTF / GLB, OBJ or STL) you can turn, light and animate.",
  icon = "cube",
  size = { 800, 800 },
  params = {
    { id = "model", label = "Model", type = "file", accept = { "glb", "gltf", "obj", "stl" }, group = "Model" },
    { id = "up", label = "Up Axis", type = "choice", choices = { "Auto", "Y Up", "Z Up" }, default = "Auto",
      group = "Model" },
    { id = "spin", label = "Spin", type = "number", default = 30, format = "%.1f°", group = "Turn" },
    { id = "tilt", label = "Tilt", type = "number", default = 15, format = "%.1f°", group = "Turn" },
    { id = "roll", label = "Roll", type = "number", default = 0, format = "%.1f°", group = "Turn" },
    { id = "turn_speed", label = "Auto Spin", type = "number", default = 0, format = "%.0f°/s", group = "Turn" },
    { id = "zoom", label = "Zoom", type = "number", default = 100, min = 1, max = 1000, format = "%.0f%%",
      group = "Camera" },
    { id = "fov", label = "Perspective", type = "number", default = 30, min = 0, max = 120,
      format = "%.0f° (0 = flat)", group = "Camera" },
    { id = "reveal", label = "Reveal", type = "number", default = 100, min = 0, max = 100, format = "%.0f%%",
      group = "Animation", intro = { from = 0, duration = 1.2 } },
    { id = "shading", label = "Shading", type = "choice", choices = { "Smooth", "Flat" }, default = "Smooth",
      group = "Look" },
    { id = "model_colors", label = "Model Colors", type = "toggle", default = true, group = "Look" },
    { id = "color", label = "Color", type = "color", default = "#FFFFFF", group = "Look" },
    { id = "light_angle", label = "Light Angle", type = "number", default = -35, format = "%.0f°", group = "Look" },
    { id = "light_height", label = "Light Height", type = "number", default = 40, min = -90, max = 90,
      format = "%.0f°", group = "Look" },
    { id = "ambient", label = "Ambient", type = "number", default = 35, min = 0, max = 100, format = "%.0f%%",
      group = "Look" },
    { id = "shine", label = "Shine", type = "number", default = 25, min = 0, max = 100, format = "%.0f%%",
      group = "Look" },
    { id = "cull", label = "Hide Back Faces", type = "toggle", default = false, group = "Look" },
    { id = "detail", label = "Detail", type = "choice", choices = { "Low", "Medium", "High", "Full" }, default = "Medium",
      group = "Model" },
  },
  draw = function(o, g)
    local file = o.model
    if not file then
      placeholder(o, g, "3D Model", "Choose a .glb, .gltf, .obj or .stl file in Properties > Model")
      return
    end
    local mesh, err = model_mesh(file, o.up, o.detail)
    if not mesh then
      placeholder(o, g, "Could not read " .. file.name, err, true)
      return
    end
    -- Reveal: grows in while finishing a quarter turn.
    local e = bemotion.ease("back_out", math.max(0, math.min(1, o.reveal / 100)))
    if e <= 0 then return end
    local a, h = math.rad(o.light_angle), math.rad(o.light_height)
    local turn = bemotion.setting("reveal_turn") == false and 0 or (1 - e) * 90 -- Settings > Plugins > 3D Models
    g.mesh(mesh, {
      width = o.width, height = o.height,
      rotation = { o.tilt, o.spin + o.turn_speed * o.time - turn, o.roll },
      zoom = o.zoom / 100 * e,
      fov = o.fov,
      light = { math.cos(h) * math.sin(a), math.sin(h), math.cos(h) * math.cos(a) },
      ambient = o.ambient / 100,
      shine = o.shine / 100,
      tint = o.color,
      model_colors = o.model_colors,
      flat = o.shading == "Flat",
      cull = o.cull,
    })
  end,
}
