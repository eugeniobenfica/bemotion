-- Layout: commands that edit the project through operations (docs/OPERATIONS.md).
-- Each command runs on a copy of the project and becomes one undo step; if it
-- raises an error, nothing changes.

-- The selected layers, in the order they are stacked (or every layer when none is selected).
local function targets(doc)
  local picked = {}
  for _, id in ipairs(doc.selection) do picked[id] = true end
  local list = {}
  for _, l in ipairs(doc.layers()) do
    if (next(picked) == nil or picked[l.layer]) and l.type ~= "camera" and l.parent == nil then
      list[#list + 1] = l
    end
  end
  return list
end

bemotion.register_command {
  id = "distribute",
  name = "Distribute Horizontally",
  description = "Spaces the selected layers evenly across the composition, keeping their heights",
  run = function(doc)
    local layers = targets(doc)
    if #layers < 2 then error("select at least two layers") end
    local comp = doc.composition()
    local margin = comp.width / (#layers + 1)
    -- Left to right in their current order on screen.
    table.sort(layers, function(a, b)
      return doc.layer(a.layer).props.position.value[1] < doc.layer(b.layer).props.position.value[1]
    end)
    for i, l in ipairs(layers) do
      local y = doc.layer(l.layer, doc.time).props.position.value[2]
      doc.apply { op = "property.set", layer = l.layer, prop = "position", time = doc.time, value = { margin * i, y } }
    end
  end,
}

bemotion.register_command {
  id = "stagger",
  name = "Stagger Start Times",
  description = "Starts each selected layer a little after the one above it (0.1 s apart)",
  run = function(doc)
    local layers = targets(doc)
    local start = layers[1] and layers[1]["in"] or 0
    for i, l in ipairs(layers) do
      doc.apply { op = "layer.set", layer = l.layer, start = start + (i - 1) * 0.1 }
    end
  end,
}

bemotion.register_command {
  id = "fade_in",
  name = "Fade In Selected",
  description = "Opacity from 0 to 100% over the first half second of each selected layer",
  run = function(doc)
    for _, l in ipairs(targets(doc)) do
      doc.apply {
        { op = "key.add", layer = l.layer, prop = "opacity", time = l["in"], value = 0, easing = "ease_out" },
        { op = "key.add", layer = l.layer, prop = "opacity", time = l["in"] + 0.5, value = 100 },
      }
    end
  end,
}
