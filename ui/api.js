// The JSON API (/gui/api/NAME) and the URLs of the rendered images (/gui/PAGE).
// The page is at /gui/, so the relative URLs work under any configured prefix.

export class ApiError extends Error
{
}

const cache = new Map();

// Fetches api/NAME?params. Answers of the same request are shared while they are fresh;
// a newer request with the same signal key aborts the older one.
export async function api(name, params = {}, {key = null, maxAge = 0} = {})
{
  const url = 'api/' + name + query(params);

  if (maxAge > 0)
  {
    const hit = cache.get(url);
    if (hit && Date.now() - hit.time < maxAge)
      return hit.promise;
  }

  let signal;
  if (key)
  {
    controllers.get(key)?.abort();
    const controller = new AbortController();
    controllers.set(key, controller);
    signal = controller.signal;
  }

  const promise = (async () => {
    const response = await fetch(url, {signal});
    let json = null;
    try
    {
      json = await response.json();
    }
    catch (e)
    {
      // Not JSON: an error page of the server
    }
    if (!response.ok)
    {
      const message = (json && json.error) || response.headers.get('X-Content-Error') ||
          `${response.status} ${response.statusText}`;
      throw new ApiError(message);
    }
    return json;
  })();

  if (maxAge > 0)
  {
    cache.set(url, {time: Date.now(), promise});
    promise.catch(() => cache.delete(url));
  }
  return promise;
}

const controllers = new Map();

export function isAbort(error)
{
  return error && error.name === 'AbortError';
}

// ?a=1&b=2 without empty values
export function query(params)
{
  const q = new URLSearchParams();
  for (const [key, value] of Object.entries(params))
  {
    if (value !== undefined && value !== null && value !== '')
      q.set(key, String(value));
  }
  const s = q.toString();
  return s ? '?' + s : '';
}

// The URL of a rendered page: image, map, streams, streamsAnimation, difference,
// table, coordinates, message or download
export function renderUrl(page, params)
{
  return page + query(params);
}
