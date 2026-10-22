-- Charts for bemotion: columns / bars, line / area and pie / donut.
--
-- Every chart reads its data from the object's table (label, value, color)
-- and animates with "Progress", which gets intro keyframes when the object is
-- created. "Trim Start / End" work like trim paths on every bar, the line and
-- every slice. See docs/PLUGINS.md for the plugin API.

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

local function animation_params()
  return {
    { id = "progress", label = "Progress", type = "number", default = 100, min = 0, max = 100,
      format = "%.0f%%", group = "Animation", intro = { from = 0, duration = 1.6 } },
    { id = "stagger", label = "Stagger", type = "number", default = 45, min = 0, max = 100,
      format = "%.0f%%", group = "Animation" },
    { id = "easing", label = "Easing", type = "choice", choices = EASINGS, default = "Ease Out", group = "Animation" },
  }
end

local function trim_params()
  return {
    { id = "trim_start", label = "Trim Start", type = "number", default = 0, min = 0, max = 100,
      format = "%.0f%%", group = "Trim" },
    { id = "trim_end", label = "Trim End", type = "number", default = 100, min = 0, max = 100,
      format = "%.0f%%", group = "Trim" },
  }
end

local function data_params()
  return {
    { id = "sort", label = "Sort", type = "choice", choices = { "None", "Ascending", "Descending" },
      default = "None", group = "Sorting & Highlight" },
    { id = "highlight", label = "Highlight Row", type = "number", default = 0, min = 0, max = 100, step = 0.05,
      format = "%.0f (0 = none)", group = "Sorting & Highlight" },
  }
end

local function text_params(extra)
  local p = {
    { id = "title", label = "Title", type = "text", default = "", group = "Text" },
    { id = "show_labels", label = "Labels", type = "toggle", default = true, group = "Text" },
    { id = "show_values", label = "Values", type = "toggle", default = true, group = "Text" },
    { id = "prefix", label = "Value Prefix", type = "text", default = "", group = "Text" },
    { id = "suffix", label = "Value Suffix", type = "text", default = "", group = "Text" },
    { id = "compact", label = "Compact Numbers", type = "toggle", default = false, group = "Text" },
    { id = "decimals", label = "Decimals", type = "number", default = 0, min = 0, max = 4, step = 0.05,
      format = "%.0f", group = "Text" },
    { id = "font_size", label = "Font Size", type = "number", default = 26, min = 6, max = 200,
      format = "%.0f px", group = "Text" },
    { id = "text_color", label = "Text Color", type = "color", default = "#FFFFFF", group = "Text" },
  }
  for _, e in ipairs(extra or {}) do p[#p + 1] = e end
  return p
end

local function grid_params()
  return {
    { id = "show_grid", label = "Grid", type = "toggle", default = true, group = "Grid" },
    { id = "grid_lines", label = "Grid Lines", type = "number", default = 4, min = 1, max = 12, step = 0.05,
      format = "%.0f", group = "Grid" },
    { id = "grid_color", label = "Grid Color", type = "color", default = "#FFFFFF26", group = "Grid" },
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
  local span = 1 / (1 + s * math.max(0, n - 1))
  local local_t = clamp((p - (i - 1) * s * span) / span, 0, 1)
  return bemotion.ease(EASE_IDS[o.easing] or "ease_out", local_t), local_t
end

-- The trimmed part of every bar / slice / the line, as fractions (a <= b).
local function trim(o)
  local a, b = clamp(o.trim_start / 100, 0, 1), clamp(o.trim_end / 100, 0, 1)
  if a > b then a, b = b, a end
  return a, b
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
  local decimals = math.floor(o.decimals + 0.5)
  local s
  if o.compact and math.abs(v) >= 1000 then
    local units = { { 1e12, "T" }, { 1e9, "B" }, { 1e6, "M" }, { 1e3, "K" } }
    for _, u in ipairs(units) do
      if math.abs(v) >= u[1] then
        s = bemotion.format(v / u[1], math.max(1, decimals)) .. u[2]
        break
      end
    end
  else
    s = bemotion.format(v, decimals)
  end
  return o.prefix .. s .. o.suffix
end

-- Rows in display order (sorted on request), each with its original index.
local function rows_of(o)
  local rows = {}
  for i, r in ipairs(o.rows) do rows[#rows + 1] = { label = r.label, value = r.value, color = r.color, index = i } end
  if o.sort == "Ascending" then
    table.sort(rows, function(a, b) return a.value < b.value end)
  elseif o.sort == "Descending" then
    table.sort(rows, function(a, b) return a.value > b.value end)
  end
  return rows
end

-- Highlighting one row (by its position in the data table) fades the others.
local function emphasis(o, row)
  local h = math.floor(o.highlight + 0.5)
  if h <= 0 or h == row.index then return 1 end
  return 0.3
end

local function data_max(o)
  local m = 0
  for _, r in ipairs(o.rows) do m = math.max(m, r.value) end
  if o.max_value and o.max_value > 0 then return o.max_value end
  return nice_max(m)
end

-- Draws the background and title; returns the remaining content box.
local function frame(o, g)
  local w, h = o.width, o.height
  local x0, y0, x1, y1 = -w / 2, -h / 2, w / 2, h / 2
  if o.background and o.background.a > 0 then g.rect(x0, y0, w, h, o.background, 18) end
  local pad = math.min(w, h) * 0.06
  x0, y0, x1, y1 = x0 + pad, y0 + pad, x1 - pad, y1 - pad
  if o.title ~= "" then
    local size = o.font_size * 1.35
    g.text(o.title, x0, y0 + size / 2, size, o.text_color, "left", true)
    y0 = y0 + size * 1.6
  end
  return x0, y0, x1, y1
end

-- Horizontal (or vertical) grid with value labels.
local function grid(o, g, bx0, by0, bx1, by1, max, vertical)
  if not o.show_grid then return end
  local n = math.max(1, math.floor(o.grid_lines + 0.5))
  local fs = o.font_size
  for k = 0, n do
    if vertical then
      local x = bx0 + (bx1 - bx0) * k / n
      g.line(x, by0, x, by1, 1.5, o.grid_color)
    else
      local y = by1 - (by1 - by0) * k / n
      g.line(bx0, y, bx1, y, 1.5, o.grid_color)
      g.text(value_text(o, max * k / n), bx0 - fs * 0.4, y, fs * 0.75, bemotion.with_alpha(o.text_color, 0.55), "right")
    end
  end
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
      { id = "outline", label = "Outline Width", type = "number", default = 0, min = 0, max = 40,
        format = "%.1f px", group = "Chart" },
      { id = "outline_color", label = "Outline Color", type = "color", default = "#FFFFFF", group = "Chart" },
      { id = "track", label = "Show Track", type = "toggle", default = false, group = "Chart" },
      { id = "track_color", label = "Track Color", type = "color", default = "#FFFFFF14", group = "Chart" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Chart" } },
    animation_params(), trim_params(), data_params(), grid_params(),
    text_params({ { id = "value_inside", label = "Values Inside", type = "toggle", default = false, group = "Text" } })),
  draw = function(o, g)
    local rows = rows_of(o)
    local n = #rows
    local x0, y0, x1, y1 = frame(o, g)
    if n == 0 then return end
    local max = data_max(o)
    local fs = o.font_size
    local ta, tb = trim(o)
    local columns = o.orientation ~= "Bars"

    -- One bar: from `base` along the length axis, trimmed, with optional track and outline.
    local function bar(r, i, x, y, w, h, along_x)
      local e, t = item_progress(o, i, n)
      local alpha = emphasis(o, r)
      local color = bemotion.with_alpha(o.single_color and o.bar_color or r.color, alpha)
      local full = (along_x and w or h)
      local len = full * clamp(r.value / max, 0, 1.5) * e
      local radius = math.min(o.corner, (along_x and h or w) / 2)
      if o.track then
        g.rect(x, y, w, h, o.track_color, radius)
      end
      local a, b = len * ta, len * tb
      if b - a > 0.5 then
        local rx, ry, rw, rh
        if along_x then rx, ry, rw, rh = x + a, y, b - a, h
        else rx, ry, rw, rh = x, y + h - b, w, b - a end
        g.rect(rx, ry, rw, rh, color, radius)
        if o.outline > 0 then
          g.rect_outline(rx, ry, rw, rh, bemotion.with_alpha(o.outline_color, alpha), o.outline, radius)
        end
      end
      return e, t, len, alpha
    end

    if columns then
      local label_h = o.show_labels and fs * 1.8 or 0
      local value_h = (o.show_values and not o.value_inside) and fs * 1.6 or 0
      local axis_w = o.show_grid and g.measure(value_text(o, max), fs * 0.75) + fs * 0.6 or 0
      local bx0, by0, bx1, by1 = x0 + axis_w, y0 + value_h, x1, y1 - label_h
      grid(o, g, bx0, by0, bx1, by1, max, false)
      local slot = (bx1 - bx0) / n
      local bw = slot * (1 - clamp(o.gap / 100, 0, 0.9))
      for i, r in ipairs(rows) do
        local cx = bx0 + slot * (i - 0.5)
        local e, t, len, alpha = bar(r, i, cx - bw / 2, by0, bw, by1 - by0, false)
        if o.show_values and t > 0 then
          local tc = bemotion.with_alpha(o.text_color, clamp(t * 3, 0, 1) * alpha)
          local y = o.value_inside and (by1 - len + fs * 0.9) or (by1 - len * tb - fs * 0.8)
          if not o.value_inside or len > fs * 1.8 then g.text(value_text(o, r.value * e), cx, y, fs, tc, "center", true) end
        end
        if o.show_labels then
          g.text(r.label, cx, by1 + label_h * 0.55, fs * 0.9, bemotion.with_alpha(o.text_color, 0.7 * alpha), "center")
        end
      end
    else
      local label_w = 0
      if o.show_labels then
        for _, r in ipairs(rows) do label_w = math.max(label_w, g.measure(r.label, fs * 0.9)) end
        label_w = label_w + fs
      end
      local value_w = (o.show_values and not o.value_inside) and g.measure(value_text(o, max), fs) + fs or 0
      local bx0, by0, bx1, by1 = x0 + label_w, y0, x1 - value_w, y1
      grid(o, g, bx0, by0, bx1, by1, max, true)
      local slot = (by1 - by0) / n
      local bh = slot * (1 - clamp(o.gap / 100, 0, 0.9))
      for i, r in ipairs(rows) do
        local cy = by0 + slot * (i - 0.5)
        local e, t, len, alpha = bar(r, i, bx0, cy - bh / 2, bx1 - bx0, bh, true)
        if o.show_labels then
          g.text(r.label, bx0 - fs * 0.5, cy, fs * 0.9, bemotion.with_alpha(o.text_color, 0.7 * alpha), "right")
        end
        if o.show_values and t > 0 then
          local tc = bemotion.with_alpha(o.text_color, clamp(t * 3, 0, 1) * alpha)
          if o.value_inside then
            if len > fs * 3 then g.text(value_text(o, r.value * e), bx0 + len - fs * 0.5, cy, fs, tc, "right", true) end
          else
            g.text(value_text(o, r.value * e), bx0 + len * tb + fs * 0.4, cy, fs, tc, "left", true)
          end
        end
      end
    end
  end,
}

-- --- line / area chart ------------------------------------------------------------

-- Catmull-Rom through the points; returns { {x, y, u}, ... } with u = data index - 1.
local function dense_points(pts, smooth)
  local out = {}
  local n = #pts
  for i = 1, n do
    if i == n or not smooth then
      out[#out + 1] = { pts[i][1], pts[i][2], i - 1 }
    else
      local p0, p1, p2, p3 = pts[math.max(1, i - 1)], pts[i], pts[i + 1], pts[math.min(n, i + 2)]
      for s = 0, 11 do
        local t = s / 12
        local t2, t3 = t * t, t * t * t
        local function cr(a, b, c, d)
          return 0.5 * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3)
        end
        out[#out + 1] = { cr(p0[1], p1[1], p2[1], p3[1]), cr(p0[2], p1[2], p2[2], p3[2]), i - 1 + t }
      end
    end
  end
  return out
end

-- The part of a dense polyline between parameters ua and ub (flat x, y list).
local function slice(dense, ua, ub)
  local out = {}
  for k = 1, #dense do
    local p = dense[k]
    local prev = dense[k - 1]
    if prev and prev[3] < ua and p[3] > ua then -- entering
      local f = (ua - prev[3]) / (p[3] - prev[3])
      out[#out + 1] = prev[1] + (p[1] - prev[1]) * f
      out[#out + 1] = prev[2] + (p[2] - prev[2]) * f
    end
    if p[3] >= ua and p[3] <= ub then
      out[#out + 1] = p[1]
      out[#out + 1] = p[2]
    elseif prev and prev[3] < ub and p[3] > ub then -- leaving
      local f = (ub - prev[3]) / (p[3] - prev[3])
      out[#out + 1] = prev[1] + (p[1] - prev[1]) * f
      out[#out + 1] = prev[2] + (p[2] - prev[2]) * f
      break
    end
  end
  return out
end

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
      { id = "smooth", label = "Smooth", type = "toggle", default = true, group = "Chart" },
      { id = "fill", label = "Fill Area", type = "toggle", default = true, group = "Chart" },
      { id = "fill_opacity", label = "Fill Opacity", type = "number", default = 22, min = 0, max = 100,
        format = "%.0f%%", group = "Chart" },
      { id = "dots", label = "Dots", type = "toggle", default = true, group = "Chart" },
      { id = "dot_size", label = "Dot Size", type = "number", default = 9, min = 0, max = 80,
        format = "%.1f px", group = "Chart" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Chart" } },
    animation_params(), trim_params(),
    { { id = "highlight", label = "Highlight Point", type = "number", default = 0, min = 0, max = 100, step = 0.05,
        format = "%.0f (0 = none)", group = "Sorting & Highlight" } },
    grid_params(), text_params()),
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
    grid(o, g, bx0, by0, bx1, by1, max, false)

    local px0, px1 = bx0 + fs * 1.2, bx1 - fs * 0.4 -- keep the end points clear of the axis labels
    local pts = {}
    for i = 1, n do
      local x = n == 1 and (px0 + px1) / 2 or px0 + (px1 - px0) * (i - 1) / (n - 1)
      pts[i] = { x, by1 - (by1 - by0) * clamp(rows[i].value / max, 0, 1.5) }
    end

    -- Revealed along x; trim cuts the line further (the stagger parameter is not used here).
    local p = bemotion.ease(EASE_IDS[o.easing] or "ease_out", clamp(o.progress / 100, 0, 1))
    local ta, tb = trim(o)
    local span = math.max(0, n - 1)
    local reveal = p * span
    local ua, ub = ta * span, math.min(tb * span, reveal)
    local line = {}
    if n > 1 and ub > ua then line = slice(dense_points(pts, o.smooth), ua, ub) end

    if o.fill and #line >= 4 then
      local area = { line[1], by1 }
      for k = 1, #line do area[#area + 1] = line[k] end
      area[#area + 1] = line[#line - 1]
      area[#area + 1] = by1
      g.polygon(area, bemotion.with_alpha(o.line_color, o.fill_opacity / 100))
    end
    if #line >= 4 and o.line_width > 0 then g.polyline(line, o.line_width, o.line_color) end

    local h = math.floor(o.highlight + 0.5)
    for i, r in ipairs(rows) do
      local x, y = pts[i][1], pts[i][2]
      local u = i - 1
      local inside = u >= ua - 1e-4 and u <= tb * span + 1e-4
      local shown = inside and clamp((reveal - u) * 3 + 1, 0, 1) or 0 -- pops in when the line reaches it
      local alpha = (h <= 0 or h == i) and 1 or 0.3
      if o.dots and shown > 0 and o.dot_size > 0 then
        local size = o.dot_size * bemotion.ease("back_out", shown) * (h == i and 1.5 or 1)
        g.circle(x, y, size, bemotion.with_alpha(o.line_color, alpha))
        g.circle(x, y, size * 0.45, o.background.a > 0 and o.background or "#101012")
      end
      if o.show_values and shown > 0 then
        g.text(value_text(o, r.value), x, y - fs * 1.1, fs, bemotion.with_alpha(o.text_color, shown * alpha), "center", true)
      end
      if o.show_labels then
        g.text(r.label, x, by1 + label_h * 0.55, fs * 0.9, bemotion.with_alpha(o.text_color, 0.7 * alpha), "center")
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
      { id = "pop_out", label = "Highlight Pop", type = "number", default = 6, min = 0, max = 30,
        format = "%.0f%%", group = "Chart" },
      { id = "legend", label = "Legend", type = "toggle", default = true, group = "Chart" },
      { id = "percent", label = "Show Percent", type = "toggle", default = true, group = "Chart" },
      { id = "center_text", label = "Center Text", type = "text", default = "", group = "Chart" },
      { id = "center_total", label = "Total in Center", type = "toggle", default = false, group = "Chart" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Chart" } },
    animation_params(), trim_params(), data_params(), text_params()),
  draw = function(o, g)
    local rows = rows_of(o)
    local n = #rows
    local x0, y0, x1, y1 = frame(o, g)
    if n == 0 then return end
    local fs = o.font_size
    local total = 0
    for _, r in ipairs(rows) do total = total + math.max(0, r.value) end
    if total <= 0 then return end
    local ta, tb = trim(o)

    local legend_w = 0
    if o.legend then
      for _, r in ipairs(rows) do
        legend_w = math.max(legend_w, g.measure(r.label .. "  100%", fs) + fs * 2)
      end
    end
    local cx = (x0 + x1 - legend_w) / 2
    local cy = (y0 + y1) / 2
    local radius = math.max(1, math.min((x1 - x0 - legend_w) / 2, (y1 - y0) / 2) * 0.92)
    local inner = radius * clamp(o.hole / 100, 0, 0.95)
    local h = math.floor(o.highlight + 0.5)

    local angle = o.start_angle
    for i, r in ipairs(rows) do
      local share = math.max(0, r.value) / total
      local e = item_progress(o, i, n)
      local sweep = 360 * share
      local gap = math.min(o.slice_gap, sweep * 0.4)
      local usable = math.max(0, sweep - gap)
      local a0 = angle + gap / 2 + usable * ta * e
      local a1 = angle + gap / 2 + usable * tb * e
      local grow = inner + (radius - inner) * (0.85 + 0.15 * e)
      local ox, oy = 0, 0
      if h == r.index then -- the highlighted slice moves out a little
        local mid = math.rad((a0 + a1) / 2 - 90)
        local d = radius * o.pop_out / 100
        ox, oy = math.cos(mid) * d, math.sin(mid) * d
      end
      if a1 > a0 then g.arc(cx + ox, cy + oy, inner, grow, a0, a1, bemotion.with_alpha(r.color, emphasis(o, r))) end
      angle = angle + sweep
    end

    local center = o.center_text
    if o.center_total then center = value_text(o, total * clamp(o.progress / 100, 0, 1)) end
    if center ~= "" and inner > fs then
      local p = clamp(o.progress / 100 * 3, 0, 1)
      g.text(center, cx, cy, math.min(inner * 0.45, fs * 2), bemotion.with_alpha(o.text_color, p), "center", true)
    end

    if o.legend then
      local lx = x1 - legend_w + fs
      local line = fs * 1.7
      local ly = cy - line * (n - 1) / 2
      for i, r in ipairs(rows) do
        local _, t = item_progress(o, i, n)
        local a = clamp(t * 3, 0, 1) * emphasis(o, r)
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
