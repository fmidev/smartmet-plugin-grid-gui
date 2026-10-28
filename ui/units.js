// Unit variants of the parameters: a field stored in one unit can be shown in another one
// by the name with the other unit suffix, such as T-C for T-K, as in the data explorer.
// The values are converted linearly, value * scale + offset: the server converts them for
// the images and the statistics of the legend, the page for the values it shows.

// The units of a quantity by the suffix of the FMI name. scale and offset convert a value in
// the first unit of the quantity to the unit; units are the spellings of the unit in the
// parameter definitions and words those in the names of the colour maps.
const quantities = [
  {
    quantity: 'temperature',
    units: [
      {suffix: 'K', label: 'K', scale: 1, offset: 0, units: ['K'], words: ['Kelvins', 'Kelvin']},
      {suffix: 'C', label: '°C', scale: 1, offset: -273.15, units: ['C', '°C'], words: ['Celcius', 'Celsius']},
    ],
  },
  {
    quantity: 'pressure',
    units: [
      {suffix: 'PA', label: 'Pa', scale: 1, offset: 0, units: ['Pa', 'kg m-1 s-2']},
      {suffix: 'HPA', label: 'hPa', scale: 0.01, offset: 0, units: ['hPa']},
    ],
  },
  {
    quantity: 'speed',
    units: [
      {suffix: 'MS', label: 'm/s', scale: 1, offset: 0, units: ['m s-1', 'm/s']},
      {suffix: 'KT', label: 'kt', scale: 1.943844, offset: 0, units: ['kt', 'knot', 'knots']},
      {suffix: 'KMH', label: 'km/h', scale: 3.6, offset: 0, units: ['km h-1', 'km/h']},
    ],
  },
];

// Statistics such as deviations and differences of temperatures are not absolute
// temperatures, so the offset of Kelvins and Celsius does not apply to them
const relativeTemperature = /STDDEV|SPREAD|DIFF|ANOM|DEV-/;

// The quantity and unit of a parameter by its name, checked against the units of its
// definition: {stem, quantity, unit} or null
function parse(parameter)
{
  const m = /^(.+)-([A-Z]+)$/.exec(String(parameter.name).toUpperCase());
  if (!m)
    return null;
  for (const q of quantities)
  {
    const unit = q.units.find((u) => u.suffix === m[2]);
    if (!unit || !unit.units.includes(parameter.units))
      continue;
    if (q.quantity === 'temperature' && relativeTemperature.test(m[1]))
      return null;
    return {stem: parameter.name.slice(0, m[1].length), quantity: q, unit};
  }
  return null;
}

// The unit variants of the parameters that are not stored themselves:
// [{name, description, units, stored, from, to, quantity}], each after the stored parameter
export function unitVariants(parameters)
{
  const names = new Set(parameters.map((p) => p.name.toUpperCase()));
  const variants = [];
  for (const p of parameters)
  {
    const parsed = parse(p);
    if (!parsed)
      continue;
    for (const unit of parsed.quantity.units)
    {
      const name = `${parsed.stem}-${unit.suffix}`;
      if (unit === parsed.unit || names.has(name.toUpperCase()))
        continue;
      variants.push({name, description: p.description, units: unit.label, stored: p.name,
                     from: parsed.unit, to: unit, quantity: parsed.quantity.quantity});
    }
  }
  return variants;
}

// The parameter shown by a name: {name, stored, variant}, null when there is no such
// parameter. The variant of a stored parameter is null, except for a temperature: its values
// may be in the other unit than its name tells, and are then converted.
export function resolveParameter(name, parameters, variants)
{
  if (!name)
    return null;
  const p = parameters.find((x) => x.name === name);
  if (p)
  {
    const parsed = parse(p);
    const variant = parsed && parsed.quantity.quantity === 'temperature'
        ? {name, description: p.description, units: parsed.unit.label, stored: name,
           from: parsed.unit, to: parsed.unit, quantity: 'temperature'}
        : null;
    return {name, stored: name, variant};
  }
  const v = variants.find((x) => x.name === name);
  return v ? {name, stored: v.stored, variant: v} : null;
}

// The names of the units the parameter can be shown in, itself first: [{name, label}]
export function unitChoices(stored, parameters, variants)
{
  const p = parameters.find((x) => x.name === stored);
  if (!p)
    return [];
  const parsed = parse(p);
  const own = {name: stored, label: parsed ? parsed.unit.label : p.units};
  const others = variants.filter((v) => v.stored === stored).map((v) => ({name: v.name, label: v.units}));
  return others.length ? [own, ...others] : [];
}

// Whether the unit the values are stored in must be checked from the values: temperatures
// named in Kelvins may be stored in Celsius (querydata), and the other way round
export function needsStoredUnit(variant)
{
  return Boolean(variant) && variant.quantity === 'temperature';
}

// The unit the values are in by their mean: no temperature in Celsius is above 150, and
// none in Kelvins below it
export function storedTemperatureUnit(variant, mean)
{
  const units = quantities.find((q) => q.quantity === 'temperature').units;
  return units.find((u) => u.suffix === (mean > 150 ? 'K' : 'C')) || variant.from;
}

// The conversion of the stored values to the unit shown: {scale, offset, convert(v)}, null
// when the values are shown as they are. from overrides the unit the values are in.
export function conversion(variant, from = null)
{
  if (!variant)
    return null;
  const a = from || variant.from;
  const b = variant.to;
  const scale = b.scale / a.scale;
  const offset = b.offset - a.offset * scale;
  if (scale === 1 && offset === 0)
    return null;
  // Rounded so that the image addresses stay the same between page loads
  const round = (x) => Number(x.toPrecision(10));
  const s = round(scale);
  const o = round(offset);
  return {scale: s, offset: o, convert: (v) => (v === null || v === undefined ? v : v * s + o)};
}

// The unit a parameter is shown in: the unit of the variant, or that of the stored parameter
export function shownUnit(shown, parameters)
{
  if (!shown)
    return null;
  if (shown.variant)
    return shown.variant.to;
  const p = parameters.find((x) => x.name === shown.stored);
  const parsed = p ? parse(p) : null;
  return parsed ? parsed.unit : null;
}

// The colour map of another unit matching a colour map by its name, such as
// "Temperature (-33C..68C)" for "Temperature (240K..341K)" or "Dali Temperature (Celcius)"
// for "Dali Temperature (Kelvins)", null when there is none
export function counterpartColorMap(name, names, from, to)
{
  const c = conversion({to}, from);
  if (!c || !name)
    return null;
  const escape = (text) => text.replace(/[.*+?^${}()|[\]\\/]/g, '\\$&');
  // The values with the unit: 240K -> -33C
  const values = new RegExp(`(-?\\d+(?:\\.\\d+)?)${escape(from.suffix)}\\b`, 'gi');
  let candidates = [name.replace(values, (m, n) => `${Math.round(c.convert(Number(n)))}${to.suffix}`)];
  // The names of the units: Kelvins -> Celcius or Celsius
  for (const word of from.words || [])
  {
    const pattern = new RegExp(`\\b${escape(word)}\\b`, 'gi');
    candidates = candidates.flatMap((n) => (pattern.test(n) ? (to.words || []).map((w) => n.replace(pattern, w)) : [n]));
  }
  const wanted = new Set(candidates.map((n) => n.toLowerCase()));
  return names.find((n) => n !== name && wanted.has(n.toLowerCase())) || null;
}

// The colour map for a unit: the counterpart of a colour map made for another unit of the
// same quantity, null when the colour map has no unit or is already in this one
export function colorMapForUnit(name, names, unit)
{
  const q = unit ? quantities.find((x) => x.units.includes(unit)) : null;
  for (const other of q ? q.units : [])
  {
    const counterpart = other === unit ? null : counterpartColorMap(name, names, other, unit);
    if (counterpart)
      return counterpart;
  }
  return null;
}
