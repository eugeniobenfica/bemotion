-- Every selected shape gets a color of its own, from a calm palette.
local doc = ...
local palette = { "#E8B931", "#E0614D", "#4FA3D9", "#5CB87A", "#A77BD9", "#E58FB8", "#F2A541" }
local n = 0
for i, id in ipairs(doc.selection) do
  local ok = pcall(doc.apply, { op = "property.set", layer = id, prop = "fill_color", value = palette[(i - 1) % #palette + 1] })
  if ok then n = n + 1 end
end
if n == 0 then error("select one or more shapes") end
print("colored " .. n .. " shapes")
