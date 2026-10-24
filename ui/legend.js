// The legend of an image. The colours are computed as the server computes them
// (Plugin.cpp): a colour map is interpolated between its stops, an image without a
// colour map uses a brightness ramp of one hue, and a difference image runs from blue
// through white to red.

import {el, replace} from './dom.js';
import {formatTick, formatValue, niceTicks} from './format.js';

// "AARRGGBB" -> [r, g, b, a] with a in 0..1
function argb(hex)
{
  const n = parseInt(hex, 16) >>> 0;
  return [(n >>> 16) & 255, (n >>> 8) & 255, n & 255, ((n >>> 24) & 255) / 255];
}

// ColorMapFile::getSmoothColor
function colorMapColor(stops)
{
  const values = stops.map((s) => s[0]);
  const colors = stops.map((s) => argb(s[1]));
  return (v) => {
    if (v <= values[0])
      return colors[0];
    const last = values.length - 1;
    if (v >= values[last])
      return colors[last];
    let i = 1;
    while (values[i] < v)
      i++;
    // Each byte of the ARGB value separately, the step truncated as in C++
    const p = (v - values[i - 1]) / (values[i] - values[i - 1]);
    const a = colors[i - 1].map((c, k) => k < 3 ? c : Math.round(c * 255));
    const b = colors[i].map((c, k) => k < 3 ? c : Math.round(c * 255));
    const mixed = a.map((c, k) => c + Math.trunc((b[k] - c) * p));
    mixed[3] /= 255;
    return mixed;
  };
}

// hsv_to_rgb() of grid-files ImageFunctions.h, all components 0..255
export function hsvToRgb(hue, saturation, value)
{
  if (saturation === 0)
    return [value, value, value];
  const region = Math.floor(hue / 43);
  const fpart = (hue - region * 43) * 6;
  const p = (value * (255 - saturation)) >> 8;
  const q = (value * (255 - ((saturation * fpart) >> 8))) >> 8;
  const t = (value * (255 - ((saturation * (255 - fpart)) >> 8))) >> 8;
  switch (region)
  {
    case 0: return [value, t, p];
    case 1: return [q, value, p];
    case 2: return [p, value, t];
    case 3: return [p, q, value];
    case 4: return [t, p, value];
    default: return [value, p, q];
  }
}

// hsvValueImage(): the lowest values brightest, 200 steps, quantized by the blur
function hsvColor(stats, hue, saturation, blur)
{
  const b = Math.max(1, blur);
  return (val) => {
    const vv = Math.floor((val - stats.min) / stats.step);
    let v = vv > 200 || vv < 0 ? 0 : 200 - vv;
    v = Math.floor(v / b) * b + 55;
    return [...hsvToRgb(hue, saturation, v), 1];
  };
}

// differenceColor()
export function differenceColor(range)
{
  return (val) => {
    let p = range > 0 ? val / range : 0;
    p = Math.max(-1, Math.min(1, p));
    let end = [0xB2, 0x18, 0x2B];
    if (p < 0)
    {
      end = [0x21, 0x66, 0xAC];
      p = -p;
    }
    return [...end.map((c) => Math.round(255 - (255 - c) * p)), 1];
  };
}

function rgba(c)
{
  return `rgba(${c[0]},${c[1]},${c[2]},${c[3]})`;
}

// Draws the legend: spec = {min, max, color: value -> rgba, units, title, note}
function draw(container, spec)
{
  const width = 320;
  const height = 14;
  const canvas = el('canvas', {width, height, class: 'legend-bar', 'aria-hidden': 'true'});
  const ctx = canvas.getContext('2d');
  for (let x = 0; x < width; x++)
  {
    const v = spec.min + (spec.max - spec.min) * (x + 0.5) / width;
    ctx.fillStyle = rgba(spec.color(v));
    ctx.fillRect(x, 0, 1, height);
  }

  const ticks = niceTicks(spec.min, spec.max, 6);
  const labels = el('div', {class: 'legend-ticks', style: {width: width + 'px'}});
  for (const t of ticks)
  {
    const left = (t - spec.min) / (spec.max - spec.min || 1) * 100;
    labels.append(el('span', {style: {left: left + '%'}}, formatTick(t, ticks.step || 1)));
  }

  replace(container,
      el('div', {class: 'legend-title'}, spec.title, spec.units ? el('span', {class: 'muted'}, ` (${spec.units})`) : null),
      canvas,
      labels,
      spec.note ? el('div', {class: spec.warn ? 'warn small' : 'muted small'}, spec.note) : null);
  container.setAttribute('aria-label',
      `${spec.title}: ${formatValue(spec.min, spec.units)} to ${formatValue(spec.max, spec.units)}`);
}

// The colour maps whose range covers most of the values of the field, best first: maps
// named after the quantity (a word of the parameter description, such as Temperature)
// before the others, and tighter maps before wider ones
function fittingColorMaps(ranges, stats, current, description)
{
  const span = stats.max - stats.min || 1;
  const words = (description || '').toLowerCase().split(/[^a-z]+/).filter((w) => w.length > 3);
  return Object.entries(ranges)
      .filter(([name]) => name !== current)
      .map(([name, [lo, hi]]) => {
        const overlap = Math.max(0, Math.min(hi, stats.max) - Math.max(lo, stats.min)) / span;
        // A map much wider than the data shows it in only a few colours
        const tightness = span / Math.max(span, hi - lo);
        const named = words.some((w) => name.toLowerCase().includes(w)) ? 1 : 0;
        return {name, overlap, tightness, named};
      })
      .filter((m) => m.overlap >= 0.8)
      .sort((a, b) => b.named - a.named || b.overlap - a.overlap || b.tightness - a.tightness)
      .slice(0, 4)
      .map((m) => m.name);
}

// stats (optional): the value range of the field, to tell when it does not fit the map.
// ranges and choose (optional): suggest colour maps that fit, and choose one of them.
export function colorMapLegend(container, colormap, units, stats, ranges, choose, description)
{
  const stops = colormap.stops || [];
  if (stops.length < 2)
  {
    replace(container);
    return;
  }
  const min = stops[0][0];
  const max = stops[stops.length - 1][0];
  let note = 'Values outside the range get the colour of the nearest end.';
  let warn = false;
  if (stats && stats.count > 0)
  {
    const range = `${formatValue(stats.min)} … ${formatValue(stats.max)}`;
    if (stats.max < min || stats.min > max)
    {
      note = `The values of this field (${range}) are all outside the range of the colour map, ` +
          'so every point gets the same end colour. Choose another colour map, or none.';
      warn = true;
    }
    else if (stats.min < min || stats.max > max)
      note = `The values of this field (${range}) reach outside the range of the colour map: those points get the end colours.`;
    else
      note = `The values of this field: ${range}.`;
  }
  // No units: the name of the colour map tells them, and the stored values may be in
  // other units than the parameter definition says (querydata in °C named T-K)
  void units;
  draw(container, {min, max, color: colorMapColor(stops), units: '', title: colormap.name, note, warn});

  if (stats && stats.count > 0 && (stats.min < min || stats.max > max) && ranges && choose)
  {
    const fitting = fittingColorMaps(ranges, stats, colormap.name, description);
    if (fitting.length)
    {
      container.append(el('div', {class: 'buttons small'}, el('span', {class: 'muted'}, 'Colour maps that fit: '),
          fitting.map((name) => {
            const b = el('button', {type: 'button', class: 'small'}, name);
            b.addEventListener('click', () => choose(name));
            return b;
          })));
    }
  }
}

export function hsvLegend(container, stats, options, units)
{
  if (!stats || !stats.count || !(stats.step > 0))
  {
    replace(container, stats && stats.count === 0 ? el('div', {class: 'muted small'}, 'All values are missing') : null);
    return;
  }
  const max = Math.min(stats.max, stats.min + 200 * stats.step);
  draw(container, {
    min: stats.min,
    max,
    color: hsvColor(stats, Number(options.hue), Number(options.saturation), Number(options.blur)),
    units,
    title: 'Value',
    note: `min ${formatValue(stats.min)}, mean ${formatValue(stats.mean)}, max ${formatValue(stats.max)}` +
        (max < stats.max ? ` — values above ${formatValue(max)} are drawn darkest` : ''),
  });
}

export function differenceLegend(container, range, units, labels)
{
  draw(container, {
    min: -range,
    max: range,
    color: differenceColor(range),
    units,
    title: `Difference ${labels}`,
    note: 'Blue: B is smaller, red: B is larger. Larger differences get the end colours.',
  });
}
