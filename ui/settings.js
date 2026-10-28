// The rendering options of the images. The names are the request parameters of the
// rendering pages (Gui.cpp), the defaults come from api/meta.

import {colorPicker, closeColorPicker} from './colorpicker.js';
import {el, replace, select, store} from './dom.js';

const positions = [
  {value: '0', label: 'Off'},
  {value: '1', label: 'Below data'},
  {value: '2', label: 'Above data'},
];

// The options used by each view
export const viewOptions = {
  image: ['colormap', 'opacity', 'hue', 'saturation', 'blur', 'coordinateLines', 'landBorder',
          'landColor', 'landColorPosition', 'seaColor', 'seaColorPosition', 'landShadingLight',
          'landShadingShadow', 'landShadingPosition', 'seaShadingLight', 'seaShadingShadow',
          'seaShadingPosition'],
  map: ['colormap', 'hue', 'saturation', 'blur', 'coordinateLines', 'landBorder', 'landColor',
        'seaColor', 'missing'],
  streams: ['streamColor', 'step', 'minLength', 'maxLength', 'coordinateLines', 'landBorder',
            'landColor', 'landColorPosition', 'seaColor', 'seaColorPosition', 'landShadingLight',
            'landShadingShadow', 'landShadingPosition', 'seaShadingLight', 'seaShadingShadow',
            'seaShadingPosition'],
};
viewOptions.streamsAnimation = viewOptions.streams;
viewOptions.difference = viewOptions.image.filter((o) => !['colormap', 'hue', 'saturation', 'blur'].includes(o));

// Whether the map layers are shown: closed at first, so that the panel is short for new users
let layersOpen = store.get('layersOpen', false);

function colorSelect(colors, value, onchange, label)
{
  return colorPicker(colors, value, onchange, label);
}

// A slider whose value label follows it; the change is reported when it is released
function slider(min, max, step, value, onchange, attrs = {})
{
  const output = el('output', {class: 'mono small'}, String(value));
  const input = el('input', {type: 'range', min, max, step, value, ...attrs});
  input.addEventListener('input', () => {
    output.textContent = input.value;
  });
  input.addEventListener('change', () => onchange(input.value));
  return el('span', {class: 'slider'}, input, output);
}

function row(label, help, ...controls)
{
  return el('div', {class: 'row'}, el('label', {title: help}, label), el('div', {class: 'controls'}, controls));
}

// The map layers in a panel that stays open or closed when the options are drawn again
function layerDetails(layers)
{
  const details = el('details', {class: 'layers', open: layersOpen}, el('summary', {}, 'Map layers'), layers);
  details.addEventListener('toggle', () => {
    layersOpen = details.open;
    store.set('layersOpen', layersOpen);
  });
  return details;
}

// Renders the options of the view. set(name, value) changes one option.
export function renderSettings(container, meta, options, view, set)
{
  closeColorPicker();
  const used = new Set(viewOptions[view] || []);
  if (used.size === 0)
  {
    replace(container, el('div', {class: 'muted small'}, 'This view has no rendering options.'));
    return;
  }

  const colors = meta.colors;
  const parts = [];
  const noColorMap = !options.colormap || options.colormap === 'None';

  if (used.has('colormap'))
  {
    parts.push(row('Colour map', 'Value to colour mapping. None: one hue, lowest values brightest.',
        select(['None', ...meta.colorMaps], options.colormap, (v) => set('colormap', v), {class: 'wide'})));
  }
  if (used.has('opacity'))
    parts.push(row('Opacity', 'Opacity of the data layer, 0..255', slider(0, 255, 1, options.opacity, (v) => set('opacity', v))));

  if (used.has('hue') && noColorMap)
  {
    const hue = slider(0, 255, 1, options.hue, (v) => set('hue', v), {class: 'hue'});
    parts.push(row('Hue', 'The hue of the values when there is no colour map', hue));
    parts.push(row('Saturation', 'The saturation of the hue', slider(0, 255, 1, options.saturation, (v) => set('saturation', v))));
    parts.push(row('Banding', 'Brightness steps merged into one band (1 = smooth)', slider(1, 50, 1, options.blur, (v) => set('blur', v))));
  }

  if (used.has('streamColor'))
  {
    parts.push(row('Stream colour', 'Colour of the stream lines', colorSelect(colors, options.streamColor, (v) => set('streamColor', v), 'Stream colour')));
    parts.push(row('Spacing', 'Grid points between the stream line starts', slider(2, 98, 2, options.step, (v) => set('step', v))));
    parts.push(row('Min length', 'Shorter stream lines are not drawn', slider(2, 2048, 2, options.minLength, (v) => set('minLength', v))));
    parts.push(row('Max length', 'Stream lines are cut at this length', slider(8, 2048, 4, options.maxLength, (v) => set('maxLength', v))));
  }

  const layers = [];
  if (used.has('coordinateLines'))
    layers.push(row('Lat/lon lines', 'Colour of the coordinate lines', colorSelect(colors, options.coordinateLines, (v) => set('coordinateLines', v), 'Lat/lon lines')));
  if (used.has('landBorder'))
    layers.push(row('Coastline', 'Colour of the land border', colorSelect(colors, options.landBorder, (v) => set('landBorder', v), 'Coastline')));
  if (used.has('landColor'))
  {
    layers.push(row('Land', 'Land colour and its position relative to the data',
        colorSelect(colors, options.landColor, (v) => set('landColor', v), 'Land'),
        used.has('landColorPosition') ? select(positions, options.landColorPosition, (v) => set('landColorPosition', v)) : null));
  }
  if (used.has('seaColor'))
  {
    layers.push(row('Sea', 'Sea colour and its position relative to the data',
        colorSelect(colors, options.seaColor, (v) => set('seaColor', v), 'Sea'),
        used.has('seaColorPosition') ? select(positions, options.seaColorPosition, (v) => set('seaColorPosition', v)) : null));
  }
  if (used.has('landShadingPosition'))
  {
    layers.push(row('Land relief', 'Terrain shading: position, light and shadow',
        select(positions, options.landShadingPosition, (v) => set('landShadingPosition', v)),
        slider(0, 512, 16, options.landShadingLight, (v) => set('landShadingLight', v), {title: 'Light'}),
        slider(0, 512, 16, options.landShadingShadow, (v) => set('landShadingShadow', v), {title: 'Shadow'})));
    layers.push(row('Sea depth', 'Sea depth shading: position, light and shadow',
        select(positions, options.seaShadingPosition, (v) => set('seaShadingPosition', v)),
        slider(0, 512, 16, options.seaShadingLight, (v) => set('seaShadingLight', v), {title: 'Light'}),
        slider(0, 512, 16, options.seaShadingShadow, (v) => set('seaShadingShadow', v), {title: 'Shadow'})));
  }
  if (used.has('missing'))
    layers.push(row('Missing', 'How the map shows missing values', select(['Default', 'Zero'], options.missing, (v) => set('missing', v))));

  const reset = el('button', {type: 'button', class: 'small'}, 'Reset to defaults');
  reset.addEventListener('click', () => set(null, null));

  replace(container,
      parts,
      layers.length ? layerDetails(layers) : null,
      el('div', {class: 'buttons'}, reset));
}
