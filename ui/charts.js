// A small SVG line chart for the time series and the vertical profile of a point.
// Missing values break the line.

import {el, replace, svg} from './dom.js';
import {formatTick, formatValue, niceTicks} from './format.js';

// spec = {
//   points: [[key, value], ...]   key is the time or the level, value may be null
//   vertical: draw the keys on the y axis (a profile), otherwise on the x axis
//   invertKeys: larger keys at the bottom (pressure) when vertical
//   keyFormat: key -> label, keyTicks: tick keys (optional)
//   mark: a highlighted key (the selected time or level)
//   title, describe: point -> text for the readout
//   onClick: point -> void
//   onHover: point -> void, null when the pointer leaves the chart
// }
// Returns {setMark(key)}, which moves the mark without drawing the chart again.
export function lineChart(container, spec)
{
  const width = 360;
  const height = spec.vertical ? 260 : 200;
  const m = {left: 56, right: 12, top: 10, bottom: 34};
  const w = width - m.left - m.right;
  const h = height - m.top - m.bottom;

  const isValid = (v) => v !== null && v !== undefined && Number.isFinite(v);
  const valid = spec.points.filter((p) => isValid(p[1]));
  if (valid.length === 0)
  {
    replace(container, spec.title ? el('div', {class: 'chart-title'}, spec.title) : null,
        el('div', {class: 'muted small'}, 'No values at this point'));
    return {setMark: () => {}};
  }

  // The ranges of the keys and the values
  const keys = spec.points.map((p) => p[0]);
  const values = valid.map((p) => p[1]);
  let kMin = Math.min(...keys);
  let kMax = Math.max(...keys);
  let vMin = Math.min(...values);
  let vMax = Math.max(...values);
  if (kMin === kMax)
  {
    kMin -= 1;
    kMax += 1;
  }
  if (vMin === vMax)
  {
    const d = Math.abs(vMin) * 0.05 || 1;
    vMin -= d;
    vMax += d;
  }
  const valueTicks = niceTicks(vMin, vMax, spec.vertical ? 4 : 5);
  if (valueTicks.length)
  {
    vMin = Math.min(vMin, valueTicks[0]);
    vMax = Math.max(vMax, valueTicks[valueTicks.length - 1]);
  }
  const keyTicks = spec.keyTicks || niceTicks(kMin, kMax, spec.vertical ? 5 : 4);

  // Positions in the chart
  const keyPos = (k) => {
    const f = (k - kMin) / (kMax - kMin);
    if (!spec.vertical)
      return m.left + f * w;
    return spec.invertKeys ? m.top + f * h : m.top + h - f * h;
  };
  const valuePos = (v) => {
    const f = (v - vMin) / (vMax - vMin);
    return spec.vertical ? m.left + f * w : m.top + h - f * h;
  };
  const xy = (p) => spec.vertical ? [valuePos(p[1]), keyPos(p[0])] : [keyPos(p[0]), valuePos(p[1])];

  const root = svg('svg', {viewBox: `0 0 ${width} ${height}`, class: 'chart', role: 'img',
                           'aria-label': spec.title || 'Chart'});

  const keyLabel = (k) => spec.keyFormat ? spec.keyFormat(k) : formatTick(k, keyTicks.step || 1);
  const valueLabel = (v) => formatTick(v, valueTicks.step || 1);

  // Grid lines and tick labels
  const gridLine = (pos, alongKeys) => {
    // alongKeys: the line marks a key
    const vertical = spec.vertical ? !alongKeys : alongKeys;
    return vertical ? svg('line', {x1: pos, x2: pos, y1: m.top, y2: m.top + h, class: 'grid'})
                    : svg('line', {x1: m.left, x2: m.left + w, y1: pos, y2: pos, class: 'grid'});
  };
  const label = (pos, text, onXAxis) => onXAxis
      ? svg('text', {x: pos, y: m.top + h + 14, class: 'tick', 'text-anchor': 'middle'}, text)
      : svg('text', {x: m.left - 4, y: pos + 3, class: 'tick', 'text-anchor': 'end'}, text);

  for (const t of valueTicks)
  {
    const pos = valuePos(t);
    root.append(gridLine(pos, false), label(pos, valueLabel(t), spec.vertical));
  }
  for (const t of keyTicks)
  {
    const pos = keyPos(t);
    const inside = spec.vertical ? pos >= m.top - 0.5 && pos <= m.top + h + 0.5
                                 : pos >= m.left - 0.5 && pos <= m.left + w + 0.5;
    if (inside)
      root.append(gridLine(pos, true), label(pos, keyLabel(t), !spec.vertical));
  }
  root.append(svg('rect', {x: m.left, y: m.top, width: w, height: h, class: 'frame'}));

  // The mark: a line at the key, and a ring around its point when it has a value
  const markLine = svg('line', {class: 'mark', visibility: 'hidden'});
  const markRing = svg('circle', {r: 4.5, class: 'mark-ring', visibility: 'hidden'});
  root.append(markLine);

  // The line, broken at missing values
  let d = '';
  let pen = false;
  for (const p of spec.points)
  {
    if (!isValid(p[1]))
    {
      pen = false;
      continue;
    }
    const [x, y] = xy(p);
    d += `${pen ? 'L' : 'M'}${x.toFixed(1)},${y.toFixed(1)}`;
    pen = true;
  }
  root.append(svg('path', {d, class: 'line'}));
  for (const p of valid)
  {
    const [x, y] = xy(p);
    root.append(svg('circle', {cx: x, cy: y, r: valid.length > 60 ? 1.2 : 2.2, class: 'dot'}));
  }
  root.append(markRing);

  const setMark = (key) => {
    const p = key === undefined || key === null ? null : spec.points.find((x) => x[0] === key);
    const inside = key !== undefined && key !== null && key >= kMin && key <= kMax;
    markLine.setAttribute('visibility', inside ? 'visible' : 'hidden');
    markRing.setAttribute('visibility', p && isValid(p[1]) ? 'visible' : 'hidden');
    if (inside)
    {
      const pos = keyPos(key);
      const line = spec.vertical ? {x1: m.left, x2: m.left + w, y1: pos, y2: pos}
                                 : {x1: pos, x2: pos, y1: m.top, y2: m.top + h};
      Object.entries(line).forEach(([k, v]) => markLine.setAttribute(k, v));
    }
    if (p && isValid(p[1]))
    {
      const [x, y] = xy(p);
      markRing.setAttribute('cx', x);
      markRing.setAttribute('cy', y);
    }
  };
  setMark(spec.mark);

  // The readout of the point nearest to the pointer
  const readout = el('div', {class: 'chart-readout small'}, ' ');
  const cursor = svg('line', {class: 'cursor', visibility: 'hidden'});
  root.append(cursor);
  const overlay = svg('rect', {x: m.left, y: m.top, width: w, height: h, class: 'overlay'});
  root.append(overlay);

  let hovered = null;
  const nearest = (event) => {
    const r = root.getBoundingClientRect();
    const pointer = spec.vertical ? (event.clientY - r.top) / r.height * height
                                  : (event.clientX - r.left) / r.width * width;
    let best = null;
    let bestDist = Infinity;
    for (const p of spec.points)
    {
      const dist = Math.abs(keyPos(p[0]) - pointer);
      if (dist < bestDist)
      {
        best = p;
        bestDist = dist;
      }
    }
    return best;
  };

  overlay.addEventListener('mousemove', (event) => {
    const p = nearest(event);
    if (!p)
      return;
    const pos = keyPos(p[0]);
    if (spec.vertical)
      Object.entries({x1: m.left, x2: m.left + w, y1: pos, y2: pos}).forEach(([k, v]) => cursor.setAttribute(k, v));
    else
      Object.entries({x1: pos, x2: pos, y1: m.top, y2: m.top + h}).forEach(([k, v]) => cursor.setAttribute(k, v));
    cursor.setAttribute('visibility', 'visible');
    readout.textContent = spec.describe ? spec.describe(p) : `${keyLabel(p[0])}: ${formatValue(p[1])}`;
    if (spec.onHover && p !== hovered)
      spec.onHover(p);
    hovered = p;
  });
  overlay.addEventListener('mouseleave', () => {
    cursor.setAttribute('visibility', 'hidden');
    readout.textContent = ' ';
    if (spec.onHover)
      spec.onHover(null);
    hovered = null;
  });
  if (spec.onClick)
  {
    overlay.style.cursor = 'pointer';
    overlay.addEventListener('click', (event) => {
      const p = nearest(event);
      if (p)
        spec.onClick(p);
    });
  }

  replace(container, spec.title ? el('div', {class: 'chart-title'}, spec.title) : null, root, readout);
  return {setMark};
}
