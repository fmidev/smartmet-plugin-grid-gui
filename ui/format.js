// Formatting of times, values and levels

// "20261024T150000" -> Date (UTC)
export function parseTime(s)
{
  const m = /^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})$/.exec(s || '');
  if (!m)
    return null;
  return new Date(Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6]));
}

// "20261024T150000" -> "2026-10-24 15:00"
export function formatTime(s)
{
  const m = /^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})$/.exec(s || '');
  if (!m)
    return s || '';
  return `${m[1]}-${m[2]}-${m[3]} ${m[4]}:${m[5]}` + (m[6] !== '00' ? ':' + m[6] : '');
}

// "20261024T150000" -> "Sat 24.10. 15:00"
const weekdays = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
export function formatShortTime(s)
{
  const d = parseTime(s);
  if (!d)
    return s || '';
  return `${weekdays[d.getUTCDay()]} ${d.getUTCDate()}.${d.getUTCMonth() + 1}. ` +
      `${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}`;
}

export function pad(n)
{
  return String(n).padStart(2, '0');
}

// The age of a time relative to now: "35 min ago", "5 h ago", "3 d ago", "in 2 h"
export function formatAge(s, now = Date.now())
{
  const d = parseTime(s);
  if (!d)
    return '';
  const minutes = Math.round((now - d.getTime()) / 60000);
  if (Math.abs(minutes) > 60 * 24 * 60)
    return d.toISOString().slice(0, 10);
  const future = minutes < 0;
  const m = Math.abs(minutes);
  let text;
  if (m < 90)
    text = `${m} min`;
  else if (m < 48 * 60)
    text = `${Math.round(m / 60)} h`;
  else
    text = `${Math.round(m / 1440)} d`;
  return future ? `in ${text}` : `${text} ago`;
}

// The difference between a time and a reference time as a lead time: "+36 h", "-3 h"
export function formatLeadTime(time, reference)
{
  const a = parseTime(time);
  const b = parseTime(reference);
  if (!a || !b)
    return '';
  const minutes = Math.round((a - b) / 60000);
  const sign = minutes < 0 ? '−' : '+';
  const m = Math.abs(minutes);
  if (m % 60 === 0)
    return `${sign}${m / 60} h`;
  return `${sign}${Math.floor(m / 60)} h ${m % 60} min`;
}

// A value with a sensible number of significant digits
export function formatValue(value, units = '')
{
  if (value === null || value === undefined || !Number.isFinite(value))
    return 'missing';
  const a = Math.abs(value);
  let text;
  if (a === 0)
    text = '0';
  else if (a >= 1e6 || a < 1e-4)
    text = value.toExponential(3);
  else if (a >= 1000)
    text = value.toFixed(1);
  else if (a >= 1)
    text = value.toPrecision(5).replace(/\.?0+$/, '');
  else
    text = value.toPrecision(4).replace(/0+$/, '').replace(/\.$/, '');
  return units ? `${text} ${units}` : text;
}

// A tick label: as short as possible for the step between ticks
export function formatTick(value, step)
{
  if (!Number.isFinite(value))
    return '';
  const decimals = Math.max(0, Math.min(6, -Math.floor(Math.log10(Math.abs(step) || 1))));
  return value.toFixed(decimals);
}

// 60.123, 24.9 -> "60.123°N 24.900°E"
export function formatLatLon(lat, lon)
{
  const ns = lat >= 0 ? 'N' : 'S';
  const ew = lon >= 0 ? 'E' : 'W';
  return `${Math.abs(lat).toFixed(3)}°${ns} ${Math.abs(lon).toFixed(3)}°${ew}`;
}

// The level with its units when the level type has them. Pressure levels are stored in
// Pa by some producers and in hPa by others; the units of the level type tell which.
export function formatLevel(level, levelType)
{
  const units = levelType && levelType.units ? ' ' + levelType.units : '';
  return `${level}${units}`;
}

// Do the levels of the level type grow downwards (pressure, model levels from the top)?
export function levelsGrowDown(levelType)
{
  return /press|hybrid|model/i.test((levelType && (levelType.name + ' ' + levelType.description)) || '');
}

// Time ticks (milliseconds) at whole UTC hours or days, about count of them, with a
// label function suitable for their spacing
export function timeTicks(min, max, count = 5)
{
  const hour = 3600000;
  const steps = [1, 2, 3, 6, 12, 24, 48, 72, 168, 336, 720].map((h) => h * hour);
  const step = steps.find((s) => (max - min) / s <= count) || steps[steps.length - 1];
  const ticks = [];
  for (let t = Math.ceil(min / step) * step; t <= max; t += step)
    ticks.push(t);
  // The date at midnight and the hour otherwise: a date on every label does not fit
  ticks.format = (t) => {
    const d = new Date(t);
    return d.getUTCHours() === 0 ? `${d.getUTCDate()}.${d.getUTCMonth() + 1}.` : `${pad(d.getUTCHours())}:00`;
  };
  return ticks;
}

// Nice tick values from min to max, about count of them
export function niceTicks(min, max, count = 6)
{
  if (!Number.isFinite(min) || !Number.isFinite(max))
    return [];
  if (min === max)
    return [min];
  const span = max - min;
  const raw = span / count;
  const power = Math.pow(10, Math.floor(Math.log10(raw)));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * power).find((s) => s >= raw) || raw;
  const ticks = [];
  for (let v = Math.ceil(min / step) * step; v <= max + step * 1e-9; v += step)
    ticks.push(Math.abs(v) < step * 1e-9 ? 0 : v);
  ticks.step = step;
  return ticks;
}
