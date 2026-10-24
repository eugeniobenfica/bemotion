-- The selected layers shake gently, each its own way (an expression on their position).
local doc = ...
if #doc.selection == 0 then error("select the layers to wiggle") end
for _, id in ipairs(doc.selection) do
  doc.apply { op = "expression.set", layer = id, prop = "position", expression = "wiggle(2, 20)" }
end
