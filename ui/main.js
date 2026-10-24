// grid-gui: browse every field the grid engine knows.
//
// The state of the page is in its address (readable parameters, see readUrl()), so a
// view can be bookmarked and shared, and Back returns to the previous selection. The
// data comes from the JSON API (api.js), the images are drawn by the server.

import {api, ApiError, isAbort, renderUrl} from './api.js';
import {lineChart} from './charts.js';
import {debounce, el, fillSelect, replace, select, store} from './dom.js';
import {formatAge, formatLatLon, formatLeadTime, formatShortTime, formatTime, formatValue,
        levelsGrowDown, parseTime, timeTicks} from './format.js';
import {renderInfo} from './info.js';
import {renderKey} from './keyexplain.js';
import {colorMapLegend, differenceLegend, hsvLegend} from './legend.js';
import {fieldContent, levelContents, matchingContent, resolve} from './selection.js';
import {renderSample} from './sample.js';
import {renderSettings, viewOptions} from './settings.js';
import {renderLevelbar, renderTimebar} from './timebar.js';

// ### State

const views = [
  {value: 'image', label: 'Image', image: true},
  {value: 'map', label: 'Map', image: true},
  {value: 'streams', label: 'Streams', image: true},
  {value: 'streamsAnimation', label: 'Animated streams', image: true},
  {value: 'info', label: 'Info'},
  {value: 'table', label: 'Values (sample)', sample: 'values'},
  {value: 'coordinates', label: 'Coordinates (sample)', sample: 'coordinates'},
  {value: 'message', label: 'Message bytes', frame: true},
];
const viewInfo = (v) => views.find((x) => x.value === v) || views[0];

// Views drawn in the projection geometry
const projected = new Set(['image', 'streams', 'streamsAnimation']);

// Parameters to show first when the producer has them and none has been chosen
const preferredParameters = ['T-K', 'T-C', 'TSEA-K', 'P-PA', 'PMSL-PA', 'FF-MS', 'RH-PRCNT'];

const state = {
  producer: '',        // producer name
  generation: '',      // analysis time, '' = the newest ready one
  parameter: '',
  sel: {},             // levelId, level, forecastType, forecastNumber, geometry, time
  view: 'image',
  projection: '',      // geometry id, '' = the field's own geometry
  lock: false,         // keep the projection when the geometry changes
  compare: '',         // analysis time of generation B, '' = no comparison
  mapArea: 'data',     // the Map view: 'data' (the area of the data) or 'world'

  point: null,         // {lat, lon}
  options: {},         // rendering options that differ from the defaults
};

const data = {
  meta: null,
  producers: [],
  generations: [],
  generation: null,    // the selected generation record
  parameters: [],
  contents: null,
  resolved: null,      // {choices, sel, field}
  content: null,       // {file, message, time}
  key: null,
  compareContents: null,
  compareContent: null,
  compareGeneration: null,
};

const $ = (id) => document.getElementById(id);

// ### The address of the page

const urlNames = {
  producer: 'producer', generation: 'generation', parameter: 'parameter',
  levelId: 'levelType', level: 'level', forecastType: 'forecastType',
  forecastNumber: 'member', geometry: 'geometry', time: 'time',
};

function readUrl()
{
  const q = new URLSearchParams(location.search);
  state.producer = q.get('producer') || '';
  state.generation = q.get('generation') || '';
  state.parameter = q.get('parameter') || '';
  state.sel = {};
  for (const name of ['levelId', 'level', 'forecastType', 'forecastNumber', 'geometry', 'time'])
  {
    const v = q.get(urlNames[name]);
    if (v !== null && v !== '')
      state.sel[name] = name === 'time' ? v : Number(v);
  }
  state.view = viewInfo(q.get('view') || 'image').value;
  state.projection = q.get('projection') || '';
  state.lock = q.get('lock') === '1';
  state.compare = q.get('compare') || '';
  state.mapArea = q.get('mapArea') === 'world' ? 'world' : 'data';
  const lat = parseFloat(q.get('lat'));
  const lon = parseFloat(q.get('lon'));
  state.point = Number.isFinite(lat) && Number.isFinite(lon) ? {lat, lon} : null;

  // Rendering options: the remembered ones, overridden by the address
  state.options = Object.assign({}, store.get('options', {}));
  for (const name of Object.keys(data.meta.defaults))
  {
    const v = q.get(name);
    if (v !== null)
      state.options[name] = v;
  }
}

// A change that should get its own history entry sets this before the address is written
let pushNext = false;

function writeUrl(push = false)
{
  push = push || pushNext;
  pushNext = false;
  const q = new URLSearchParams();
  q.set('producer', state.producer);
  if (data.generation)
    q.set('generation', data.generation.analysisTime);
  if (state.parameter)
    q.set('parameter', state.parameter);
  const sel = data.resolved ? data.resolved.sel : state.sel;
  for (const name of ['levelId', 'level', 'forecastType', 'forecastNumber', 'geometry', 'time'])
  {
    if (sel[name] !== undefined && sel[name] !== null)
      q.set(urlNames[name], sel[name]);
  }
  if (state.view !== 'image')
    q.set('view', state.view);
  if (state.projection)
    q.set('projection', state.projection);
  if (state.lock)
    q.set('lock', '1');
  if (state.compare)
    q.set('compare', state.compare);
  if (state.mapArea !== 'data')
    q.set('mapArea', state.mapArea);
  if (state.point)
  {
    q.set('lat', state.point.lat.toFixed(4));
    q.set('lon', state.point.lon.toFixed(4));
  }
  for (const [name, value] of Object.entries(currentOptions()))
  {
    if (String(value) !== String(data.meta.defaults[name]))
      q.set(name, value);
  }
  const url = '?' + q.toString();
  if (url !== location.search)
    history[push ? 'pushState' : 'replaceState'](null, '', url);
}

// The rendering options with the defaults filled in
function currentOptions()
{
  return Object.assign({}, data.meta.defaults, state.options);
}

// ### Status messages

let statusTimer = null;
function status(text, kind = 'info')
{
  const node = $('status');
  node.textContent = text || '';
  node.className = 'status ' + kind;
  clearTimeout(statusTimer);
  if (text && kind !== 'error')
    statusTimer = setTimeout(() => {
      node.textContent = '';
    }, 4000);
}

function report(error)
{
  if (isAbort(error))
    return;
  console.error(error);
  status(error instanceof ApiError ? error.message : String(error), 'error');
}

// ### Loading the selection, each step from the previous one

async function start()
{
  try
  {
    [data.meta, data.producers] = await Promise.all([api('meta'), api('producers')]);
    readUrl();
    await loadProducer();
  }
  catch (error)
  {
    report(error);
  }
}

async function loadProducer()
{
  const producer = data.producers.find((p) => p.name === state.producer) ||
      data.producers.find((p) => p.name === store.get('producer')) || data.producers[0];
  if (!producer)
  {
    status('The grid engine has no producers', 'error');
    return;
  }
  state.producer = producer.name;
  store.set('producer', producer.name);
  data.generations = await api('generations', {producer: producer.id}, {key: 'generations'});
  await loadGeneration();
}

async function loadGeneration()
{
  const g = data.generations;
  data.generation = g.find((x) => x.analysisTime === state.generation) || g.find((x) => x.ready) || g[0] || null;
  state.generation = data.generation ? data.generation.analysisTime : '';
  if (!data.generation)
  {
    data.parameters = [];
    data.contents = null;
    renderAll();
    return;
  }
  data.parameters = await api('parameters', {generation: data.generation.id}, {key: 'parameters', maxAge: 5000});
  await loadParameter();
}

async function loadParameter()
{
  const names = data.parameters.map((p) => p.name);
  const remembered = store.get('parameter.' + state.producer);
  state.parameter = [state.parameter, remembered, ...preferredParameters].find((n) => n && names.includes(n)) || names[0] || '';
  if (!state.parameter)
  {
    data.contents = null;
    renderAll();
    return;
  }
  store.set('parameter.' + state.producer, state.parameter);
  data.contents = await api('contents', {generation: data.generation.id, parameter: state.parameter},
                            {key: 'contents', maxAge: 5000});
  await loadCompare();
  applySelection();
}

// The contents of generation B in compare mode
async function loadCompare()
{
  data.compareGeneration = null;
  data.compareContents = null;
  if (!state.compare || !data.generation)
    return;
  const b = data.generations.find((x) => x.analysisTime === state.compare && x.id !== data.generation.id);
  if (!b)
    return;
  data.compareGeneration = b;
  data.compareContents = await api('contents', {generation: b.id, parameter: state.parameter},
                                   {key: 'compare', maxAge: 5000});
}

// Resolves the selection from the contents, keeping as much of the old one as exists
function applySelection()
{
  const previousGeometry = data.resolved ? data.resolved.sel.geometry : null;
  data.resolved = data.contents ? resolve(data.contents.fields, state.sel) : null;
  if (data.resolved)
  {
    state.sel = Object.assign({}, data.resolved.sel);
    if (!state.lock && previousGeometry !== null && previousGeometry !== state.sel.geometry)
      state.projection = '';
  }
  updateContent();
}

// The file and message of the selected time, the key and the compare field. Only the
// time changed: the selection panel stays as it is.
function updateContent(timeOnly = false)
{
  const r = data.resolved;
  data.content = r ? fieldContent(r.field, r.sel.time) : null;
  data.compareContent = null;
  if (r && data.compareContents)
    data.compareContent = matchingContent(data.compareContents.fields, r.sel).content;
  writeUrl();
  if (timeOnly)
    renderTimeDependent();
  else
    renderAll();
  loadKey();
}

const loadKey = debounce(async () => {
  if (!data.content)
  {
    data.key = null;
    renderKeyPanel();
    return;
  }
  try
  {
    data.key = await api('key', {file: data.content.file, message: data.content.message},
                         {key: 'key', maxAge: 60000});
    renderKeyPanel();
  }
  catch (error)
  {
    report(error);
  }
}, 150);

// ### The data panel

function generationLabel(g)
{
  const flags = [];
  if (!g.ready)
    flags.push('not ready');
  return `${formatTime(g.analysisTime)} UTC` + (flags.length ? ` (${flags.join(', ')})` : '');
}

function producerOption(p)
{
  const age = p.newestReady ? formatAge(p.newestReady) : 'no ready generation';
  return {value: p.name, label: `${p.name} · ${age}`,
          title: [p.title !== p.name ? p.title : '', p.description,
                  `${p.generations} generation(s), newest ready ${p.newestReady ? formatTime(p.newestReady) : 'none'}`]
              .filter((s) => s).join('\n')};
}

function describeLevelType(id)
{
  const lt = data.contents.levelTypes[id] || {};
  return `${id}: ${lt.description || lt.name || 'not defined'}`;
}

function describeForecastType(id)
{
  const ft = data.contents.forecastTypes[id] || {};
  return `${id}: ${ft.description || ft.name || 'not defined'}`;
}

// Tooltips of the type selections: the meaning of the selected one, and the defined
// types when it has no definition
function typeHelp(kind, id, record, defined)
{
  if (record && (record.name || record.description))
  {
    const text = [record.name, record.description].filter((x, i, a) => x && a.indexOf(x) === i).join(' — ');
    return `${kind} ${id}: ${text}${record.units ? ` (levels in ${record.units})` : ''}`;
  }
  const list = (defined || []).map((d) => `${d.id} = ${d.name || d.description}`).join(', ');
  return `${kind} ${id} has no definition in the grid configuration` +
      (list ? `. The defined ones are ${list}.` : '.') +
      ' The data itself uses this value, so it is shown as it is.';
}

function describeMember(n)
{
  if (n === -1)
    return '−1: none (deterministic)';
  if (n === 0)
    return '0: control';
  return `${n}: member ${n}`;
}

function describeGeometry(id)
{
  const g = data.contents.geometries[id] || {};
  const size = g.columns ? ` (${g.columns} × ${g.rows})` : '';
  return `${id}: ${g.name || 'geometry'}${size}${g.searchConfigured === false ? ' — not in producer file' : ''}`;
}

function field(label, help, control, note)
{
  return el('div', {class: 'field'}, el('label', {title: help}, label, control), note || null);
}

// The parameter list with a filter: the filter matches names, descriptions, units and
// the other names of the parameters
let parameterFilter = '';

function parameterPicker()
{
  const filter = el('input', {type: 'search', placeholder: `Filter ${data.parameters.length} parameters`,
                              'aria-label': 'Filter the parameters', value: parameterFilter});
  const list = el('select', {size: 8, class: 'parameter-list', 'aria-label': 'Parameter'});
  const label = (p) => `${p.name} — ${p.description || ''}${p.units ? ` [${p.units}]` : ''}`;
  const fill = () => {
    parameterFilter = filter.value;
    const f = filter.value.trim().toLowerCase();
    const matches = data.parameters.filter((p) => !f ||
        [p.name, p.description, p.units, ...(p.aliases || [])].some((s) => s && s.toLowerCase().includes(f)));
    fillSelect(list, matches.map((p) => ({
      value: p.name,
      label: label(p),
      title: (p.aliases || []).length ? `Also known as ${p.aliases.join(', ')}` : '',
    })), state.parameter);
  };
  filter.addEventListener('input', fill);
  filter.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && list.options.length)
      chooseParameter(list.options[0].value);
  });
  list.addEventListener('change', () => chooseParameter(list.value));
  fill();
  // Keep the selected parameter visible, scrolling only the list
  requestAnimationFrame(() => {
    const o = list.selectedOptions[0];
    if (o)
      list.scrollTop = Math.max(0, o.offsetTop - list.clientHeight / 2);
  });
  return el('div', {class: 'parameter-picker'}, filter, list);
}

async function chooseParameter(name)
{
  if (name === state.parameter)
    return;
  state.parameter = name;
  run(loadParameter, true);
}

// Runs a loading step and reports its errors
async function run(step, push = false)
{
  try
  {
    status('Loading…');
    pushNext = push;
    await step();
    status('');
  }
  catch (error)
  {
    pushNext = false;
    report(error);
  }
}

function renderDataPanel()
{
  const controls = [];

  controls.push(field('Producer', 'The producers of the content server, with the age of their newest ready generation',
      select(data.producers.map(producerOption), state.producer, (v) => {
        state.producer = v;
        state.generation = '';
        state.compare = '';
        run(loadProducer, true);
      })));

  if (data.generations.length)
  {
    const g = data.generation;
    const note = g ? el('div', {class: 'note small'},
        g.ready ? null : el('span', {class: 'warn'}, 'Not ready: the generation is still being loaded. '),
        g.start ? `Times ${formatShortTime(g.start)} – ${formatShortTime(g.end)}. ` : '',
        `Analysis ${formatAge(g.analysisTime)}.`) : null;
    controls.push(field('Generation', 'The model run: its analysis time',
        select(data.generations.map((x) => ({value: x.analysisTime, label: generationLabel(x)})), state.generation, (v) => {
          state.generation = v;
          if (state.compare === v)
            state.compare = '';
          run(loadGeneration, true);
        }), note));
  }
  else
    controls.push(el('div', {class: 'muted'}, 'The producer has no generations.'));

  if (data.parameters.length)
    controls.push(field('Parameter', 'FMI parameter name, description and unit', parameterPicker()));

  const r = data.resolved;
  if (r)
  {
    const c = r.choices;
    const s = r.sel;
    const choose = (name) => (v) => {
      state.sel[name] = Number(v);
      pushNext = true;
      applySelection();
    };
    controls.push(el('div', {class: 'field-row'},
        field('Level type', typeHelp('Level type', s.levelId, data.contents.levelTypes[s.levelId], data.meta.levelTypes),
            select(c.levelIds.map((v) => ({value: v, label: describeLevelType(v),
                                          title: typeHelp('Level type', v, data.contents.levelTypes[v], data.meta.levelTypes)})),
                   s.levelId, choose('levelId'), {disabled: c.levelIds.length < 2,
                                                  title: typeHelp('Level type', s.levelId, data.contents.levelTypes[s.levelId], data.meta.levelTypes)})),
        field('Level', 'The vertical level', select(c.levels.map((v) => ({value: v, label: String(v)})), s.level, choose('level'), {disabled: c.levels.length < 2}))));
    controls.push(el('div', {class: 'field-row'},
        field('Forecast type', typeHelp('Forecast type', s.forecastType, data.contents.forecastTypes[s.forecastType], data.meta.forecastTypes),
            select(c.forecastTypes.map((v) => ({value: v, label: describeForecastType(v),
                                               title: typeHelp('Forecast type', v, data.contents.forecastTypes[v], data.meta.forecastTypes)})),
                   s.forecastType, choose('forecastType'), {disabled: c.forecastTypes.length < 2,
                                                           title: typeHelp('Forecast type', s.forecastType, data.contents.forecastTypes[s.forecastType], data.meta.forecastTypes)})),
        field('Member', 'The ensemble member (forecast number). −1: the field is not an ensemble member. 0: the control forecast of an ensemble.',
            select(c.forecastNumbers.map((v) => ({value: v, label: describeMember(v)})), s.forecastNumber, choose('forecastNumber'),
                   {disabled: c.forecastNumbers.length < 2, title: describeMember(s.forecastNumber)}))));
    const geometry = data.contents.geometries[s.geometry] || {};
    controls.push(field('Geometry', 'The grid of the field', select(c.geometries.map((v) => ({value: v, label: describeGeometry(v)})), s.geometry, choose('geometry'), {disabled: c.geometries.length < 2}),
        geometry.searchConfigured === false
            ? el('div', {class: 'note small warn'}, 'Not in the producer file: queries without an explicit geometry do not find this grid.')
            : null));
  }
  else if (state.parameter)
    controls.push(el('div', {class: 'muted'}, 'No fields.'));

  replace($('data-controls'), controls);
}

// ### The key panel

function timeseriesUrl()
{
  if (!data.key)
    return null;
  let lat;
  let lon;
  if (state.point)
    ({lat, lon} = state.point);
  else if (data.key.geometry && data.key.geometry.corners)
  {
    const c = data.key.geometry.corners;
    lat = (c.bottomLeft[1] + c.topRight[1]) / 2;
    lon = (c.bottomLeft[0] + c.topRight[0]) / 2;
  }
  else
    return null;
  const base = location.origin + '/timeseries';
  return `${base}?producer=${encodeURIComponent(data.key.producer.name)}` +
      `&param=time,${encodeURIComponent(data.key.key)}&latlon=${lat.toFixed(4)},${lon.toFixed(4)}` +
      `&origintime=${data.key.generation.analysisTime}&format=json`;
}

function renderKeyPanel()
{
  renderKey($('key'), data.key, {timeseries: timeseriesUrl(), meta: data.meta});
}

// ### The view panel

function renderViewPanel()
{
  const tabs = el('div', {class: 'tabs', role: 'tablist', 'aria-label': 'View'});
  for (const v of views)
  {
    if (v.value === 'streamsAnimation' && !data.meta.animationEnabled)
      continue;
    const tab = el('button', {type: 'button', role: 'tab', class: 'tab',
                              'aria-selected': state.view === v.value ? 'true' : 'false'}, v.label);
    tab.addEventListener('click', () => {
      state.view = v.value;
      writeUrl(true);
      renderAll();
    });
    tabs.append(tab);
  }

  const parts = [tabs];

  if (projected.has(state.view) && data.resolved)
  {
    const own = data.resolved.sel.geometry;
    const projections = [{value: '', label: `Own geometry (${own})`}].concat(
        data.meta.projections.filter((p) => !p.blocked || String(p.id) === state.projection).map((p) => ({
          value: String(p.id),
          label: `${p.id}: ${p.name || ''}${p.columns ? ` (${p.columns} × ${p.rows})` : ''}`,
        })));
    const lock = el('input', {type: 'checkbox', checked: state.lock});
    lock.addEventListener('change', () => {
      state.lock = lock.checked;
      writeUrl();
    });
    parts.push(field('Projection', 'Draw the field in another grid geometry (interpolated)',
        select(projections, state.projection, (v) => {
          state.projection = v;
          writeUrl(true);
          renderAll();
        }, {class: 'wide'})));
    parts.push(el('label', {class: 'check', title: 'Keep the projection when the geometry changes'}, lock, ' Keep projection'));
  }

  if (state.view === 'map')
  {
    parts.push(field('Map area', 'The area of the data, or the whole world',
        select([{value: 'data', label: 'Area of the data'}, {value: 'world', label: 'Whole world'}], state.mapArea, (v) => {
          state.mapArea = v;
          writeUrl(true);
          renderAll();
        }, {class: 'wide'})));
  }

  // Compare with another generation of the producer: the same field in both
  if (data.generations.length > 1 && viewInfo(state.view).image)
  {
    const others = data.generations.filter((g) => !data.generation || g.id !== data.generation.id);
    parts.push(field('Compare with', 'Show the same field of another generation and the difference B − A',
        select([{value: '', label: 'Off'}].concat(others.map((g) => ({value: g.analysisTime, label: generationLabel(g)}))),
            state.compare, (v) => {
              state.compare = v;
              run(async () => {
                await loadCompare();
                updateContent();
              }, true);
            }, {class: 'wide'})));
  }

  if (data.content)
  {
    parts.push(el('div', {class: 'buttons'},
        el('a', {class: 'button small', href: renderUrl('download', {file: data.content.file, message: data.content.message})}, 'Download message'),
        el('a', {class: 'button small', href: legacyUrl(), title: 'The same field in the legacy interface'}, 'Open in legacy UI')));
  }

  replace($('view-controls'), parts);
}

// The legacy interface with the same field selected
function legacyUrl()
{
  if (!data.content || !data.resolved)
    return '../grid-gui';
  const s = data.resolved.sel;
  const producer = data.producers.find((p) => p.name === state.producer);
  const attrs = {
    pg: 'main', pi: producer ? producer.id : '', pn: state.producer, g: data.generation.id, p: state.parameter,
    lt: s.levelId, l: s.level, ft: s.forecastType, fn: s.forecastNumber, gm: s.geometry, t: s.time,
    f: data.content.file, m: data.content.message, pre: 'Image', pro: state.projection || s.geometry,
    tgt: 'Month', tg: s.time.slice(0, 6),
  };
  // The rendering options with their legacy names
  const o = currentOptions();
  Object.assign(attrs, {
    cm: o.colormap, op: o.opacity, hu: o.hue, sa: o.saturation, bl: o.blur, cl: o.coordinateLines,
    lb: o.landBorder, lm: o.landColor, lcp: o.landColorPosition, sm: o.seaColor, scp: o.seaColorPosition,
    lsl: o.landShadingLight, lss: o.landShadingShadow, lsp: o.landShadingPosition,
    ssl: o.seaShadingLight, sss: o.seaShadingShadow, ssp: o.seaShadingPosition, mi: o.missing,
    st: o.step, min: o.minLength, max: o.maxLength, sc: o.streamColor, k: '', u: '', xx: '', yy: '', pl: '',
  });
  const session = Object.entries(attrs).map(([k, v]) => `${k}=${v}`).join(';') + ';';
  return '../grid-gui?session=' + encodeURIComponent(session);
}

// ### The rendering panel

function setOption(name, value)
{
  if (name === null)
    state.options = {};
  else
    state.options[name] = value;
  store.set('options', state.options);
  writeUrl();
  renderSettingsPanel();
  renderViewer();
}

function renderSettingsPanel()
{
  const view = compareActive() ? 'image' : state.view;
  renderSettings($('settings'), data.meta, currentOptions(), view, setOption);
}

// ### Times

function times()
{
  return data.resolved && data.resolved.field ? data.resolved.field.times : [];
}

function selectTime(time, push = false)
{
  if (!time || !data.resolved)
    return;
  state.sel.time = time;
  data.resolved.sel.time = time;
  pushNext = push;
  updateContent(true);
}

function stepTime(step)
{
  const t = times();
  const i = t.indexOf(state.sel.time);
  if (i < 0)
    return;
  const j = Math.max(0, Math.min(t.length - 1, i + step));
  selectTime(t[j]);
}

// Playing the times: the next time is shown when its image has loaded
const player = {timer: null, delay: 600};

function play()
{
  if (player.timer)
  {
    clearTimeout(player.timer);
    player.timer = null;
    renderTimeControls();
    return;
  }
  const next = async () => {
    const t = times();
    const i = t.indexOf(state.sel.time);
    selectTime(t[(i + 1) % t.length]);
    await imagesLoaded();
    if (player.timer)
      player.timer = setTimeout(next, player.delay);
  };
  player.timer = setTimeout(next, 0);
  renderTimeControls();
}

function imagesLoaded()
{
  const images = [...document.querySelectorAll('#viewer img')];
  return Promise.all(images.map((img) => img.complete ? null : new Promise((resolve) => {
    img.addEventListener('load', resolve, {once: true});
    img.addEventListener('error', resolve, {once: true});
  })));
}

function renderTimeControls()
{
  const t = times();
  if (!t.length)
  {
    replace($('timecontrols'));
    replace($('timebar'));
    return;
  }
  const current = state.sel.time;
  const i = t.indexOf(current);
  const analysis = data.generation ? data.generation.analysisTime : '';

  const button = (label, title, onclick, disabled) => {
    const b = el('button', {type: 'button', title, disabled}, label);
    b.addEventListener('click', onclick);
    return b;
  };

  // The times grouped by day: long histories stay readable
  const groups = [];
  for (const x of t)
  {
    const day = formatTime(x).slice(0, 10);
    if (!groups.length || groups[groups.length - 1].group !== day)
      groups.push({group: day, options: []});
    groups[groups.length - 1].options.push({value: x, label: `${formatShortTime(x)} UTC  (${formatLeadTime(x, analysis)})`});
  }
  const timeSelect = select(groups, current, (v) => selectTime(v, true), {'aria-label': 'Time'});

  const speed = select([
    {value: 1500, label: 'slow'}, {value: 600, label: 'normal'}, {value: 200, label: 'fast'},
  ], player.delay, (v) => {
    player.delay = Number(v);
  }, {'aria-label': 'Animation speed'});

  replace($('timecontrols'),
      button('«', 'First time (Home)', () => selectTime(t[0]), i <= 0),
      button('‹', 'Previous time (←)', () => stepTime(-1), i <= 0),
      timeSelect,
      button('›', 'Next time (→)', () => stepTime(1), i >= t.length - 1),
      button('»', 'Last time (End)', () => selectTime(t[t.length - 1]), i >= t.length - 1),
      button(player.timer ? 'Pause' : 'Play', 'Play the times (Space)', play, t.length < 2),
      speed,
      el('span', {class: 'muted small'}, `${i + 1} / ${t.length}`));

  renderTimebar($('timebar'), t, current, {
    select: (time) => selectTime(time, true),
    preview: (time) => preview(time ? {time} : null),
  }, analysis);
}

// ### The viewer

function compareActive()
{
  return Boolean(data.compareGeneration) && viewInfo(state.view).image;
}

// The rendering parameters of the view (only the options it uses and that differ from
// the defaults, so that the image cache is shared with other users)
function renderParams(view, content)
{
  const params = {file: content.file, message: content.message, geometry: data.resolved.sel.geometry};
  if (projected.has(view) && state.projection)
    params.projection = state.projection;
  if (view === 'map' && state.mapArea === 'data')
  {
    const area = mapArea();
    if (area)
      params.area = area;
  }
  const options = currentOptions();
  for (const name of viewOptions[view] || [])
  {
    if (String(options[name]) !== String(data.meta.defaults[name]))
      params[name] = options[name];
  }
  return params;
}

// The area of the data on the map with a margin: "lon0,lat0,lon1,lat1", or null when the
// data covers (nearly) the whole world
function mapArea()
{
  const g = data.contents && data.contents.geometries[data.resolved.sel.geometry];
  if (!g || !g.bbox)
    return null;
  let [lon0, lat0, lon1, lat1] = g.bbox;
  const mx = Math.max(1, (lon1 - lon0) * 0.05);
  const my = Math.max(1, (lat1 - lat0) * 0.05);
  lon0 = Math.max(-180, lon0 - mx);
  lon1 = Math.min(180, lon1 + mx);
  lat0 = Math.max(-90, lat0 - my);
  lat1 = Math.min(90, lat1 + my);
  if (lon1 - lon0 > 300 && lat1 - lat0 > 150)
    return null;
  // Whole degrees, so that small differences between grids share the cached images
  return [Math.floor(lon0), Math.floor(lat0), Math.ceil(lon1), Math.ceil(lat1)].join(',');
}

function imageUrl(view, content)
{
  return renderUrl(view, renderParams(view, content));
}

function differenceUrl(a, b)
{
  return renderUrl('difference', Object.assign(renderParams('difference', a), {file2: b.file, message2: b.message}));
}

// The panes of the viewer: [{label, img, content}]
let panes = [];

function renderViewer()
{
  const viewer = $('viewer');
  panes = [];

  if (!data.content)
  {
    replace(viewer, el('div', {class: 'empty'}, data.contents ? 'No field at this selection.' : ''));
    replace($('legend'));
    renderReadout(null);
    return;
  }

  const view = viewInfo(state.view);

  // The readout belongs to the view: an old image value must not stay there
  if (!view.image)
  {
    lastReadout = null;
    renderReadout('');
  }

  if (view.sample)
  {
    renderSample(viewer, {file: data.content.file, message: data.content.message,
                          geometry: data.resolved.sel.geometry, mode: view.sample, units: units(),
                          readout: renderReadout});
    replace($('legend'));
    return;
  }

  if (view.frame)
  {
    replace(viewer, el('iframe', {class: 'frame', title: view.label,
                                  src: renderUrl(state.view, {file: data.content.file, message: data.content.message,
                                                              geometry: data.resolved.sel.geometry})}));
    replace($('legend'));
    return;
  }

  if (state.view === 'info')
  {
    const box = el('div', {class: 'info'}, el('div', {class: 'muted'}, 'Loading…'));
    replace(viewer, box);
    replace($('legend'));
    api('info', {file: data.content.file, message: data.content.message}, {key: 'info'})
        .then((info) => renderInfo(box, info))
        .catch(report);
    return;
  }

  // Images
  const makePane = (label, src, content, kind) => {
    const img = el('img', {src, alt: label || viewInfo(state.view).label, draggable: 'false'});
    img.addEventListener('error', () => status(`The ${(label || 'field').toLowerCase()} image failed`, 'error'));
    img.addEventListener('load', () => fitImage(img));
    const marker = el('div', {class: 'marker', hidden: true});
    const wrap = el('div', {class: 'pane-image'}, img, marker);
    const viewport = el('div', {class: 'pane-viewport', title: 'Wheel: zoom, drag: pan, double click: whole image'}, wrap);
    const pane = {label, img, marker, wrap, viewport, content, kind};
    img.addEventListener('mousemove', (event) => hover(pane, event));
    img.addEventListener('mouseleave', () => hover(null));
    img.addEventListener('click', (event) => {
      if (!zoom.dragged)
        pin(pane, event);
    });
    enableZoom(pane);
    panes.push(pane);
    return el('figure', {class: 'pane'}, label ? el('figcaption', {}, label) : null, viewport);
  };

  if (compareActive())
  {
    const a = data.content;
    const b = data.compareContent;
    const la = `A: ${formatTime(data.generation.analysisTime)}`;
    const lb = `B: ${formatTime(data.compareGeneration.analysisTime)}`;
    const children = [makePane(la, imageUrl('image', a), a, 'a')];
    if (b)
    {
      children.push(makePane(lb, imageUrl('image', b), b, 'b'));
      children.push(makePane('B − A', differenceUrl(a, b), a, 'diff'));
    }
    else
      children.push(el('figure', {class: 'pane missing'}, el('figcaption', {}, lb),
          el('div', {class: 'empty'}, 'Generation B has no field at this time.')));
    replace(viewer, el('div', {class: 'panes compare'}, children));
  }
  else
    replace(viewer, el('div', {class: 'panes'}, makePane('', imageUrl(state.view, data.content), data.content, 'a')));

  viewer.classList.toggle('pixelated', true);
  // A zoom applies to one image layout: the same view, geometry and projection
  const layout = [state.view, data.resolved.sel.geometry, state.projection, compareActive(), state.mapArea].join('/');
  if (zoom.layout !== layout)
  {
    zoom.layout = layout;
    Object.assign(zoom, {scale: 1, x: 0, y: 0});
  }
  applyZoom();
  renderLegend();
  showMarker();
  preloadNeighbours();
}

// Scales the image to the space available, also up: grids are often smaller than the
// screen. The size is set in pixels so that a position in the image maps exactly to the
// grid (no letterboxing).
function fitImage(img)
{
  const nw = img.naturalWidth;
  const nh = img.naturalHeight;
  if (!nw || !nh)
    return;
  const pane = img.closest('.pane');
  const viewport = img.closest('.pane-viewport');
  const availableWidth = Math.max(100, (pane ? pane.clientWidth : $('viewer').clientWidth) - 2);
  const top = (viewport || img).getBoundingClientRect().top + window.scrollY;
  const reserve = compareActive() ? 60 : 110;
  const availableHeight = Math.max(240, window.innerHeight - top - reserve);
  const scale = Math.min(availableWidth / nw, availableHeight / nh);
  const width = Math.floor(nw * scale) + 'px';
  const height = Math.floor(nh * scale) + 'px';
  img.style.width = width;
  img.style.height = height;
  if (viewport)
  {
    viewport.style.width = width;
    viewport.style.height = height;
  }
}

// ### Zooming: the same zoom in every pane, so that compared fields stay aligned

const zoom = {scale: 1, x: 0, y: 0, dragged: false};

function applyZoom()
{
  for (const pane of panes)
  {
    pane.wrap.style.transform = zoom.scale === 1 ? '' : `translate(${zoom.x}px, ${zoom.y}px) scale(${zoom.scale})`;
    // The marker keeps its size
    pane.wrap.style.setProperty('--marker-scale', String(1 / zoom.scale));
  }
}

function resetZoom()
{
  Object.assign(zoom, {scale: 1, x: 0, y: 0});
  applyZoom();
}

// Keeps the zoomed image covering the viewport
function clampZoom(viewport)
{
  const w = viewport.clientWidth;
  const h = viewport.clientHeight;
  zoom.x = Math.min(0, Math.max(w - w * zoom.scale, zoom.x));
  zoom.y = Math.min(0, Math.max(h - h * zoom.scale, zoom.y));
}

function enableZoom(pane)
{
  const viewport = pane.viewport;
  viewport.addEventListener('wheel', (event) => {
    event.preventDefault();
    const r = viewport.getBoundingClientRect();
    const px = event.clientX - r.left;
    const py = event.clientY - r.top;
    const scale = Math.min(32, Math.max(1, zoom.scale * (event.deltaY < 0 ? 1.25 : 0.8)));
    // The point under the pointer stays in place
    zoom.x = px - (px - zoom.x) * scale / zoom.scale;
    zoom.y = py - (py - zoom.y) * scale / zoom.scale;
    zoom.scale = scale;
    clampZoom(viewport);
    applyZoom();
  }, {passive: false});

  viewport.addEventListener('mousedown', (event) => {
    if (event.button !== 0 || zoom.scale === 1)
      return;
    event.preventDefault();
    const start = {x: event.clientX, y: event.clientY, zx: zoom.x, zy: zoom.y};
    zoom.dragged = false;
    const move = (e) => {
      const dx = e.clientX - start.x;
      const dy = e.clientY - start.y;
      if (Math.abs(dx) + Math.abs(dy) > 3)
        zoom.dragged = true;
      zoom.x = start.zx + dx;
      zoom.y = start.zy + dy;
      clampZoom(viewport);
      applyZoom();
    };
    const up = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
      // The click that ends a drag does not pin a point
      setTimeout(() => {
        zoom.dragged = false;
      }, 0);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  });

  viewport.addEventListener('dblclick', (event) => {
    event.preventDefault();
    resetZoom();
  });
}

window.addEventListener('resize', debounce(() => panes.forEach((p) => fitImage(p.img)), 150));

// Loads the images of the next and the previous time into the browser cache
const preloadNeighbours = debounce(() => {
  const t = times();
  const i = t.indexOf(state.sel.time);
  if (i < 0 || !viewInfo(state.view).image || compareActive())
    return;
  for (const j of [i + 1, i - 1])
  {
    const c = fieldContent(data.resolved.field, t[j]);
    if (c)
      new Image().src = imageUrl(state.view, c);
  }
}, 400);

// Hovering the time or level bar shows that field in the first pane
function preview(target)
{
  if (!panes.length || compareActive())
    return;
  const pane = panes[0];
  if (!target)
  {
    pane.img.src = imageUrl(state.view, data.content);
    $('readout').classList.remove('preview');
    renderReadout(lastReadout);
    return;
  }
  let content = null;
  let text = '';
  if (target.time)
  {
    content = fieldContent(data.resolved.field, target.time);
    text = `Preview ${formatShortTime(target.time)} UTC`;
  }
  else
  {
    content = target;
    text = `Preview level ${target.level}`;
  }
  if (content)
  {
    pane.img.src = imageUrl(state.view, content);
    $('readout').textContent = text;
    $('readout').classList.add('preview');
  }
}

// ### Values under the pointer

function relativePosition(img, event)
{
  const r = img.getBoundingClientRect();
  return {x: (event.clientX - r.left) / r.width, y: (event.clientY - r.top) / r.height};
}

let lastReadout = null;

const hoverValue = debounce(async (pane, pos) => {
  try
  {
    const params = {file: pane.content.file, message: pane.content.message, x: pos.x, y: pos.y};
    if (projected.has(state.view) && state.projection)
      params.projection = state.projection;
    const a = await api('value', params, {key: 'hover'});
    let text = `${formatLatLon(a.lat, a.lon)}  ` +
        (a.i !== undefined ? `(i ${a.i.toFixed(1)}, j ${a.j.toFixed(1)})  ` : '');
    if (compareActive() && data.compareContent)
    {
      const b = await api('value', {file: data.compareContent.file, message: data.compareContent.message,
                                    lat: a.lat, lon: a.lon}, {key: 'hoverB'});
      const d = a.value !== null && b.value !== null ? b.value - a.value : null;
      text += `A ${formatValue(a.value, units())}  B ${formatValue(b.value, units())}  B−A ${formatValue(d, units())}`;
    }
    else
      text += formatValue(a.value, units());
    lastReadout = text;
    renderReadout(text);
  }
  catch (error)
  {
    if (!isAbort(error))
      renderReadout('');
  }
}, 60);

function hover(pane, event)
{
  if (!pane)
  {
    hoverValue.cancel();
    return;
  }
  hoverValue(pane, relativePosition(pane.img, event));
}

function renderReadout(text)
{
  $('readout').textContent = text || ' ';
}

function units()
{
  const p = data.parameters.find((x) => x.name === state.parameter);
  return p ? p.units : '';
}

// ### The pinned point: its time series and vertical profile

let markerPosition = null;   // relative position in the image at the click

async function pin(pane, event)
{
  const pos = relativePosition(pane.img, event);
  try
  {
    const params = {file: pane.content.file, message: pane.content.message, x: pos.x, y: pos.y};
    if (projected.has(state.view) && state.projection)
      params.projection = state.projection;
    const v = await api('value', params);
    state.point = {lat: v.lat, lon: v.lon};
    markerPosition = {x: pos.x, y: pos.y, projection: state.projection, geometry: data.resolved.sel.geometry};
    writeUrl();
    showMarker();
    renderPoint();
    renderKeyPanel();
  }
  catch (error)
  {
    report(error);
  }
}

function unpin()
{
  state.point = null;
  markerPosition = null;
  writeUrl();
  showMarker();
  renderPoint();
  renderKeyPanel();
}

function showMarker()
{
  const valid = markerPosition && state.point && markerPosition.projection === state.projection &&
      markerPosition.geometry === (data.resolved && data.resolved.sel.geometry);
  for (const pane of panes)
  {
    pane.marker.hidden = !valid;
    if (valid)
    {
      pane.marker.style.left = (markerPosition.x * 100) + '%';
      pane.marker.style.top = (markerPosition.y * 100) + '%';
    }
  }
}

async function renderPoint()
{
  const section = $('point');
  const body = $('point-body');
  if (!state.point || !data.resolved || !data.resolved.field)
  {
    section.hidden = true;
    return;
  }
  section.hidden = false;
  const s = data.resolved.sel;
  const {lat, lon} = state.point;
  const close = el('button', {type: 'button', class: 'small'}, 'Remove point (Esc)');
  close.addEventListener('click', unpin);

  const series = el('div', {class: 'chart-box'}, el('div', {class: 'muted small'}, 'Loading…'));
  const profile = el('div', {class: 'chart-box'});
  replace(body, el('div', {class: 'point-head'}, el('span', {class: 'mono'}, formatLatLon(lat, lon)), close),
      el('div', {class: 'charts'}, series, profile));

  const common = {generation: data.generation.id, parameter: state.parameter, levelId: s.levelId,
                  forecastType: s.forecastType, forecastNumber: s.forecastNumber, geometry: s.geometry, lat, lon};
  const u = units();

  try
  {
    const ts = await api('series', {...common, level: s.level}, {key: 'series', maxAge: 5000});
    const points = ts.times.map((t, i) => [parseTime(t).getTime(), ts.values[i]]);
    const ticks = points.length ? timeTicks(points[0][0], points[points.length - 1][0]) : undefined;
    lineChart(series, {
      title: `${state.parameter} at level ${s.level}${u ? ` [${u}]` : ''}`,
      points,
      keyFormat: ticks && ticks.format,
      keyTicks: ticks,
      mark: parseTime(s.time).getTime(),
      describe: (p) => `${formatShortTime(isoTime(p[0]))} UTC: ${formatValue(p[1], u)}`,
      onClick: (p) => selectTime(isoTime(p[0]), true),
    });
  }
  catch (error)
  {
    replace(series);
    report(error);
  }

  if (data.resolved.choices.levels.length < 2)
  {
    replace(profile);
    return;
  }
  try
  {
    const pr = await api('profile', {...common, time: s.time}, {key: 'profile', maxAge: 5000});
    const lt = data.contents.levelTypes[s.levelId] || {};
    lineChart(profile, {
      title: `Profile at ${formatShortTime(s.time)} UTC`,
      points: pr.levels.map((l, i) => [l, pr.values[i]]),
      vertical: true,
      invertKeys: levelsGrowDown(lt),
      mark: s.level,
      describe: (p) => `Level ${p[0]}${lt.units ? ' ' + lt.units : ''}: ${formatValue(p[1], u)}`,
      onClick: (p) => {
        state.sel.level = p[0];
        applySelection();
      },
    });
  }
  catch (error)
  {
    replace(profile);
    report(error);
  }
}

// Milliseconds -> "20261024T150000"
function isoTime(ms)
{
  return new Date(ms).toISOString().replace(/[-:]/g, '').slice(0, 15);
}

// ### The legend

const renderLegend = debounce(async () => {
  const legend = $('legend');
  if (!data.content || !viewInfo(state.view).image || state.view.startsWith('streams'))
  {
    replace(legend);
    return;
  }
  const o = currentOptions();
  const u = units();
  try
  {
    if (compareActive() && data.compareContent)
    {
      const params = {file: data.content.file, message: data.content.message, geometry: data.resolved.sel.geometry,
                      file2: data.compareContent.file, message2: data.compareContent.message};
      if (state.projection)
        params.projection = state.projection;
      const stats = await api('stats', params, {key: 'legend', maxAge: 60000});
      differenceLegend(legend, stats.range, u, 'B − A');
    }
    else if (o.colormap && o.colormap !== 'None')
    {
      const params = {file: data.content.file, message: data.content.message, geometry: data.resolved.sel.geometry};
      if (state.projection && projected.has(state.view))
        params.projection = state.projection;
      const [colormap, stats] = await Promise.all([
        api('colormap', {name: o.colormap}, {maxAge: 60000}),
        api('stats', params, {maxAge: 60000}).catch(() => null),
      ]);
      const parameter = data.parameters.find((x) => x.name === state.parameter);
      colorMapLegend(legend, colormap, u, stats, data.meta.colorMapRanges || {}, (name) => setOption('colormap', name),
                     parameter ? parameter.description : '');
    }
    else
    {
      const params = {file: data.content.file, message: data.content.message, geometry: data.resolved.sel.geometry};
      if (state.projection && projected.has(state.view))
        params.projection = state.projection;
      const stats = await api('stats', params, {key: 'legend', maxAge: 60000});
      hsvLegend(legend, stats, o, u);
    }
  }
  catch (error)
  {
    if (!isAbort(error))
      replace(legend);
  }
}, 100);

// ### The level bar

function renderLevels()
{
  const r = data.resolved;
  if (!r || !viewInfo(state.view).image || compareActive())
  {
    replace($('levelbar'));
    return;
  }
  const levels = levelContents(data.contents.fields, r.sel);
  renderLevelbar($('levelbar'), levels, r.sel.level, {
    select: (level) => {
      state.sel.level = level;
      pushNext = true;
      applySelection();
    },
    preview: (l) => preview(l),
  }, data.contents.levelTypes[r.sel.levelId]);
}

// ### Everything

function renderAll()
{
  renderDataPanel();
  renderSettingsPanel();
  renderTimeDependent();
}

// The parts that change with the time
function renderTimeDependent()
{
  renderViewPanel();
  renderTimeControls();
  renderViewer();
  renderLevels();
  renderPoint();
  document.title = data.content
      ? `${state.parameter} ${state.producer} ${formatTime(state.sel.time)} — Grid GUI`
      : 'Grid GUI';
}

// ### Keyboard

document.addEventListener('keydown', (event) => {
  const target = event.target;
  if (event.altKey || event.ctrlKey || event.metaKey ||
      (target && ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName)))
    return;
  const t = times();
  switch (event.key)
  {
    case 'ArrowLeft':
      stepTime(-1);
      break;
    case 'ArrowRight':
      stepTime(1);
      break;
    case 'Home':
      selectTime(t[0]);
      break;
    case 'End':
      selectTime(t[t.length - 1]);
      break;
    case 'ArrowUp':
    case 'ArrowDown':
    {
      if (!data.resolved)
        return;
      const levels = data.resolved.choices.levels;
      const i = levels.indexOf(data.resolved.sel.level);
      const lt = data.contents.levelTypes[data.resolved.sel.levelId] || {};
      // Up is towards smaller pressures and model levels, but towards larger heights
      const up = levelsGrowDown(lt) ? -1 : 1;
      const j = i + (event.key === 'ArrowUp' ? up : -up);
      if (j < 0 || j >= levels.length)
        return;
      state.sel.level = levels[j];
      applySelection();
      break;
    }
    case ' ':
      play();
      break;
    case 'c':
    {
      const others = data.generations.filter((g) => g.id !== data.generation.id);
      state.compare = state.compare ? '' : (others[0] ? others[0].analysisTime : '');
      run(async () => {
        await loadCompare();
        updateContent();
      }, true);
      break;
    }
    case 'Escape':
      unpin();
      break;
    case '?':
      $('help').showModal();
      break;
    default:
      return;
  }
  event.preventDefault();
});

window.addEventListener('popstate', () => {
  readUrl();
  run(loadProducer);
});

$('help-button').addEventListener('click', () => $('help').showModal());

start();
