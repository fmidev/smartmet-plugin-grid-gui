// A picker of the named colours: each colour is a dot on a hue × lightness plane, as in
// a generic HSV picker, but only the named colours can be chosen. Colours with hardly any
// saturation (greys) are in a strip of their own, and "None" (transparent) is a button.

import {el, replace} from './dom.js';

// "AARRGGBB" -> {h: 0..360, s: 0..1, l: 0..1, a: 0..1, css}
function hsl(argb)
{
  const n = parseInt(argb, 16) >>> 0;
  const a = ((n >>> 24) & 255) / 255;
  const r = ((n >>> 16) & 255) / 255;
  const g = ((n >>> 8) & 255) / 255;
  const b = (n & 255) / 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const l = (max + min) / 2;
  const d = max - min;
  let h = 0;
  let s = 0;
  if (d > 0)
  {
    s = d / (1 - Math.abs(2 * l - 1));
    if (max === r)
      h = 60 * (((g - b) / d) % 6);
    else if (max === g)
      h = 60 * ((b - r) / d + 2);
    else
      h = 60 * ((r - g) / d + 4);
  }
  if (h < 0)
    h += 360;
  return {h, s, l, a, css: `#${argb.slice(2)}`};
}

const GREY_SATURATION = 0.12;

// The size of the plane and of the dots in pixels
const PLANE_WIDTH = 330;
const PLANE_HEIGHT = 240;
const DOT = 13;

// Moves overlapping dots apart a little, so that every colour can be pointed at. The
// dots stay near their true hue and lightness.
function relax(points, width, height, horizontal = true)
{
  const min = DOT + 1;
  for (let iteration = 0; iteration < 60; iteration++)
  {
    let moved = false;
    for (let i = 0; i < points.length; i++)
    {
      for (let j = i + 1; j < points.length; j++)
      {
        const a = points[i];
        const b = points[j];
        let dx = horizontal ? b.x - a.x : 0;
        let dy = b.y - a.y;
        const d = Math.hypot(dx, dy);
        if (d >= min)
          continue;
        if (d < 0.01)
        {
          dx = horizontal ? 0.5 : 0;
          dy = 0.5;
        }
        const push = (min - d) / 2 / Math.max(Math.hypot(dx, dy), 0.01);
        if (horizontal)
        {
          a.x -= dx * push;
          b.x += dx * push;
        }
        a.y -= dy * push;
        b.y += dy * push;
        moved = true;
      }
    }
    for (const p of points)
    {
      p.x = Math.max(DOT / 2, Math.min(width - DOT / 2, p.x));
      p.y = Math.max(DOT / 2, Math.min(height - DOT / 2, p.y));
    }
    if (!moved)
      break;
  }
}

let openPicker = null;

function close()
{
  if (openPicker)
  {
    openPicker.remove();
    openPicker = null;
    document.removeEventListener('mousedown', outside, true);
    document.removeEventListener('keydown', escape, true);
  }
}

function outside(event)
{
  if (openPicker && !openPicker.contains(event.target))
    close();
}

function escape(event)
{
  if (event.key === 'Escape')
  {
    event.stopPropagation();
    close();
  }
}

// The control: a button with the swatch and the name of the colour, opening the picker.
// colors = [{name, argb}], onchange(name)
export function colorPicker(colors, value, onchange, label = 'Colour')
{
  const swatch = el('span', {class: 'swatch'});
  const name = el('span', {class: 'color-name'});
  const button = el('button', {type: 'button', class: 'color-button', 'aria-haspopup': 'dialog'}, swatch, name);

  const show = (v) => {
    const c = colors.find((x) => x.name === v);
    const a = c ? parseInt(c.argb.slice(0, 2), 16) : 0;
    swatch.style.background = c && a > 0 ? `#${c.argb.slice(2)}` : 'transparent';
    swatch.classList.toggle('none', !c || a === 0);
    name.textContent = v;
    button.setAttribute('aria-label', `${label}: ${v}`);
  };
  show(value);

  button.addEventListener('click', () => {
    if (openPicker)
    {
      close();
      return;
    }
    const picker = buildPicker(colors, value, (v) => {
      value = v;
      show(v);
      close();
      button.focus();
      onchange(v);
    });
    document.body.append(picker);
    const r = button.getBoundingClientRect();
    const width = 410;
    picker.style.left = Math.max(8, Math.min(r.left, window.innerWidth - width - 8)) + 'px';
    // Below the button, or above it when there is no room below
    const height = picker.offsetHeight;
    const above = r.bottom + 4 + height > window.innerHeight && r.top - 4 - height >= 0;
    picker.style.top = ((above ? r.top - 4 - height : r.bottom + 4) + window.scrollY) + 'px';
    openPicker = picker;
    document.addEventListener('mousedown', outside, true);
    document.addEventListener('keydown', escape, true);
    const current = picker.querySelector('.dot.selected') || picker.querySelector('input');
    if (current)
      current.focus();
  });

  return el('span', {class: 'color-select'}, button);
}

function buildPicker(colors, value, choose)
{
  const plane = el('div', {class: 'color-plane', role: 'group', 'aria-label': 'Colours by hue and lightness'});
  const greys = el('div', {class: 'color-greys', role: 'group', 'aria-label': 'Greys'});
  const readout = el('div', {class: 'color-readout small'}, value);
  const dots = [];
  const planePoints = [];
  const greyPoints = [];

  let none = null;
  for (const c of colors)
  {
    const v = hsl(c.argb);
    if (v.a === 0)
    {
      none = c.name;
      continue;
    }
    const dot = el('button', {
      type: 'button',
      class: 'dot' + (c.name === value ? ' selected' : ''),
      title: c.name,
      'aria-label': c.name,
      style: {background: v.css},
    });
    dot.addEventListener('click', () => choose(c.name));
    dot.addEventListener('mouseenter', () => {
      readout.textContent = c.name;
    });
    dot.addEventListener('focus', () => {
      readout.textContent = c.name;
    });
    if (v.s < GREY_SATURATION)
    {
      greyPoints.push({dot, x: 20, y: (1 - v.l) * PLANE_HEIGHT});
      greys.append(dot);
    }
    else
    {
      planePoints.push({dot, x: v.h / 360 * PLANE_WIDTH, y: (1 - v.l) * PLANE_HEIGHT});
      plane.append(dot);
    }
    dots.push({dot, name: c.name.toLowerCase()});
  }

  relax(planePoints, PLANE_WIDTH, PLANE_HEIGHT);
  relax(greyPoints, 40, PLANE_HEIGHT);
  for (const p of planePoints.concat(greyPoints))
  {
    p.dot.style.left = `${p.x}px`;
    p.dot.style.top = `${p.y}px`;
  }

  // Filtering by name dims the other dots
  const filter = el('input', {type: 'search', placeholder: 'Filter by name', 'aria-label': 'Filter the colours'});
  filter.addEventListener('input', () => {
    const f = filter.value.trim().toLowerCase();
    for (const d of dots)
      d.dot.classList.toggle('dim', Boolean(f) && !d.name.includes(f));
  });
  filter.addEventListener('keydown', (event) => {
    if (event.key === 'Enter')
    {
      const f = filter.value.trim().toLowerCase();
      const match = dots.find((d) => d.name === f) || dots.find((d) => f && d.name.includes(f));
      if (match)
        match.dot.click();
    }
  });

  const noneButton = none ? el('button', {type: 'button', class: 'small' + (value === none ? ' selected' : '')}, none) : null;
  if (noneButton)
    noneButton.addEventListener('click', () => choose(none));

  return el('div', {class: 'color-picker', role: 'dialog', 'aria-label': 'Choose a colour'},
      el('div', {class: 'color-top'}, filter, noneButton),
      el('div', {class: 'color-area'}, greys, plane),
      readout);
}

// Closes an open picker when the panel it belongs to is drawn again
export function closeColorPicker()
{
  close();
}
