-- The selected layer, copied into a 4 x 3 grid that pops in one copy after another.
local doc = ...
if #doc.selection ~= 1 then error("select one layer") end
local source = doc.selection[1]
local comp = doc.composition()
local cols, rows = 4, 3
local ids = {}
for r = 0, rows - 1 do
  for c = 0, cols - 1 do
    local id = source
    if r > 0 or c > 0 then id = doc.apply { op = "layer.duplicate", layer = source }.layer end
    local x = comp.width * (c + 0.5) / cols
    local y = comp.height * (r + 0.5) / rows
    doc.apply { op = "key.clear", layer = id, prop = "position" } -- each copy stays in its cell
    doc.apply { op = "property.set", layer = id, prop = "position", value = { x, y } }
    doc.apply { op = "property.set", layer = id, prop = "scale", value = { 35, 35 } }
    ids[#ids + 1] = id
  end
end
doc.apply { op = "arrange.stagger", layers = ids, step = 0.06 }
for _, id in ipairs(ids) do
  doc.apply { op = "transition.add", layer = id, transition = "pop", edge = "in", duration = 0.5 }
end
