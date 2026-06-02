# Grid-GUI Plugin — Feature List

A structured inventory of user-facing features in the SmartMet Grid-GUI plugin.
Use as a checklist when drafting release notes. When new functionality is added,
append the new entry under the matching section (and bump the *Last updated*
line at the bottom).

The plugin is served by SmartMet Server at `/grid-gui` and is administered
through a web browser.

---

## 1. Grid data selection

- **Producer** dropdown — pick a data producer registered in the Content Server.
- **Generation** dropdown — pick a model generation (forecast cycle) for the producer.
- **Parameter** dropdown — pick a meteorological parameter (FMI parameter name).
- **Level type** dropdown — filter parameters by level type (pressure, height, …).
- **Level value** dropdown — pick a numeric level inside the chosen level type.
- **Forecast type / forecast number** — choose deterministic, control, perturbed,
  ensemble member, etc.
- **Time** dropdown — pick a forecast time from the available times.
- **Time grouping** — group times by year, month, day, or hour for faster scanning
  of long forecast histories.
- **Year selector** — jump to a specific year when scrubbing through long archives.
- **File / message navigation** — previous / next buttons step through the available
  files and messages for the current selection.

## 2. Geometry & projection

- **Geometry** dropdown — pick the source grid geometry (the data's native grid).
- **Projection** dropdown — pick a different geometry to render the data into;
  the plugin reprojects on-the-fly via the grid engine.
- **Projection lock** — keep the current projection choice when the geometry or
  producer changes, so comparisons stay in the same map view.
- **Blocked projections** — geometries listed in the config as too large to render
  are skipped automatically.

## 3. Presentation modes

Set by the *Presentation* dropdown.

- **Image** — color-mapped grid rendered as a still PNG.
- **Streams** — vector-field streamlines rendered as a still WebP.
- **Streams animation** — animated WebP showing flow over time.
- **Map** — grid overlaid on the world map.
- **Table (sample)** — sampled tabular dump of grid values.
- **Coordinates (sample)** — sampled tabular dump of grid coordinates.
- **Info** — grid metadata: dimensions, projection parameters, geometry id, etc.
- **Message** — raw GRIB/NetCDF message contents.
- **Download** — original file download.

## 4. Image-rendering controls

- **Color map** — pick a named CSV color map (e.g. "Dali Temperature (Celsius)")
  from the configured color-map files.
- **HSV fallback** — when no color map is selected, render with adjustable hue,
  saturation, and blur for quick value-range inspection.
- **Opacity / alpha** — blend grid colors with background layers.
- **Coordinate lines** — draw lat/lon grid lines on top of the image.
- **Land border** — outline the continents.
- **Land mask / sea mask** — paint solid colors over land or sea pixels.
- **Land shading** — light + shadow shading from elevation data.
- **Sea shading** — light + shadow shading from bathymetry data.
- **Land color / sea color positioning** — order land/sea fills relative to the
  data layer.
- **Missing-value indicator** — pick how `ParamValueMissing` is rendered (color or
  transparent).
- **Linear interpolation** when reprojecting to a different geometry; nearest-neighbor
  when rendering in the native geometry.

## 5. Streamline controls

- **Stream color** — color of the streamline strokes.
- **Step** — sampling step between flow integration points.
- **Min length / max length** — clip streamlines outside this length range.
- **Animation** — animated WebP loop over multiple frames.

## 6. Interactive value lookup

- **Click-to-value** — click anywhere on the Image, Streams, or Streams-animation
  view to read out the grid value at that point. The value is shown in the
  *Grid value* field next to the image.
- **Cross-projection value lookup** — when the selected projection differs from
  the data's geometry, the click is mapped via the projection grid's lat/lon to
  the original data grid (linear interpolation), so the value matches the
  rendered pixel.

## 7. Output & export

- **Image download** — the rendered Image/Streams output is a normal image URL
  and can be saved from the browser.
- **File download** — the original GRIB/NetCDF/QueryData file can be downloaded
  via the Download presentation.
- **Info & Message views** — printable HTML pages for inspection / copying.

## 8. Performance & caching

- **Server-side image cache** — rendered images are hash-keyed and cached on
  disk; repeated views are served from the cache.
- **Configurable cache size** — `imageCache.maxImages` / `minImages` cap the cache.
- **Cache directory** — set by `imageCache.directory` (default `/tmp/`).
- **Thread-safe generation** — concurrent requests for the same image share a
  single render.

## 9. Configuration & administration

- **Hot-reload** of color-map CSV files when they change on disk.
- **Hot-reload** of producer / parameter mapping lists.
- **Static config** (main `.conf`, list of color-map files, blocked projections)
  is read once at start; requires a restart to pick up additions.
- **Config validation** via `make configtest` (libconfig syntax check).

## 10. Integration & deployment

- **Loaded by SmartMet Server** as a shared object (`grid-gui.so`) at startup.
- **Engine dependency**: the grid engine (`smartmet-engine-grid`) must be loaded
  in the same server.
- **Supported input formats** (via grid-files): GRIB1, GRIB2, NetCDF, QueryData.
- **HTTP entry point**: `/grid-gui` on the SmartMet Server port.

---

*Last updated: 2026-06-01.*
