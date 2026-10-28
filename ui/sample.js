// The sample tables: a window of the grid values, or of the coordinates of the grid
// points. Hovering a cell shows the rest: its indexes, coordinates and value. The window
// can be moved over the whole grid.

import {api} from './api.js';
import {el, replace, select} from './dom.js';
import {formatLatLon, formatValue} from './format.js';

// Where each field's window was, so that moving in time keeps it
const windows = new Map();

// options = {file, message, geometry, mode: 'values' | 'coordinates', units, convert(value) to the
// unit shown (optional), readout(text)}
export async function renderSample(container, options)
{
  const key = `${options.geometry}`;
  const win = windows.get(key) || {i0: 0, j0: 0, size: 50};
  windows.set(key, win);

  const box = el('div', {class: 'sample'}, el('div', {class: 'muted'}, 'Loading…'));
  replace(container, box);

  let data;
  try
  {
    data = await api('sample', {file: options.file, message: options.message, i0: win.i0, j0: win.j0, size: win.size},
                     {key: 'sample'});
  }
  catch (error)
  {
    replace(box, el('div', {class: 'error'}, String(error.message || error)));
    return;
  }

  if (options.convert)
    data.values = data.values.map((row) => row.map(options.convert));

  const width = data.values.length ? data.values[0].length : 0;
  const height = data.values.length;
  const haveLatLon = data.lat.length === height;
  const haveXY = data.x.length === width && data.y.length === height;

  // Moving the window
  const move = (di, dj) => {
    win.i0 = Math.max(0, Math.min(Math.max(0, data.columns - win.size), win.i0 + di));
    win.j0 = Math.max(0, Math.min(Math.max(0, data.rows - win.size), win.j0 + dj));
    renderSample(container, options);
  };
  const step = Math.max(1, Math.floor(win.size / 2));
  const button = (label, title, di, dj, disabled) => {
    const b = el('button', {type: 'button', class: 'small', title, disabled}, label);
    b.addEventListener('click', () => move(di, dj));
    return b;
  };
  const numberInput = (value, max, label, onchange) => {
    const input = el('input', {type: 'number', min: 0, max, value, class: 'small-number', 'aria-label': label});
    input.addEventListener('change', () => onchange(Math.max(0, Math.min(max, Number(input.value) || 0))));
    return input;
  };
  const size = select([10, 25, 50, 100].map((n) => ({value: n, label: `${n} × ${n}`})), win.size, (v) => {
    win.size = Number(v);
    renderSample(container, options);
  }, {'aria-label': 'Window size'});

  const nav = el('div', {class: 'sample-nav'},
      button('◀', 'Left', -step, 0, win.i0 === 0),
      button('▶', 'Right', step, 0, win.i0 + width >= data.columns),
      button('▲', 'Up (smaller j)', 0, -step, win.j0 === 0),
      button('▼', 'Down (larger j)', 0, step, win.j0 + height >= data.rows),
      el('label', {class: 'small'}, ' i from ', numberInput(win.i0, data.columns - 1, 'First column', (v) => {
        win.i0 = v;
        renderSample(container, options);
      })),
      el('label', {class: 'small'}, ' j from ', numberInput(win.j0, data.rows - 1, 'First row', (v) => {
        win.j0 = v;
        renderSample(container, options);
      })),
      size,
      el('span', {class: 'muted small'},
          ` columns ${win.i0}–${win.i0 + width - 1} of ${data.columns}, rows ${win.j0}–${win.j0 + height - 1} of ${data.rows}.` +
          ' Rows are in the order of the data.'));

  // The table: indexes in the headers, the grid's own coordinates in their tooltips
  const head = el('tr', {}, el('th', {class: 'corner'}, 'j \\ i'));
  for (let c = 0; c < width; c++)
    head.append(el('th', {title: haveXY ? `x = ${data.x[c]}` : null}, String(win.i0 + c)));

  const body = el('tbody');
  for (let r = 0; r < height; r++)
  {
    const tr = el('tr', {}, el('th', {title: haveXY ? `y = ${data.y[r]}` : null}, String(win.j0 + r)));
    for (let c = 0; c < width; c++)
    {
      let text;
      if (options.mode === 'coordinates')
        text = haveLatLon ? `${data.lat[r][c].toFixed(4)}, ${data.lon[r][c].toFixed(4)}` : '';
      else
      {
        const v = data.values[r][c];
        text = v === null ? '–' : formatValue(v);
      }
      tr.append(el('td', {dataset: {r, c}}, text));
    }
    body.append(tr);
  }
  const table = el('table', {class: 'sample-table' + (options.mode === 'coordinates' ? ' coordinates' : '')},
      el('thead', {}, head), body);

  // The readout of the cell under the pointer, and its row and column highlighted
  let highlighted = [];
  const unhighlight = () => {
    highlighted.forEach((n) => n.classList.remove('hl'));
    highlighted = [];
  };
  body.addEventListener('mouseover', (event) => {
    const td = event.target.closest('td');
    if (!td)
      return;
    const r = Number(td.dataset.r);
    const c = Number(td.dataset.c);
    unhighlight();
    highlighted = [td, head.children[c + 1], body.children[r].children[0]];
    highlighted.forEach((n) => n.classList.add('hl'));

    const parts = [`i ${win.i0 + c}, j ${win.j0 + r}`];
    if (haveLatLon)
      parts.push(formatLatLon(data.lat[r][c], data.lon[r][c]));
    if (haveXY)
      parts.push(`x ${data.x[c]}, y ${data.y[r]}`);
    parts.push(`value ${formatValue(data.values[r][c], options.units)}`);
    options.readout(parts.join('  ·  '));
  });
  table.addEventListener('mouseleave', () => {
    unhighlight();
    options.readout('');
  });

  replace(box, nav, el('div', {class: 'sample-scroll'}, table));
}
