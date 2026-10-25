-- The other selected layers follow the first one, each a moment after the one before (expressions).
local doc = ...
if #doc.selection < 2 then error("select the leader first, then the layers that follow it") end
local leader = doc.layer(doc.selection[1])
for _, l in ipairs(doc.layers()) do
  if l.name == leader.name and l.layer ~= leader.layer then
    error("another layer is also named \"" .. leader.name .. "\": rename the leader first")
  end
end
local name = leader.name:gsub("\\", "\\\\"):gsub("\"", "\\\"")
for i = 2, #doc.selection do
  local expression = string.format("layer(\"%s\").position(time - %.2f)", name, (i - 1) * 0.12)
  local _, problem = doc.expression(doc.selection[i], "position", expression)
  if problem then error(problem) end
  doc.apply { op = "expression.set", layer = doc.selection[i], prop = "position", expression = expression }
end
print(#doc.selection - 1 .. " layers follow " .. leader.name)
