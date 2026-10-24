// The selection of a field from the contents of a parameter (api/contents).
//
// A field is one level type, level, forecast type, forecast number and geometry, with
// its times and the file and message of each time. Each choice limits the next ones:
// level type -> level -> forecast type -> forecast number -> geometry -> time.

import {parseTime} from './format.js';

const numeric = (a, b) => a - b;

function unique(values)
{
  return [...new Set(values)].sort(numeric);
}

// The value wanted if it is one of the values, otherwise the fallback
function pick(values, wanted, fallback)
{
  if (wanted !== undefined && wanted !== null && wanted !== '' && values.includes(Number(wanted)))
    return Number(wanted);
  return fallback(values);
}

const first = (values) => values[0];

// Deterministic (-1) or the control member (0) before the perturbed members
const firstMember = (values) =>
  values.includes(-1) ? -1 : values.includes(0) ? 0 : values[0];

// The choices and the resolved selection. wanted has the previous or requested values:
// {levelId, level, forecastType, forecastNumber, geometry, time}
export function resolve(fields, wanted = {})
{
  const choices = {};
  const sel = {};

  choices.levelIds = unique(fields.map((f) => f.levelId));
  sel.levelId = pick(choices.levelIds, wanted.levelId, first);
  let list = fields.filter((f) => f.levelId === sel.levelId);

  choices.levels = unique(list.map((f) => f.level));
  sel.level = pick(choices.levels, wanted.level, first);
  list = list.filter((f) => f.level === sel.level);

  choices.forecastTypes = unique(list.map((f) => f.forecastType));
  sel.forecastType = pick(choices.forecastTypes, wanted.forecastType, first);
  list = list.filter((f) => f.forecastType === sel.forecastType);

  choices.forecastNumbers = unique(list.map((f) => f.forecastNumber));
  sel.forecastNumber = pick(choices.forecastNumbers, wanted.forecastNumber, firstMember);
  list = list.filter((f) => f.forecastNumber === sel.forecastNumber);

  choices.geometries = unique(list.map((f) => f.geometry));
  sel.geometry = pick(choices.geometries, wanted.geometry, first);

  const field = list.find((f) => f.geometry === sel.geometry) || null;
  sel.time = field ? nearestTime(field.times, wanted.time) : null;

  return {choices, sel, field};
}

// The time wanted, or the nearest one to it, or the one nearest to now if the times
// reach now, or the first one
export function nearestTime(times, wanted)
{
  if (!times.length)
    return null;
  if (wanted && times.includes(wanted))
    return wanted;

  let target = parseTime(wanted);
  if (!target)
  {
    const now = new Date();
    const firstTime = parseTime(times[0]);
    const lastTime = parseTime(times[times.length - 1]);
    if (firstTime <= now && now <= lastTime)
      target = now;
    else
      return times[0];
  }

  let best = times[0];
  let bestDiff = Infinity;
  for (const t of times)
  {
    const diff = Math.abs(parseTime(t) - target);
    if (diff < bestDiff)
    {
      best = t;
      bestDiff = diff;
    }
  }
  return best;
}

// The file and message of the field at a time, or null
export function fieldContent(field, time)
{
  if (!field)
    return null;
  const i = field.times.indexOf(time);
  if (i < 0)
    return null;
  return {file: field.files[i], message: field.messages[i], time, index: i};
}

// The fields of the other levels of the level type at the same forecast type, number and
// geometry, for the level bar: [{level, file, message}] for the levels that have the time
export function levelContents(fields, sel)
{
  const result = [];
  for (const f of fields)
  {
    if (f.levelId !== sel.levelId || f.forecastType !== sel.forecastType ||
        f.forecastNumber !== sel.forecastNumber || f.geometry !== sel.geometry)
      continue;
    const c = fieldContent(f, sel.time);
    if (c)
      result.push({level: f.level, file: c.file, message: c.message});
  }
  return result.sort((a, b) => a.level - b.level);
}

// The same field in another generation (compare mode): same level type, level, forecast
// type, forecast number and geometry, at the same time
export function matchingContent(fields, sel)
{
  const field = fields.find((f) => f.levelId === sel.levelId && f.level === sel.level &&
      f.forecastType === sel.forecastType && f.forecastNumber === sel.forecastNumber &&
      f.geometry === sel.geometry);
  return field ? {field, content: fieldContent(field, sel.time)} : {field: null, content: null};
}
