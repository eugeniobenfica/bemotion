-- Charts for bemotion: columns / bars, line / area and pie / donut.
--
-- Every chart reads its data from the object's table (label, value, color)
-- and animates with the "Progress" parameter, which gets intro keyframes
-- when the object is created. See docs/PLUGINS.md for the plugin API.

local PALETTE = { "#E8B931", "#4F8EF7", "#3DBE8B", "#E5604D", "#A77BF3", "#3CC6D6", "#F28C38" }

local SAMPLE = {
  { "Jan", 42, PALETTE[1] },
  { "Feb", 58, PALETTE[2] },
  { "Mar", 51, PALETTE[3] },
  { "Apr", 76, PALETTE[4] },
  { "May", 93, PALETTE[5] },
}

local EASINGS = { "Ease Out", "Back Out", "Elastic", "Bounce", "Ease In Out", "Linear" }
local EASE_IDS = {
  ["Ease Out"] = "ease_out", ["Back Out"] = "back_out", ["Elastic"] = "elastic",
  ["Bounce"] = "bounce", ["Ease In Out"] = "ease_in_out", ["Linear"] = "linear",
}

-- --- shared parameters --------------------------------------------------------

local function animation_params(extra)
  local p = {
    { id = "progress", label = "Progress", type = "number", default = 100, min = 0, max = 100,
      format = "%.0f%%", group = "Animation", intro = { from = 0, duration = 1.6 } },
    { id = "stagger", label = "Stagger", type = "number", default = 45, min = 0, max = 100,
      format = "%.0f%%", group = "Animation" },
    { id = "easing", label = "Easing", type = "choice", choices = EASINGS, default = "Ease Out", group = "Animation" },
  }
  for _, e in ipairs(extra or {}) do p[#p + 1] = e end
  return p
end

local function text_params()
  return {
    { id = "title", label = "Title", type = "text", default = "", group = "Text" },
    { id = "show_labels", label = "Labels", type = "toggle", default = true, group = "Text" },
    { id = "show_values", label = "Values", type = "toggle", default = true, group = "Text" },
    { id = "prefix", label = "Value Prefix", type = "text", default = "", group = "Text" },
    { id = "suffix", label = "Value Suffix", type = "text", default = "", group = "Text" },
    { id = "decimals", label = "Decimals", type = "number", default = 0, min = 0, max = 4, step = 0.05,
      format = "%.0f", group = "Text" },
    { id = "font_size", label = "Font Size", type = "number", default = 26, min = 6, max = 200,
      format = "%.0f px", group = "Text" },
    { id = "text_color", label = "Text Color", type = "color", default = "#FFFFFF", group = "Text" },
  }
end

local function concat(...)
  local out = {}
  for _, list in ipairs({ ... }) do
    for _, v in ipairs(list) do out[#out + 1] = v end
  end
  return out
end

-- --- helpers -----------------------------------------------------------------

local function clamp(v, a, b) return math.max(a, math.min(b, v)) end

-- Progress of item i (1-based) of n: with stagger the items start one after another.
local function item_progress(o, i, n)
  local p = clamp(o.progress / 100, 0, 1)
  local s = clamp(o.stagger / 100, 0, 1)
  local span = 1 / (1 + s * (n - 1))
  local local_t = clamp((p - (i - 1) * s * span) / span, 0, 1)
  return bemotion.ease(EASE_IDS[o.easing] or "ease_out", local_t), local_t
end

local function nice_max(v)
  if v <= 0 then return 1 end
  local e = 10 ^ math.floor(math.log(v, 10))
  for _, m in ipairs({ 1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10 }) do
    if m * e >= v then return m * e end
  end
  return 10 * e
end

local function value_text(o, v)
  return o.prefix .. bemotion.format(v, math.floor(o.decimals + 0.5)) .. o.suffix
end

local function data_max(o)
  local m = 0
  for _, r in ipairs(o.rows) do m = math.max(m, r.value) end
  if o.max_value and o.max_value > 0 then return o.max_value end
  return nice_max(m)
end

-- Draws the title and returns the remaining content box.
local function frame(o, g)
  local w, h = o.width, o.height
  local x0, y0, x1, y1 = -w / 2, -h / 2, w / 2, h / 2
  if o.background and o.background.a > 0 then
    g.rect(x0, y0, w, h, o.background, o.corner_bg or 18)
  end
  local pad = math.min(w, h) * 0.06
  x0, y0, x1, y1 = x0 + pad, y0 + pad, x1 - pad, y1 - pad
  if o.title ~= "" then
    local size = o.font_size * 1.35
    g.text(o.title, x0, y0 + size / 2, size, o.text_color, "left", true)
    y0 = y0 + size * 1.6
  end
  return x0, y0, x1, y1
end

-- --- column / bar chart ---------------------------------------------------------

bemotion.register_object {
  id = "bars",
  name = "Bar Chart",
  description = "Columns or horizontal bars that grow in, one after another.",
  icon = "bars",
  size = { 1000, 600 },
  rows = SAMPLE,
  params = concat(
    { { id = "orientation", label = "Orientation", type = "choice", choices = { "Columns", "Bars" },
        default = "Columns", group = "Chart" },
      { id = "max_value", label = "Max Value", type = "number", default = 0, min = 0, step = 1,
        format = "%.0f (0 = auto)", group = "Chart" },
      { id = "gap", label = "Gap", type = "number", default = 35, min = 0, max = 90, format = "%.0f%%", group = "Chart" },
      { id = "corner", label = "Corner Radius", type = "number", default = 10, min = 0, max = 200,
        format = "%.0f px", group = "Chart" },
      { id = "single_color", label = "Single Color", type = "toggle", default = false, group = "Chart" },
      { id = "bar_color", label = "Bar Color", type = "color", default = "#E8B931", group = "Chart" },
      { id = "show_grid", label = "Grid", type = "toggle", default = true, group = "Chart" },
      { id = "grid_color", label = "Grid Color", type = "color", default = "#FFFFFF26", group = "Chart" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Chart" } },
    animation_params(), text_params()),
  draw = function(o, g)
    local rows = o.rows
    local n = #rows
    local x0, y0, x1, y1 = frame(o, g)
    if n == 0 then return end
    local max = data_max(o)
    local fs = o.font_size
    local columns = o.orientation ~= "Bars"

    if columns then
      local label_h = o.show_labels and fs * 1.8 or 0
      local value_h = o.show_values and fs * 1.6 or 0
      local axis_w = o.show_grid and g.measure(value_text(o, max), fs * 0.75) + fs * 0.6 or 0
      local bx0, by0, bx1, by1 = x0 + axis_w, y0 + value_h, x1, y1 - label_h
      if o.show_grid then
        for k = 0, 4 do
          local y = by1 - (by1 - by0) * k / 4
          g.line(bx0, y, bx1, y, 1.5, o.grid_color)
          g.text(value_text(o, max * k / 4), bx0 - fs * 0.4, y, fs * 0.75,
                 bemotion.with_alpha(o.text_color, 0.55), "right")
        end
      end
      local slot = (bx1 - bx0) / n
      local bw = slot * (1 - clamp(o.gap / 100, 0, 0.9))
      for i, r in ipairs(rows) do
        local e, t = item_progress(o, i, n)
        local color = o.single_color and o.bar_color or r.color
        local hgt = (by1 - by0) * clamp(r.value / max, 0, 1.5) * e
        local cx = bx0 + slot * (i - 0.5)
        if hgt > 0.5 then
          g.rect(cx - bw / 2, by1 - hgt, bw, hgt, color, math.min(o.corner, bw / 2))
        end
        if o.show_values and t > 0 then
          local tc = bemotion.with_alpha(o.text_color, clamp(t * 3, 0, 1))
          g.text(value_text(o, r.value * e), cx, by1 - hgt - fs * 0.8, fs, tc, "center", true)
        end
        if o.show_labels then
          g.text(r.label, cx, by1 + label_h * 0.55, fs * 0.9, bemotion.with_alpha(o.text_color, 0.7), "center")
        end
      end
    else
      local label_w = 0
      if o.show_labels then
        for _, r in ipairs(rows) do label_w = math.max(label_w, g.measure(r.label, fs * 0.9)) end
        label_w = label_w + fs
      end
      local value_w = o.show_values and g.measure(value_text(o, max), fs) + fs or 0
      local bx0, by0, bx1, by1 = x0 + label_w, y0, x1 - value_w, y1
      if o.show_grid then
        for k = 0, 4 do
          local x = bx0 + (bx1 - bx0) * k / 4
          g.line(x, by0, x, by1, 1.5, o.grid_color)
        end
      end
      local slot = (by1 - by0) / n
      local bh = slot * (1 - clamp(o.gap / 100, 0, 0.9))
      for i, r in ipairs(rows) do
        local e, t = item_progress(o, i, n)
        local color = o.single_color and o.bar_color or r.color
        local len = (bx1 - bx0) * clamp(r.value / max, 0, 1.5) * e
        local cy = by0 + slot * (i - 0.5)
        if len > 0.5 then
          g.rect(bx0, cy - bh / 2, len, bh, color, math.min(o.corner, bh / 2))
        end
        if o.show_labels then
          g.text(r.label, bx0 - fs * 0.5, cy, fs * 0.9, bemotion.with_alpha(o.text_color, 0.7), "right")
        end
        if o.show_values and t > 0 then
          local tc = bemotion.with_alpha(o.text_color, clamp(t * 3, 0, 1))
          g.text(value_text(o, r.value * e), bx0 + len + fs * 0.4, cy, fs, tc, "left", true)
        end
      end
    end
  end,
}

-- --- line / area chart ------------------------------------------------------------

bemotion.register_object {
  id = "line",
  name = "Line Chart",
  description = "A line (or filled area) that draws itself from left to right.",
  icon = "line_chart",
  size = { 1000, 600 },
  rows = SAMPLE,
  params = concat(
    { { id = "max_value", label = "Max Value", type = "number", default = 0, min = 0, step = 1,
        format = "%.0f (0 = auto)", group = "Chart" },
      { id = "line_color", label = "Line Color", type = "color", default = "#E8B931", group = "Chart" },
      { id = "line_width", label = "Line Width", type = "number", default = 6, min = 0, max = 60,
        format = "%.1f px", group = "Chart" },
      { id = "fill", label = "Fill Area", type = "toggle", default = true, group = "Chart" },
      { id = "fill_opacity", label = "Fill Opacity", type = "number", default = 22, min = 0, max = 100,
        format = "%.0f%%", group = "Chart" },
      { id = "dots", label = "Dots", type = "toggle", default = true, group = "Chart" },
      { id = "dot_size", label = "Dot Size", type = "number", default = 9, min = 0, max = 80,
        format = "%.1f px", group = "Chart" },
      { id = "show_grid", label = "Grid", type = "toggle", default = true, group = "Chart" },
      { id = "grid_color", label = "Grid Color", type = "color", default = "#FFFFFF26", group = "Chart" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Chart" } },
    animation_params(), text_params()),
  draw = function(o, g)
    local rows = o.rows
    local n = #rows
    local x0, y0, x1, y1 = frame(o, g)
    if n == 0 then return end
    local max = data_max(o)
    local fs = o.font_size
    local label_h = o.show_labels and fs * 1.8 or 0
    local axis_w = o.show_grid and g.measure(value_text(o, max), fs * 0.75) + fs * 0.6 or 0
    local bx0, by0, bx1, by1 = x0 + axis_w, y0 + fs * 1.6, x1 - fs, y1 - label_h

    if o.show_grid then
      for k = 0, 4 do
        local y = by1 - (by1 - by0) * k / 4
        g.line(bx0, y, bx1, y, 1.5, o.grid_color)
        g.text(value_text(o, max * k / 4), bx0 - fs * 0.4, y, fs * 0.75,
               bemotion.with_alpha(o.text_color, 0.55), "right")
      end
    end

    local px0, px1 = bx0 + fs * 1.2, bx1 - fs * 0.4 -- keep the end points clear of the axis labels
    local function point(i)
      local x = n == 1 and (px0 + px1) / 2 or px0 + (px1 - px0) * (i - 1) / (n - 1)
      return x, by1 - (by1 - by0) * clamp(rows[i].value / max, 0, 1.5)
    end

    -- The line is revealed along x; the stagger parameter is not used here.
    local p = bemotion.ease(EASE_IDS[o.easing] or "ease_out", clamp(o.progress / 100, 0, 1))
    local reveal = p * (n - 1) + 1 -- in points (1 .. n)
    local pts = {}
    for i = 1, n do
      if i <= reveal then
        local x, y = point(i)
        pts[#pts + 1] = x
        pts[#pts + 1] = y
      elseif i - 1 < reveal then -- partial segment
        local ax, ay = point(i - 1)
        local bx, by = point(i)
        local f = reveal - (i - 1)
        pts[#pts + 1] = ax + (bx - ax) * f
        pts[#pts + 1] = ay + (by - ay) * f
      end
    end

    if o.fill and #pts >= 4 then
      local area = { pts[1], by1 }
      for k = 1, #pts do area[#area + 1] = pts[k] end
      area[#area + 1] = pts[#pts - 1]
      area[#area + 1] = by1
      g.polygon(area, bemotion.with_alpha(o.line_color, o.fill_opacity / 100))
    end
    if #pts >= 4 and o.line_width > 0 then g.polyline(pts, o.line_width, o.line_color) end

    for i, r in ipairs(rows) do
      local x, y = point(i)
      local shown = clamp((reveal - i) * 3 + 1, 0, 1) -- pops in when the line reaches it
      if o.dots and shown > 0 and o.dot_size > 0 then
        g.circle(x, y, o.dot_size * bemotion.ease("back_out", shown), o.line_color)
        g.circle(x, y, o.dot_size * 0.45 * shown, o.background.a > 0 and o.background or "#101012")
      end
      if o.show_values and shown > 0 then
        g.text(value_text(o, r.value), x, y - fs * 1.1, fs, bemotion.with_alpha(o.text_color, shown), "center", true)
      end
      if o.show_labels then
        g.text(r.label, x, by1 + label_h * 0.55, fs * 0.9, bemotion.with_alpha(o.text_color, 0.7), "center")
      end
    end
  end,
}

-- --- pie / donut chart ---------------------------------------------------------------

bemotion.register_object {
  id = "donut",
  name = "Donut Chart",
  description = "Pie or donut slices that sweep in, with a legend.",
  icon = "pie",
  size = { 1000, 600 },
  rows = SAMPLE,
  params = concat(
    { { id = "hole", label = "Hole Size", type = "number", default = 58, min = 0, max = 95,
        format = "%.0f%%", group = "Chart" },
      { id = "slice_gap", label = "Slice Gap", type = "number", default = 1.5, min = 0, max = 20,
        format = "%.1f°", group = "Chart" },
      { id = "start_angle", label = "Start Angle", type = "number", default = 0, format = "%.0f°", group = "Chart" },
      { id = "legend", label = "Legend", type = "toggle", default = true, group = "Chart" },
      { id = "percent", label = "Show Percent", type = "toggle", default = true, group = "Chart" },
      { id = "center_text", label = "Center Text", type = "text", default = "", group = "Chart" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Chart" } },
    animation_params(), text_params()),
  draw = function(o, g)
    local rows = o.rows
    local n = #rows
    local x0, y0, x1, y1 = frame(o, g)
    if n == 0 then return end
    local fs = o.font_size
    local total = 0
    for _, r in ipairs(rows) do total = total + math.max(0, r.value) end
    if total <= 0 then return end

    local legend_w = 0
    if o.legend then
      for _, r in ipairs(rows) do
        legend_w = math.max(legend_w, g.measure(r.label .. "  100%", fs) + fs * 2)
      end
    end
    local cx = (x0 + x1 - legend_w) / 2
    local cy = (y0 + y1) / 2
    local radius = math.max(1, math.min((x1 - x0 - legend_w) / 2, (y1 - y0) / 2))
    local inner = radius * clamp(o.hole / 100, 0, 0.95)

    local angle = o.start_angle
    for i, r in ipairs(rows) do
      local share = math.max(0, r.value) / total
      local e = item_progress(o, i, n)
      local sweep = 360 * share
      local gap = math.min(o.slice_gap, sweep * 0.4)
      local a0 = angle + gap / 2
      local a1 = a0 + math.max(0, sweep - gap) * e
      local grow = inner + (radius - inner) * (0.85 + 0.15 * e)
      if a1 > a0 then g.arc(cx, cy, inner, grow, a0, a1, r.color) end
      angle = angle + sweep
    end

    if o.center_text ~= "" and inner > fs then
      local p = clamp(o.progress / 100 * 3, 0, 1)
      g.text(o.center_text, cx, cy, math.min(inner * 0.45, fs * 2), bemotion.with_alpha(o.text_color, p), "center", true)
    end

    if o.legend then
      local lx = x1 - legend_w + fs
      local line = fs * 1.7
      local ly = cy - line * (n - 1) / 2
      for i, r in ipairs(rows) do
        local _, t = item_progress(o, i, n)
        local a = clamp(t * 3, 0, 1)
        local y = ly + (i - 1) * line
        g.rect(lx, y - fs * 0.35, fs * 0.7, fs * 0.7, bemotion.with_alpha(r.color, a), fs * 0.2)
        local label = r.label
        if o.percent then
          label = label .. "  " .. bemotion.format(math.max(0, r.value) / total * 100, 0) .. "%"
        elseif o.show_values then
          label = label .. "  " .. value_text(o, r.value)
        end
        g.text(label, lx + fs * 1.1, y, fs, bemotion.with_alpha(o.text_color, a * 0.9), "left")
      end
    end
  end,
}
