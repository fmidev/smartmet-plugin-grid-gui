// Small DOM helpers. Text is always set with textContent, never as HTML.

// el('div', {class: 'x', onclick: f}, child, 'text', ...)
export function el(tag, attrs = {}, ...children)
{
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {}))
  {
    if (value === undefined || value === null || value === false)
      continue;
    if (key.startsWith('on') && typeof value === 'function')
      node.addEventListener(key.slice(2), value);
    else if (key === 'class')
      node.className = value;
    else if (key === 'dataset')
      Object.assign(node.dataset, value);
    else if (key === 'style' && typeof value === 'object')
      Object.assign(node.style, value);
    else if (value === true)
      node.setAttribute(key, '');
    else
      node.setAttribute(key, String(value));
  }
  append(node, children);
  return node;
}

export function append(node, children)
{
  for (const child of children.flat(Infinity))
  {
    if (child === undefined || child === null || child === false)
      continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

export function clear(node)
{
  while (node.firstChild)
    node.removeChild(node.firstChild);
  return node;
}

export function replace(node, ...children)
{
  return append(clear(node), children);
}

// An SVG element
export function svg(tag, attrs = {}, ...children)
{
  const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [key, value] of Object.entries(attrs))
  {
    if (value !== undefined && value !== null)
      node.setAttribute(key, String(value));
  }
  append(node, children);
  return node;
}

// A <select> with the given options [{value, label, title, disabled}] or [value, ...].
// A drop-down list with fewer than two choices is disabled: it shows the value but it
// cannot be changed.
export function select(options, selected, onchange, attrs = {})
{
  const node = el('select', attrs);
  fillSelect(node, options, selected);
  if (onchange)
    node.addEventListener('change', () => onchange(node.value));
  return node;
}

export function fillSelect(node, options, selected)
{
  clear(node);
  for (const option of options)
  {
    if (option && option.group)
    {
      const group = el('optgroup', {label: option.group});
      for (const o of option.options)
        group.append(optionElement(o, selected));
      node.append(group);
    }
    else
      node.append(optionElement(option, selected));
  }
  if (!node.multiple && node.size <= 1)
    node.disabled = Array.from(node.options).filter((o) => !o.disabled).length < 2;
  return node;
}

function optionElement(option, selected)
{
  const o = typeof option === 'object' ? option : {value: option, label: String(option)};
  const node = el('option', {value: o.value, title: o.title, disabled: o.disabled}, o.label);
  if (String(o.value) === String(selected))
    node.selected = true;
  return node;
}

// Calls f at most once per wait milliseconds, with the latest arguments
export function debounce(f, wait)
{
  let timer = null;
  const debounced = (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => f(...args), wait);
  };
  debounced.cancel = () => clearTimeout(timer);
  return debounced;
}

// Copies the text to the clipboard and flashes the button
export async function copyText(text, button)
{
  try
  {
    await navigator.clipboard.writeText(text);
    flash(button, 'Copied');
  }
  catch (e)
  {
    // Not a secure context (plain http): fall back to a selection
    const area = el('textarea', {style: {position: 'fixed', opacity: '0'}}, text);
    document.body.append(area);
    area.select();
    const ok = document.execCommand('copy');
    area.remove();
    flash(button, ok ? 'Copied' : 'Copy failed');
  }
}

function flash(button, text)
{
  if (!button)
    return;
  const old = button.textContent;
  button.textContent = text;
  button.disabled = true;
  setTimeout(() => {
    button.textContent = old;
    button.disabled = false;
  }, 1000);
}

// localStorage that never throws
export const store = {
  get(key, fallback = null)
  {
    try
    {
      const value = localStorage.getItem('grid-gui.' + key);
      return value === null ? fallback : JSON.parse(value);
    }
    catch (e)
    {
      return fallback;
    }
  },
  set(key, value)
  {
    try
    {
      localStorage.setItem('grid-gui.' + key, JSON.stringify(value));
    }
    catch (e)
    {
      // Storage disabled: nothing is remembered
    }
  },
};
