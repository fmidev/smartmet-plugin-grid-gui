// The Info view: the producer, generation, file and content records of the message and
// the attributes of the message, with a filter for the attributes.

import {el, replace} from './dom.js';

export function renderInfo(container, info)
{
  const sections = [];
  for (const [title, record] of Object.entries(info))
  {
    if (title === 'Attributes')
      continue;
    sections.push(el('section', {class: 'info-section'},
        el('h3', {}, title),
        el('table', {class: 'info-table'},
            el('tbody', {}, Object.entries(record).map(([k, v]) =>
                el('tr', {}, el('th', {}, k), el('td', {class: 'mono'}, String(v))))))));
  }

  const attributes = info.Attributes || [];
  const body = el('tbody');
  const filter = el('input', {type: 'search', placeholder: `Filter ${attributes.length} attributes`,
                              'aria-label': 'Filter the attributes'});
  const fill = () => {
    const f = filter.value.trim().toLowerCase();
    replace(body, attributes
        .filter(([k, v]) => !f || k.toLowerCase().includes(f) || String(v).toLowerCase().includes(f))
        .map(([k, v]) => el('tr', {}, el('th', {class: 'mono'}, k.replace(/^message\[\d+\]\./, '')),
            el('td', {class: 'mono'}, String(v)))));
  };
  filter.addEventListener('input', fill);
  fill();

  replace(container,
      el('div', {class: 'info-grid'}, sections),
      el('section', {class: 'info-section'}, el('h3', {}, 'Attributes'), filter,
          el('table', {class: 'info-table'}, body)));
}
