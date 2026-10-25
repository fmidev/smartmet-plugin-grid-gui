// The parameter key of the selected field, shown as it is, with the meaning of each
// part in a tooltip and in a table that opens below it.
//
//   T-K:MEPS:1093:2:850:1:-1
//   name:producer:geometry:levelId:level:forecastType[:forecastNumber]
//
// A query may give more parts: ...:generationFlags:areaInterp:timeInterp:levelInterp.
// The interpolation methods shown are those the query of the key uses: the methods of
// the parameter mapping, or the defaults of the parameter.

import {copyText, el, replace} from './dom.js';
import {formatLatLon, formatTime} from './format.js';

function describe(name, description)
{
  return [name, description].filter((s) => s).join(' — ');
}

// The parts of the key: [{value, label, text}]
// The defined types as text: "1 = Deterministic forecast, 2 = Analysis, ..."
function definedList(defined)
{
  return (defined || []).map((d) => `${d.id} = ${d.name || d.description}`).join(', ');
}

export function keyParts(info, meta = {})
{
  const p = info.parameter || {};
  const g = info.geometry || {};
  const lt = info.levelType || {};
  const ft = info.forecastType || {};
  const parts = [];

  const aliases = (info.aliases || []).length ? ` Queries may also call it ${info.aliases.join(', ')}.` : '';
  parts.push({
    value: p.name,
    label: 'Parameter',
    text: `${describe(p.name, p.description)}${p.units ? `, unit ${p.units}` : ''}` +
        `${p.fmiId ? ` (FMI parameter id ${p.fmiId})` : ''}.${aliases}`,
  });

  const producer = info.producer || {};
  const producerText = producer.description || (producer.title !== producer.name ? producer.title : '');
  parts.push({
    value: producer.name,
    label: 'Producer',
    text: describe(producer.name, producerText) || 'The producer of the data.',
  });

  let geometryText = `Geometry ${g.id}${g.name ? ` (${g.name})` : ''}`;
  if (g.columns)
    geometryText += `: a grid of ${g.columns} × ${g.rows} points`;
  if (g.cellWidth)
    geometryText += `, cells about ${g.cellWidth.toFixed(1)} × ${g.cellHeight.toFixed(1)} km`;
  if (g.corners)
  {
    const c = g.corners;
    geometryText += `, from ${formatLatLon(c.bottomLeft[1], c.bottomLeft[0])} (bottom left)` +
        ` to ${formatLatLon(c.topRight[1], c.topRight[0])} (top right)`;
  }
  if (g.bbox)
    geometryText += `; the grid points lie within latitudes ${g.bbox[1].toFixed(2)}…${g.bbox[3].toFixed(2)} and longitudes ${g.bbox[0].toFixed(2)}…${g.bbox[2].toFixed(2)}`;
  if (info.searchConfigured === false)
    geometryText += '. Queries without a geometry do not find it: the producer file does not list it';
  parts.push({value: String(g.id), label: 'Geometry', text: geometryText + '.'});

  parts.push({
    value: String(lt.id),
    label: 'Level type',
    text: lt.name ? `Level type ${lt.id}: ${describe(lt.name, lt.description)}` + (lt.units ? `, levels in ${lt.units}` : '') + '.'
                  : `Level type ${lt.id} has no definition in the grid configuration. The defined ones are ${definedList(meta.levelTypes)}.`,
  });

  parts.push({
    value: String(info.level),
    label: 'Level',
    text: `Level ${info.level}${lt.units ? ' ' + lt.units : ''}` +
        (lt.name ? ` of level type ${lt.name}` : '') + '.',
  });

  // The key has the forecast type when it is not negative, and the number when that
  // is not negative either
  if (ft.id !== undefined && ft.id >= 0)
  {
    parts.push({
      value: String(ft.id),
      label: 'Forecast type',
      text: ft.name ? `Forecast type ${ft.id}: ${describe(ft.name, ft.description)}.`
                    : `Forecast type ${ft.id} has no definition in the grid configuration. The defined ones are ${definedList(meta.forecastTypes)}.`,
    });
    if (info.forecastNumber >= 0)
    {
      parts.push({
        value: String(info.forecastNumber),
        label: 'Forecast number',
        text: info.forecastNumber === 0 ? 'Forecast number 0: the control member of an ensemble.'
                                        : `Forecast number ${info.forecastNumber}: ensemble member ${info.forecastNumber}.`,
      });
    }
  }

  // The values exactly as in the key: getFmiKey() writes a zero geometry or level type
  // as an empty part
  const raw = (info.key || '').split(':');
  parts.forEach((part, i) => {
    if (i >= raw.length)
      return;
    if (raw[i] === '' && part.value !== '')
      part.text = `Empty (${part.value}). ${part.text}`;
    part.value = raw[i];
  });
  return parts;
}

// The extra parts a query may give, with the methods the key uses
export function queryParts(info)
{
  const i = info.interpolation;
  if (!i)
    return [];
  const source = i.source === 'mapping' ? 'the parameter mapping of the producer'
                                         : 'the defaults of the parameter';
  return [
    {
      value: '',
      label: 'Generation flags',
      text: 'Empty: the newest ready generation. Flags select an older generation.',
    },
    {
      value: String(i.area),
      label: 'Area interpolation',
      text: `${i.area} = ${i.areaName}: how values between grid points are computed (from ${source}).`,
    },
    {
      value: String(i.time),
      label: 'Time interpolation',
      text: `${i.time} = ${i.timeName}: how values between the forecast times are computed (from ${source}).`,
    },
    {
      value: String(i.level),
      label: 'Level interpolation',
      text: `${i.level} = ${i.levelName}: how values between levels are computed (from ${source}).`,
    },
  ];
}

// The key with all parts: the forecast number part is empty when the key has none
export function fullKey(info)
{
  const parts = keyParts(info).map((p) => p.value);
  while (parts.length < 7)
    parts.push('');
  return parts.concat(queryParts(info).map((p) => p.value)).join(':');
}

// A tooltip shared by all key parts
let tooltip = null;

function showTooltip(target, title, text)
{
  if (!tooltip)
  {
    tooltip = el('div', {class: 'tooltip', role: 'tooltip', id: 'key-tooltip'});
    document.body.append(tooltip);
  }
  replace(tooltip, el('strong', {}, title), el('div', {}, text));
  tooltip.hidden = false;
  const r = target.getBoundingClientRect();
  const width = Math.min(360, window.innerWidth - 16);
  tooltip.style.maxWidth = width + 'px';
  const left = Math.max(8, Math.min(r.left, window.innerWidth - width - 8));
  tooltip.style.left = left + 'px';
  tooltip.style.top = (r.bottom + 6 + window.scrollY) + 'px';
}

function hideTooltip()
{
  if (tooltip)
    tooltip.hidden = true;
}

function chips(parts)
{
  const line = el('div', {class: 'key-line'});
  parts.forEach((part, index) => {
    if (index > 0)
      line.append(el('span', {class: 'key-sep'}, ':'));
    const chip = el('button', {
      type: 'button',
      class: 'key-part' + (part.value === '' ? ' blank' : ''),
      'aria-describedby': 'key-tooltip',
      'aria-label': `${part.label}: ${part.value || 'empty'}`,
    }, part.value === '' ? '·' : part.value);
    const show = () => showTooltip(chip, part.label, part.text);
    chip.addEventListener('mouseenter', show);
    chip.addEventListener('focus', show);
    chip.addEventListener('mouseleave', hideTooltip);
    chip.addEventListener('blur', hideTooltip);
    line.append(chip);
  });
  return line;
}

// The key panel. links: {timeseries: url or null, explorer: url or null}
export function renderKey(container, info, links = {})
{
  if (!info)
  {
    replace(container);
    return;
  }

  const parts = keyParts(info, links.meta || {});
  const extra = queryParts(info);

  const table = el('dl', {class: 'key-list'},
      parts.concat(extra).map((p) => [
        el('dt', {}, p.label, ' ', el('code', {}, p.value === '' ? '(empty)' : p.value)),
        el('dd', {}, p.text),
      ]));

  const copyKey = el('button', {type: 'button', class: 'small'}, 'Copy key');
  copyKey.addEventListener('click', () => copyText(info.key, copyKey));
  const copyFull = el('button', {type: 'button', class: 'small', title: 'The key with the generation flags and the interpolation methods'}, 'Copy full form');
  copyFull.addEventListener('click', () => copyText(fullKey(info), copyFull));

  const buttons = el('div', {class: 'buttons'}, copyKey, extra.length ? copyFull : null);
  if (links.timeseries)
  {
    const copyTs = el('button', {type: 'button', class: 'small', title: links.timeseries}, 'Copy TimeSeries URL');
    copyTs.addEventListener('click', () => copyText(links.timeseries, copyTs));
    buttons.append(copyTs);
  }
  if (links.explorer)
    buttons.append(el('a', {class: 'button small', href: links.explorer, target: '_blank', rel: 'noopener',
                            title: 'The time series of the field at the pinned point (or the centre of the grid) in the data explorer of the server'},
                      'Open in data explorer'));

  const notes = [];
  if (info.aggregation)
  {
    const a = info.aggregation;
    const period = a.periodMinutes ? ` over ${a.periodMinutes % 60 === 0 ? a.periodMinutes / 60 + ' h' : a.periodMinutes + ' min'}` : '';
    notes.push(`Aggregation: ${a.description || a.name || 'id ' + a.id}${period}`);
  }
  if (info.processing)
    notes.push(`Processing: ${info.processing.description || info.processing.name || 'id ' + info.processing.id}`);
  if (info.generation)
    notes.push(`Analysis time ${formatTime(info.generation.analysisTime)} UTC`);

  replace(container,
      chips(parts),
      extra.length ? el('div', {class: 'key-extra', title: 'Parts a query may add to the key'},
          el('span', {class: 'muted'}, 'query adds '), chips(extra)) : null,
      notes.length ? el('div', {class: 'muted small'}, notes.join(' · ')) : null,
      buttons,
      el('details', {class: 'key-details'}, el('summary', {}, 'What do the parts mean?'), table));
}
