-- Each selected layer starts a little after the one before it (0.12 s).
local doc = ...
if #doc.selection < 2 then error("select two or more layers") end
doc.apply { op = "arrange.stagger", layers = doc.selection, step = 0.12 }
