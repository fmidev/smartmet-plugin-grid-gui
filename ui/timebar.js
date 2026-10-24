// The time bar and the level bar. Hovering a cell previews the field at that time or
// level, a click selects it. Days alternate in shade so that the day changes show.

import {el, replace} from './dom.js';
import {formatShortTime, formatTime, levelsGrowDown} from './format.js';

// callbacks: {select(time), preview(time or null)}
export function renderTimebar(container, times, current, callbacks, reference)
{
  if (!times || times.length === 0)
  {
    replace(container);
    return;
  }

  const bar = el('div', {class: 'timebar', role: 'listbox', 'aria-label': 'Times'});
  let day = null;
  let shade = false;
  let labelled = null;
  // Labels for the days, or for the months when there are too many days
  const days = new Set(times.map((t) => t.slice(0, 8))).size;
  const labelLength = days > 24 ? 6 : 8;
  const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  times.forEach((time) => {
    const d = time.slice(0, 8);
    if (d !== day)
    {
      shade = !shade;
      day = d;
    }
    const cell = el('div', {
      class: 'tcell' + (shade ? ' shade' : '') + (time === current ? ' current' : ''),
      role: 'option',
      'aria-selected': time === current ? 'true' : 'false',
      title: formatTime(time) + ' UTC',
    });
    cell.addEventListener('mouseenter', () => callbacks.preview(time));
    cell.addEventListener('click', () => callbacks.select(time));
    const period = time.slice(0, labelLength);
    if (labelled !== period)
    {
      const text = labelLength === 8 ? formatShortTime(time).split(' ').slice(0, 2).join(' ')
                                     : `${months[Number(time.slice(4, 6)) - 1]} ${time.slice(0, 4)}`;
      cell.append(el('span', {class: 'tlabel'}, text));
      labelled = period;
    }
    bar.append(cell);
  });
  bar.addEventListener('mouseleave', () => callbacks.preview(null));

  // Too many days or months for labels: they would overlap
  if ((labelLength === 8 ? days : new Set(times.map((t) => t.slice(0, 6))).size) > 24)
    bar.classList.add('dense');

  replace(container, bar);
  void reference;
}

// The levels of the selected field: [{level, file, message}] from the top of the
// atmosphere down when the levels are pressures (largest last)
export function renderLevelbar(container, levels, current, callbacks, levelType)
{
  if (!levels || levels.length < 2)
  {
    replace(container);
    return;
  }

  // Pressure and model levels: the largest values at the bottom. Heights: at the top.
  const ordered = levels.slice();
  if (!levelsGrowDown(levelType))
    ordered.reverse();

  const bar = el('div', {class: 'levelbar', role: 'listbox', 'aria-label': 'Levels'});
  for (const l of ordered)
  {
    const cell = el('div', {
      class: 'lcell' + (l.level === current ? ' current' : ''),
      role: 'option',
      'aria-selected': l.level === current ? 'true' : 'false',
      title: `Level ${l.level}${levelType && levelType.units ? ' ' + levelType.units : ''}`,
    }, String(l.level));
    cell.addEventListener('mouseenter', () => callbacks.preview(l));
    cell.addEventListener('click', () => callbacks.select(l.level));
    bar.append(cell);
  }
  bar.addEventListener('mouseleave', () => callbacks.preview(null));
  replace(container, el('div', {class: 'levelbar-title small muted'}, 'Level'), bar);
}
