-- 3D Models for bemotion: imported models (glTF / GLB, OBJ, STL) drawn into the 2D scene.
--
-- The model file is a "file" parameter: the user picks it, bemotion reads it
-- (the "files" permission) and hands its bytes over. It is parsed once per
-- file version (bemotion.cached) into a mesh, which g.mesh projects, lights
-- and paints every frame. Turning, zoom, light and color are ordinary
-- parameters, so they keyframe like any property. See docs/PLUGINS.md.
--
-- Supported: triangles (and polygons, fanned) with their normals, vertex
-- colors and glTF material colors. Not supported: textures, glTF files with
-- external .bin buffers (export a .glb or an embedded .gltf), compressed
-- meshes (Draco), skinning and morph targets.

local unpack = string.unpack

-- --- matrices (4x4, column-major, as in glTF) -----------------------------------------

local IDENTITY = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }

local function mat_mul(a, b)
  local c = {}
  for col = 0, 3 do
    for row = 0, 3 do
      local s = 0
      for k = 0, 3 do s = s + a[k * 4 + row + 1] * b[col * 4 + k + 1] end
      c[col * 4 + row + 1] = s
    end
  end
  return c
end

local function mat_trs(t, r, s)
  t, r, s = t or { 0, 0, 0 }, r or { 0, 0, 0, 1 }, s or { 1, 1, 1 }
  local x, y, z, w = r[1], r[2], r[3], r[4]
  return {
    (1 - 2 * (y * y + z * z)) * s[1], (2 * (x * y + z * w)) * s[1], (2 * (x * z - y * w)) * s[1], 0,
    (2 * (x * y - z * w)) * s[2], (1 - 2 * (x * x + z * z)) * s[2], (2 * (y * z + x * w)) * s[2], 0,
    (2 * (x * z + y * w)) * s[3], (2 * (y * z - x * w)) * s[3], (1 - 2 * (x * x + y * y)) * s[3], 0,
    t[1], t[2], t[3], 1,
  }
end

-- Z-up models (most CAD and STL files) turned so that up is y.
local Z_UP = { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 }

-- --- glTF / GLB ---------------------------------------------------------------------------

local COMPONENTS = { SCALAR = 1, VEC2 = 2, VEC3 = 3, VEC4 = 4, MAT4 = 16 }
local FORMATS = { -- componentType -> unpack format, bytes, normalization divisor
  [5120] = { "i1", 1, 127 }, [5121] = { "I1", 1, 255 }, [5122] = { "i2", 2, 32767 },
  [5123] = { "I2", 2, 65535 }, [5125] = { "I4", 4, 4294967295 }, [5126] = { "f", 4, 1 },
}

local function parse_glb(data)
  local magic = unpack("<c4", data, 1)
  if magic ~= "glTF" then error("not a GLB file") end
  local pos, json_text, bin = 13, nil, nil
  while pos + 8 <= #data do
    local length, kind = unpack("<I4I4", data, pos)
    local chunk = data:sub(pos + 8, pos + 7 + length)
    if kind == 0x4E4F534A then json_text = chunk elseif kind == 0x004E4942 then bin = chunk end
    pos = pos + 8 + length
  end
  if not json_text then error("the GLB file has no JSON chunk") end
  return bemotion.json(json_text), bin
end

-- The bytes of every buffer: the GLB's binary chunk or base64 data URIs.
local function load_buffers(gltf, bin)
  local buffers = {}
  for i, b in ipairs(gltf.buffers or {}) do
    if b.uri == nil then
      buffers[i] = bin or error("buffer " .. (i - 1) .. " has no data")
    elseif b.uri:sub(1, 5) == "data:" then
      buffers[i] = bemotion.base64(b.uri:match(",(.*)$") or "")
    else
      error("this .gltf keeps its data in \"" .. b.uri .. "\": export a .glb (or an embedded .gltf) instead")
    end
  end
  return buffers
end

-- An accessor's data: a packed string when it can go to bemotion.mesh as is, else a table.
local function read_accessor(gltf, buffers, index, want_floats)
  local acc = gltf.accessors[index + 1]
  if acc.sparse then error("sparse accessors are not supported") end
  local n = COMPONENTS[acc.type] or 1
  local fmt = FORMATS[acc.componentType] or error("unknown component type " .. tostring(acc.componentType))
  if acc.bufferView == nil then -- all zeros
    local t = {}
    for i = 1, acc.count * n do t[i] = 0 end
    return t
  end
  local view = gltf.bufferViews[acc.bufferView + 1]
  local data = buffers[(view.buffer or 0) + 1]
  local start = (view.byteOffset or 0) + (acc.byteOffset or 0) + 1
  local stride = view.byteStride or 0
  local packed = n * fmt[2]
  if stride == 0 or stride == packed then
    if (want_floats and acc.componentType == 5126) or (not want_floats and n == 1 and acc.componentType ~= 5126) then
      return data:sub(start, start + acc.count * packed - 1), fmt[2]
    end
    stride = packed
  end
  local out, k = {}, 1
  local f = "<" .. fmt[1]
  local norm = (want_floats and acc.normalized) and fmt[3] or 1
  for i = 0, acc.count - 1 do
    local at = start + i * stride
    for c = 0, n - 1 do
      out[k] = unpack(f, data, at + c * fmt[2]) / norm
      k = k + 1
    end
  end
  if not want_floats then -- indices as a table: 1-based
    for i = 1, #out do out[i] = out[i] + 1 end
  end
  return out
end

local function gltf_parts(gltf, bin)
  for _, ext in ipairs(gltf.extensionsRequired or {}) do
    error("this model needs " .. ext .. ", which is not supported")
  end
  local buffers = load_buffers(gltf, bin)
  local parts = {}
  local function add_mesh(mesh_index, matrix)
    local mesh = gltf.meshes[mesh_index + 1]
    for _, prim in ipairs(mesh.primitives or {}) do
      local mode = prim.mode or 4
      if mode == 4 and prim.attributes.POSITION then
        local part = { matrix = matrix }
        part.positions = read_accessor(gltf, buffers, prim.attributes.POSITION, true)
        if prim.attributes.NORMAL then part.normals = read_accessor(gltf, buffers, prim.attributes.NORMAL, true) end
        if prim.attributes.COLOR_0 then
          local colors = read_accessor(gltf, buffers, prim.attributes.COLOR_0, true)
          if type(colors) == "string" then -- packed floats: unpack them for the size check below
            local t = {}
            for i = 1, #colors // 4 do t[i] = unpack("<f", colors, (i - 1) * 4 + 1) end
            colors = t
          end
          local acc = gltf.accessors[prim.attributes.COLOR_0 + 1]
          part.colors, part.color_size = colors, COMPONENTS[acc.type]
        end
        if prim.indices then part.indices, part.index_size = read_accessor(gltf, buffers, prim.indices, false) end
        local material = prim.material and gltf.materials and gltf.materials[prim.material + 1]
        local pbr = material and material.pbrMetallicRoughness
        local f = pbr and pbr.baseColorFactor
        if f then part.color = { f[1], f[2], f[3], f[4] } end
        parts[#parts + 1] = part
      end
    end
  end
  local function visit(node_index, parent)
    local node = gltf.nodes[node_index + 1]
    local local_matrix = node.matrix or mat_trs(node.translation, node.rotation, node.scale)
    local matrix = mat_mul(parent, local_matrix)
    if node.mesh then add_mesh(node.mesh, matrix) end
    for _, child in ipairs(node.children or {}) do visit(child, matrix) end
  end
  local scene = gltf.scenes and gltf.scenes[(gltf.scene or 0) + 1]
  if scene then
    for _, root in ipairs(scene.nodes or {}) do visit(root, IDENTITY) end
  else
    for i = 0, #(gltf.meshes or {}) - 1 do add_mesh(i, IDENTITY) end
  end
  if #parts == 0 then error("no triangle meshes in this file") end
  return parts
end

-- --- OBJ ------------------------------------------------------------------------------------

local function parse_obj(text)
  local vp, vn, vc = {}, {}, {}
  local positions, normals, colors, indices = {}, {}, {}, {}
  local corners, count, all_normals, any_colors = {}, 0, true, false
  for line in text:gmatch("[^\r\n]+") do
    local kind, rest = line:match("^%s*(%S+)%s*(.*)$")
    if kind == "v" then
      local x, y, z, r, g, b = rest:match("^(%S+)%s+(%S+)%s+(%S+)%s*(%S*)%s*(%S*)%s*(%S*)")
      vp[#vp + 1] = { tonumber(x) or 0, tonumber(y) or 0, tonumber(z) or 0 }
      if tonumber(b) then
        vc[#vp] = { tonumber(r), tonumber(g), tonumber(b) }
        any_colors = true
      end
    elseif kind == "vn" then
      local x, y, z = rest:match("^(%S+)%s+(%S+)%s+(%S+)")
      vn[#vn + 1] = { tonumber(x) or 0, tonumber(y) or 0, tonumber(z) or 1 }
    elseif kind == "f" then
      local face = {}
      for token in rest:gmatch("%S+") do
        local vi, ni = token:match("^(%-?%d+)/?%-?%d*/?(%-?%d*)")
        vi, ni = tonumber(vi), tonumber(ni)
        if vi then
          if vi < 0 then vi = #vp + vi + 1 end
          if ni and ni < 0 then ni = #vn + ni + 1 end
          if not ni or not vn[ni] then all_normals = false end
          -- One vertex per distinct position / normal pair, so smooth shading stays smooth.
          local key = vi .. "/" .. (ni or "")
          local index = corners[key]
          if not index and vp[vi] then
            count = count + 1
            index, corners[key] = count, count
            local p, n, c = vp[vi], ni and vn[ni], vc[vi]
            positions[#positions + 1], positions[#positions + 2], positions[#positions + 3] = p[1], p[2], p[3]
            n = n or { 0, 0, 0 }
            normals[#normals + 1], normals[#normals + 2], normals[#normals + 3] = n[1], n[2], n[3]
            c = c or { 1, 1, 1 }
            colors[#colors + 1], colors[#colors + 2], colors[#colors + 3] = c[1], c[2], c[3]
          end
          face[#face + 1] = index
        end
      end
      for i = 2, #face - 1 do -- polygons as triangle fans
        indices[#indices + 1], indices[#indices + 2], indices[#indices + 3] = face[1], face[i], face[i + 1]
      end
    end
  end
  if #indices == 0 then error("no faces in this OBJ file") end
  return { {
    positions = positions, indices = indices,
    normals = all_normals and normals or nil,
    colors = any_colors and colors or nil, color_size = 3,
  } }
end

-- --- STL --------------------------------------------------------------------------------------

local function parse_stl(data)
  -- Vertices with the same position are shared (smooth shading needs them welded).
  local positions, indices, seen, count = {}, {}, {}, 0
  local function vertex(x, y, z)
    local key = string.format("%.6g %.6g %.6g", x, y, z)
    local index = seen[key]
    if not index then
      count = count + 1
      index, seen[key] = count, count
      positions[#positions + 1], positions[#positions + 2], positions[#positions + 3] = x, y, z
    end
    indices[#indices + 1] = index
  end
  local triangles = #data >= 84 and unpack("<I4", data, 81) or 0
  if #data >= 84 and 84 + triangles * 50 == #data then -- binary
    local pos = 85
    for _ = 1, triangles do
      local x1, y1, z1, x2, y2, z2, x3, y3, z3 = unpack("<fffffffff", data, pos + 12)
      vertex(x1, y1, z1)
      vertex(x2, y2, z2)
      vertex(x3, y3, z3)
      pos = pos + 50
    end
  else -- text
    for x, y, z in data:gmatch("vertex%s+(%S+)%s+(%S+)%s+(%S+)") do
      vertex(tonumber(x) or 0, tonumber(y) or 0, tonumber(z) or 0)
    end
  end
  if #indices < 3 then error("no triangles in this STL file") end
  return { { positions = positions, indices = indices } }
end

-- --- loading -------------------------------------------------------------------------------

-- The mesh of a model file (parsed once per file version and orientation), or nil and why.
local function model_mesh(file, up)
  local entry = bemotion.cached(file.path .. "|" .. file.version .. "|" .. up, function()
    local ok, result = pcall(function()
      local parts
      if file.ext == "glb" then
        parts = gltf_parts(parse_glb(file.data))
      elseif file.ext == "gltf" then
        parts = gltf_parts(bemotion.json(file.data), nil)
      elseif file.ext == "obj" then
        parts = parse_obj(file.data)
      elseif file.ext == "stl" then
        parts = parse_stl(file.data)
      else
        error("unsupported file type ." .. file.ext)
      end
      local z_up = up == "Z Up" or (up == "Auto" and file.ext == "stl")
      if z_up then
        for _, part in ipairs(parts) do part.matrix = mat_mul(Z_UP, part.matrix or IDENTITY) end
      end
      return bemotion.mesh { parts = parts }
    end)
    if ok then return { mesh = result } end
    return { error = tostring(result):gsub("^[^:]*:%d+: ", "") } -- the message without the Lua position
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
  },
  draw = function(o, g)
    local file = o.model
    if not file then
      placeholder(o, g, "3D Model", "Choose a .glb, .gltf, .obj or .stl file in Properties > Model")
      return
    end
    local mesh, err = model_mesh(file, o.up)
    if not mesh then
      placeholder(o, g, "Could not read " .. file.name, err, true)
      return
    end
    -- Reveal: grows in while finishing a quarter turn.
    local e = bemotion.ease("back_out", math.max(0, math.min(1, o.reveal / 100)))
    if e <= 0 then return end
    local a, h = math.rad(o.light_angle), math.rad(o.light_height)
    g.mesh(mesh, {
      width = o.width, height = o.height,
      rotation = { o.tilt, o.spin + o.turn_speed * o.time - (1 - e) * 90, o.roll },
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
