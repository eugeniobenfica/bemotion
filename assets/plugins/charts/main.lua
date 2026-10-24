-- Charts for bemotion: columns / bars, line / area, pie / donut, tables and maps.
--
-- Every chart reads its data from the object's rows (label, value, color) and
-- animates with "Progress", which gets intro keyframes when the object is
-- created. "Trim Start / End" work like trim paths on every bar, the line and
-- every slice. Tables show a grid of text cells whose rows come in one after
-- another. Charts and tables import CSV files (Properties > Data > Import CSV).
-- See docs/PLUGINS.md for the plugin API.

local PALETTE = { "#E8B931", "#4F8EF7", "#3DBE8B", "#E5604D", "#A77BF3", "#3CC6D6", "#F28C38" }

local SAMPLE = {
  { "Jan", 42, PALETTE[1] },
  { "Feb", 58, PALETTE[2] },
  { "Mar", 51, PALETTE[3] },
  { "Apr", 76, PALETTE[4] },
  { "May", 93, PALETTE[5] },
}

local SAMPLE_TABLE = {
  header = { "Player", "Team", "Goals", "Assists" },
  rows = {
    { "Ana Souza", "Lions", "12", "7" },
    { "Bruno Lima", "Hawks", "9", "11" },
    { "Carla Dias", "Lions", "8", "4" },
    { "Diego Alves", "Wolves", "6", "9" },
  },
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

-- --- CSV import ---------------------------------------------------------------------

-- Charts: the first column of numbers gives the values, the first column of words the
-- labels; a header line above the numbers becomes the title (when there is none yet).
local function import_chart(cells, o)
  local width = 0
  for _, r in ipairs(cells) do width = math.max(width, #r) end
  local value_col, label_col
  for c = 1, width do
    local numeric, filled = 0, 0
    for _, r in ipairs(cells) do
      local cell = r[c]
      if cell and cell ~= "" then
        filled = filled + 1
        if bemotion.number(cell) then numeric = numeric + 1 end
      end
    end
    if filled > 0 then
      if numeric * 2 >= filled then
        value_col = value_col or c
      else
        label_col = label_col or c
      end
    end
  end
  if not value_col then error("no column of numbers found") end
  local rows, params = {}, {}
  for i, r in ipairs(cells) do
    local v = bemotion.number(r[value_col] or "")
    if v then
      local label = label_col and r[label_col] or ("Item " .. (#rows + 1))
      rows[#rows + 1] = { label, v, PALETTE[#rows % #PALETTE + 1] }
    elseif i == 1 and o.title == "" and (r[value_col] or "") ~= "" then
      params.title = r[value_col]
    end
  end
  if #rows == 0 then error("no line with a number was found") end
  return { rows = rows, params = params }
end

-- Tables: the first line is the header, unless it is all numbers (then columns get names).
local function import_table(cells)
  local first = cells[1] or {}
  local all_numbers = #first > 0
  for _, cell in ipairs(first) do
    if cell ~= "" and not bemotion.number(cell) then all_numbers = false end
  end
  local header, start = first, 2
  if all_numbers then
    header, start = {}, 1
    for c = 1, #first do header[c] = "Column " .. c end
  end
  local rows = {}
  for i = start, #cells do rows[#rows + 1] = cells[i] end
  return { table = { header = header, rows = rows } }
end

-- --- column / bar chart ---------------------------------------------------------

bemotion.register_object {
  id = "bars",
  import_csv = import_chart,
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
  import_csv = import_chart,
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
  import_csv = import_chart,
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

-- --- table ---------------------------------------------------------------------------

bemotion.register_object {
  id = "table",
  name = "Table",
  description = "A table of text and numbers whose rows come in one after another.",
  icon = "table",
  size = { 1100, 520 },
  table = SAMPLE_TABLE,
  import_csv = import_table,
  params = concat(
    { { id = "show_header", label = "Header Row", type = "toggle", default = true, group = "Table" },
      { id = "header_color", label = "Header Color", type = "color", default = "#E8B931", group = "Table" },
      { id = "header_text", label = "Header Text", type = "color", default = "#141417", group = "Table" },
      { id = "row_color", label = "Row Color", type = "color", default = "#FFFFFF10", group = "Table" },
      { id = "stripes", label = "Stripes", type = "toggle", default = true, group = "Table" },
      { id = "stripe_color", label = "Stripe Color", type = "color", default = "#FFFFFF1C", group = "Table" },
      { id = "corner", label = "Corner Radius", type = "number", default = 8, min = 0, max = 60,
        format = "%.0f px", group = "Table" },
      { id = "row_gap", label = "Row Gap", type = "number", default = 4, min = 0, max = 40,
        format = "%.0f px", group = "Table" },
      { id = "cell_padding", label = "Cell Padding", type = "number", default = 18, min = 0, max = 120,
        format = "%.0f px", group = "Table" },
      { id = "numbers_right", label = "Numbers Right", type = "toggle", default = true, group = "Table" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Table" } },
    { { id = "progress", label = "Progress", type = "number", default = 100, min = 0, max = 100,
        format = "%.0f%%", group = "Animation", intro = { from = 0, duration = 1.4 } },
      { id = "stagger", label = "Stagger", type = "number", default = 60, min = 0, max = 100,
        format = "%.0f%%", group = "Animation" },
      { id = "easing", label = "Easing", type = "choice", choices = EASINGS, default = "Ease Out", group = "Animation" },
      { id = "reveal", label = "Reveal", type = "choice", choices = { "Slide Up", "Slide In", "Fade" },
        default = "Slide Up", group = "Animation" } },
    { { id = "sort", label = "Sort", type = "choice", choices = { "None", "Ascending", "Descending" },
        default = "None", group = "Sorting & Highlight" },
      { id = "sort_column", label = "Sort By Column", type = "number", default = 1, min = 1, max = 50, step = 0.05,
        format = "%.0f", group = "Sorting & Highlight" },
      { id = "highlight", label = "Highlight Row", type = "number", default = 0, min = 0, max = 100, step = 0.05,
        format = "%.0f (0 = none)", group = "Sorting & Highlight" },
      { id = "highlight_color", label = "Highlight Color", type = "color", default = "#FFFFFF38",
        group = "Sorting & Highlight" } },
    { { id = "title", label = "Title", type = "text", default = "", group = "Text" },
      { id = "font_size", label = "Font Size", type = "number", default = 28, min = 6, max = 200,
        format = "%.0f px", group = "Text" },
      { id = "text_color", label = "Text Color", type = "color", default = "#FFFFFF", group = "Text" },
      { id = "bold_first", label = "Bold First Column", type = "toggle", default = true, group = "Text" } }),
  draw = function(o, g)
    local header, rows = o.table.header, o.table.rows
    local ncols = #header
    for _, r in ipairs(rows) do ncols = math.max(ncols, #r) end
    local x0, y0, x1, y1 = frame(o, g)
    if ncols == 0 then return end
    -- Rows in display order: sorted by a column on request (numbers as numbers), keeping their
    -- position in the data for "Highlight Row".
    local order = {}
    for i, r in ipairs(rows) do order[i] = { cells = r, index = i } end
    if o.sort ~= "None" then
      local c = math.floor(o.sort_column + 0.5)
      local descending = o.sort == "Descending"
      table.sort(order, function(a, b)
        local x, y = a.cells[c] or "", b.cells[c] or ""
        local nx, ny = bemotion.number(x), bemotion.number(y)
        if nx and ny then
          if nx ~= ny then
            if descending then return nx > ny end
            return nx < ny
          end
        elseif x ~= y then
          if descending then return x > y end
          return x < y
        end
        return a.index < b.index
      end)
    end
    local lines = {}
    if o.show_header then lines[1] = { cells = header, header = true } end
    for i, r in ipairs(order) do lines[#lines + 1] = { cells = r.cells, index = r.index, stripe = i } end
    if #lines == 0 then return end

    -- Columns as wide as their longest text, then scaled to the width of the object.
    local fs, pad = o.font_size, o.cell_padding
    local widths, total = {}, 0
    for c = 1, ncols do
      local w = 0
      for _, line in ipairs(lines) do w = math.max(w, g.measure(line.cells[c] or "", fs)) end
      widths[c] = w + pad * 2
      total = total + widths[c]
    end
    local scale = (x1 - x0) / math.max(total, 1)
    for c = 1, ncols do widths[c] = widths[c] * scale end
    local gap = o.row_gap
    local row_h = math.min(fs * 2.1, ((y1 - y0) - gap * (#lines - 1)) / #lines)
    if row_h <= 2 then return end
    local size = math.min(fs * math.min(1, scale), row_h * 0.5)
    -- The table sits in the middle of its frame (below the title, if any).
    y0 = y0 + math.max(0, ((y1 - y0) - (#lines * row_h + gap * (#lines - 1))) / 2)

    -- Numbers line up on the right of their column.
    local numeric = {}
    for c = 1, ncols do
      local count, filled = 0, 0
      for _, r in ipairs(rows) do
        if (r[c] or "") ~= "" then
          filled = filled + 1
          if bemotion.number(r[c]) then count = count + 1 end
        end
      end
      numeric[c] = o.numbers_right and filled > 0 and count * 2 >= filled
    end

    local h = math.floor(o.highlight + 0.5)
    for i, line in ipairs(lines) do
      local e, t = item_progress(o, i, #lines)
      local alpha = clamp(t * 2, 0, 1)
      local y = y0 + (i - 1) * (row_h + gap)
      local dx, dy = 0, 0
      if o.reveal == "Slide Up" then dy = (1 - e) * row_h * 0.8
      elseif o.reveal == "Slide In" then dx = -(1 - e) * (x1 - x0) * 0.15 end
      if alpha > 0 then
        local fill
        if line.header then fill = o.header_color
        elseif h > 0 and line.index == h then fill = o.highlight_color
        elseif o.stripes and line.stripe % 2 == 0 then fill = o.stripe_color
        else fill = o.row_color end
        g.rect(x0 + dx, y + dy, x1 - x0, row_h, bemotion.with_alpha(fill, alpha), math.min(o.corner, row_h / 2))
        local x = x0 + dx
        local color = line.header and o.header_text or o.text_color
        local dim = (h > 0 and not line.header and line.index ~= h) and 0.55 or 1
        for c = 1, ncols do
          local text = line.cells[c] or ""
          if text ~= "" then
            local bold = line.header or (o.bold_first and c == 1)
            local tc = bemotion.with_alpha(color, alpha * dim)
            local cy = y + dy + row_h / 2
            if numeric[c] and not line.header then
              g.text(text, x + widths[c] - pad * scale, cy, size, tc, "right", bold)
            elseif numeric[c] then
              g.text(text, x + widths[c] - pad * scale, cy, size, tc, "right", true)
            else
              g.text(text, x + pad * scale, cy, size, tc, "left", bold)
            end
          end
          x = x + widths[c]
        end
      end
    end
  end,
}

-- --- maps (choropleth) --------------------------------------------------------------

-- Countries or states colored by their value. The outlines come from Natural Earth (public
-- domain), made into maps/*.lua by tools/maps/make-maps.py. Rows name a region by its name
-- (English, Portuguese, Spanish, French, German, Italian, local) or code ("BR", "BRA", "SP").
local MAPS = { "World", "Brazil", "United States", "Canada", "Australia", "India", "China", "South Africa" }
local MAP_FILES = {
  ["World"] = "maps/world.lua", ["Brazil"] = "maps/brazil.lua", ["United States"] = "maps/usa.lua",
  ["Canada"] = "maps/canada.lua", ["Australia"] = "maps/australia.lua", ["India"] = "maps/india.lua",
  ["China"] = "maps/china.lua", ["South Africa"] = "maps/south_africa.lua",
}

local SAMPLE_MAP = { -- population, millions
  { "China", 1410, PALETTE[1] }, { "India", 1430, PALETTE[1] }, { "United States", 335, PALETTE[1] },
  { "Indonesia", 277, PALETTE[1] }, { "Pakistan", 240, PALETTE[1] }, { "Nigeria", 224, PALETTE[1] },
  { "Brazil", 216, PALETTE[1] }, { "Bangladesh", 173, PALETTE[1] }, { "Russia", 144, PALETTE[1] },
  { "Mexico", 128, PALETTE[1] }, { "Japan", 124, PALETTE[1] }, { "Ethiopia", 126, PALETTE[1] },
  { "Egypt", 112, PALETTE[1] }, { "Germany", 84, PALETTE[1] }, { "France", 68, PALETTE[1] },
  { "United Kingdom", 68, PALETTE[1] }, { "South Africa", 60, PALETTE[1] }, { "Argentina", 46, PALETTE[1] },
  { "Canada", 39, PALETTE[1] }, { "Australia", 26, PALETTE[1] },
}

-- Lower case without accents, as the maps' keys are.
local ACCENTS = {}
for plain, chars in pairs({ a = "áàâãäåÁÀÂÃÄÅ", e = "éèêëÉÈÊË", i = "íìîïÍÌÎÏ", o = "óòôõöÓÒÔÕÖ", u = "úùûüÚÙÛÜ",
                            c = "çÇ", n = "ñÑ", y = "ýÿÝ" }) do
  for _, code in utf8.codes(chars) do ACCENTS[utf8.char(code)] = plain end
end
local function plain_name(s)
  s = s:gsub("[\xC0-\xF4][\x80-\xBF]*", function(ch) return ACCENTS[ch] or ch end)
  return (s:lower():gsub("^%s+", ""):gsub("%s+$", ""))
end

-- The map and an index of its region names (built once).
local loaded = {}
local function map_of(name)
  local file = MAP_FILES[name] or MAP_FILES["World"]
  if not loaded[file] then
    local m = bemotion.include(file)
    m.index = {}
    for i, region in ipairs(m.regions) do
      for _, key in ipairs(region.keys) do m.index[key] = i end
    end
    loaded[file] = m
  end
  return loaded[file]
end

bemotion.register_object {
  id = "map",
  import_csv = import_chart,
  name = "Map",
  description = "Countries or states colored by their value, coming in one after another.",
  icon = "map",
  size = { 1400, 760 },
  rows = SAMPLE_MAP,
  params = concat(
    { { id = "map", label = "Map", type = "choice", choices = MAPS, default = "World", group = "Map" },
      { id = "low_color", label = "Low Color", type = "color", default = "#3A4A72", group = "Map" },
      { id = "high_color", label = "High Color", type = "color", default = "#E8B931", group = "Map" },
      { id = "scale", label = "Color Scale", type = "choice", choices = { "Linear", "Logarithmic", "By Rank" },
        default = "Linear", group = "Map" },
      { id = "row_colors", label = "Use Row Colors", type = "toggle", default = false, group = "Map" },
      { id = "empty_color", label = "No Data Color", type = "color", default = "#FFFFFF1A", group = "Map" },
      { id = "border_color", label = "Border Color", type = "color", default = "#0E1018", group = "Map" },
      { id = "border", label = "Border Width", type = "number", default = 0.8, min = 0, max = 10, step = 0.05,
        format = "%.1f px", group = "Map" },
      { id = "legend", label = "Legend", type = "toggle", default = true, group = "Map" },
      { id = "zoom", label = "Zoom", type = "number", default = 100, min = 50, max = 2000, format = "%.0f%%", group = "View" },
      { id = "center_x", label = "Center X", type = "number", default = 50, min = 0, max = 100, step = 0.1,
        format = "%.1f%%", group = "View" },
      { id = "center_y", label = "Center Y", type = "number", default = 50, min = 0, max = 100, step = 0.1,
        format = "%.1f%%", group = "View" },
      { id = "background", label = "Background", type = "color", default = "#00000000", group = "Map" } },
    animation_params(), data_params(),
    text_params()),
  draw = function(o, g)
    local x0, y0, x1, y1 = frame(o, g)
    local m = map_of(o.map)
    local fs = o.font_size

    -- The rows that name a region, in display order (sorting decides which comes in first).
    local rows = rows_of(o)
    local of_region, matched = {}, {}
    local lo, hi = math.huge, -math.huge
    for _, r in ipairs(rows) do
      local i = m.index[plain_name(r.label)]
      if i and not of_region[i] then
        of_region[i] = r
        matched[#matched + 1] = { region = i, row = r }
        lo, hi = math.min(lo, r.value), math.max(hi, r.value)
      end
    end
    local legend_h = (o.legend and #matched > 0) and fs * 2.6 or 0

    -- Fitted into the box, then zoomed around the chosen center.
    local bw, bh = x1 - x0, y1 - y0 - legend_h
    local s = math.min(bw / m.width, bh / m.height) * clamp(o.zoom, 10, 5000) / 100
    local cx, cy = m.width * o.center_x / 100, m.height * o.center_y / 100
    local mx, my = (x0 + x1) / 2, y0 + bh / 2
    local function at(px, py) return mx + (px - cx) * s, my + (py - cy) * s end
    g.clip(x0, y0, bw, bh)

    local order = {}
    for k, entry in ipairs(matched) do order[entry.region] = k end
    -- Where a value sits on the color scale (0: low color, 1: high color).
    local ranked = {}
    for _, entry in ipairs(matched) do ranked[#ranked + 1] = entry.row.value end
    table.sort(ranked)
    local function scale(v)
      if hi <= lo then return 1 end
      if o.scale == "Logarithmic" and lo > 0 then return (math.log(v) - math.log(lo)) / (math.log(hi) - math.log(lo)) end
      if o.scale == "By Rank" then
        for k, x in ipairs(ranked) do if x >= v then return #ranked > 1 and (k - 1) / (#ranked - 1) or 1 end end
        return 1
      end
      return (v - lo) / (hi - lo)
    end
    for i, region in ipairs(m.regions) do
      local r = of_region[i]
      local color = o.empty_color
      local alpha = 1
      if r then
        local e = item_progress(o, order[i], #matched)
        local t = clamp(scale(r.value), 0, 1)
        local full = o.row_colors and r.color or bemotion.mix(o.low_color, o.high_color, t)
        color = bemotion.mix(o.empty_color, full, clamp(e, 0, 1))
        alpha = emphasis(o, r)
        color = bemotion.with_alpha(color, alpha)
      end
      for _, ring in ipairs(region.rings) do
        local pts = {}
        for k = 1, #ring, 2 do
          local x, y = at(ring[k], ring[k + 1])
          pts[#pts + 1] = x
          pts[#pts + 1] = y
        end
        g.polygon(pts, color)
        if o.border > 0 then g.polyline(pts, o.border, o.border_color, true) end
      end
    end

    -- Names and values on the regions with data, as they come in. The biggest values first;
    -- a label that would cover one already placed is left out (dense areas stay readable).
    if o.show_labels or o.show_values then
      local placed = {}
      local by_value = {}
      for _, entry in ipairs(matched) do by_value[#by_value + 1] = entry end
      table.sort(by_value, function(a, b) return a.row.value > b.row.value end)
      local function free(bx0, by0, bx1, by1)
        for _, p in ipairs(placed) do
          if bx0 < p[3] and bx1 > p[1] and by0 < p[4] and by1 > p[2] then return false end
        end
        placed[#placed + 1] = { bx0, by0, bx1, by1 }
        return true
      end
      for _, entry in ipairs(by_value) do
        local region, r = m.regions[entry.region], entry.row
        local e, t = item_progress(o, order[entry.region], #matched)
        local x, y = at(region.lx, region.ly)
        local both = o.show_labels and o.show_values
        local half_w = math.max(o.show_labels and g.measure(r.label, fs * 0.75, true) or 0,
                                o.show_values and g.measure(value_text(o, r.value), fs * 0.75) or 0) / 2 + fs * 0.2
        local half_h = (both and fs * 1.1 or fs * 0.55)
        if t > 0 and free(x - half_w, y - half_h, x + half_w, y + half_h) then
          local tc = bemotion.with_alpha(o.text_color, clamp(t * 3, 0, 1) * emphasis(o, r))
          if o.show_labels and o.show_values then
            g.text(r.label, x, y - fs * 0.55, fs * 0.75, tc, "center", true)
            g.text(value_text(o, r.value * e), x, y + fs * 0.45, fs * 0.75, tc, "center")
          elseif o.show_labels then
            g.text(r.label, x, y, fs * 0.75, tc, "center", true)
          else
            g.text(value_text(o, r.value * e), x, y, fs * 0.75, tc, "center", true)
          end
        end
      end
    end
    g.unclip()

    -- The legend: the color scale from the lowest value to the highest.
    if legend_h > 0 and not o.row_colors then
      local ly = y1 - legend_h * 0.45
      local lw = math.min(bw * 0.4, fs * 14)
      local steps = 24
      for k = 0, steps - 1 do
        g.rect(x0 + lw * k / steps, ly - fs * 0.25, lw / steps + 0.5, fs * 0.5, bemotion.mix(o.low_color, o.high_color, k / (steps - 1)))
      end
      local tc = bemotion.with_alpha(o.text_color, 0.7)
      g.text(value_text(o, lo), x0, ly + fs * 0.85, fs * 0.7, tc, "left")
      g.text(value_text(o, hi), x0 + lw, ly + fs * 0.85, fs * 0.7, tc, "right")
    end
  end,
}
