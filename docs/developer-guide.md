# grid-gui developer guide

This guide is for developers who change the `grid-gui` plugin
(`smartmet-plugin-grid-gui`). It explains how the plugin is put together, how a page is
built and rendered, how images are cached, and the pitfalls.

grid-gui is a thin visual front end over the grid engine's Content and Data Servers.
For those, see the grid-engine
[developer guide](https://github.com/fmidev/smartmet-engine-grid/blob/master/docs/developer-guide.md),
the grid-content [developer guide](https://github.com/fmidev/smartmet-library-grid-content/blob/master/docs/developer-guide.md)
and the grid-files [developer guide](https://github.com/fmidev/smartmet-library-grid-files/blob/master/docs/developer-guide.md).
[FEATURES.md](../FEATURES.md) lists what the UI offers.

## Contents

1. [What the plugin does](#1-what-the-plugin-does)
2. [Building and testing](#2-building-and-testing)
3. [Source files](#3-source-files)
4. [Startup](#4-startup)
5. [Request flow and the session string](#5-request-flow-and-the-session-string)
6. [The main page](#6-the-main-page)
7. [Rendering pages](#7-rendering-pages)
8. [Image cache](#8-image-cache)
9. [Colours and colour maps](#9-colours-and-colour-maps)
10. [Configuration](#10-configuration)
11. [Common tasks](#11-common-tasks)
12. [Known pitfalls](#12-known-pitfalls)

---

## 1. What the plugin does

`grid-gui.so` registers the **private** URL `/grid-gui`. It serves a browser UI
for looking at any field in the grid content registry:

* choose producer → generation → parameter → level type → level → forecast type →
  forecast number → geometry → time from drop-downs filled from the Content Server;
* show the field as a colour-mapped **image** (optionally reprojected to another
  geometry), a **map** with land and sea masks, **streamlines** (static or an animated
  WebP), a sample **table** of values or coordinates, the message **info** (all
  attributes), or the raw **message** dump;
* hover to see point values, and **download** the original GRIB message.

It was written as a developer tool for verifying the grid support. It has no public API
and is not used by other code.

## 2. Building and testing

```bash
make               # builds grid-gui.so
make install       # -> $(plugindir)
make rpm
make configtest    # cfgvalidate on cfg/grid-gui-plugin.conf
```

* `REQUIRES = gdal configpp webp`. The plugin links grid-files, grid-content, spine and
  macgyver; the grid engine is resolved at runtime.
* **There are no tests.** `make test` prints "No tests available". Test changes by
  running a server with the grid engine and the plugin and clicking through the
  presentation modes. The engine's test fixture (`smartmet-engine-grid-test`) with the
  GRIB files from `smartmet-test-data` is enough for that.

## 3. Source files

| File | Contents |
|------|----------|
| `grid-gui/Plugin.{h,cpp}` (~5 400 lines) | Everything: configuration, request dispatch, the main page HTML and JavaScript, the page handlers, image rendering, the image cache. `ImagePaintParameters` (in `Plugin.h`) bundles the rendering options for one image. |
| `grid-gui/ColorMapFile.{h,cpp}` | `T::ColorMapFile`: loads named value → colour tables from CSV and reloads them when the file changes. |
| `cfg/grid-gui-plugin.conf` | Sample configuration. |
| `cfg/colors.csv`, `cfg/colors2.csv` | Named colours for the colour drop-downs. |
| `cfg/colormaps/*.csv` | Colour maps. |

The session class (`Session`) comes from grid-files (`common/Session.h`).

## 4. Startup

**Constructor**

1. It registers `/grid-gui` with `addPrivateContentHandler()`. A **private** handler is
   only left out of the server's URI list, which the frontends use for routing, so it
   cannot be reached through a frontend. It is **not** access-restricted: anyone who can
   connect to the backend's own port can call it. Restrict it with
   `plugins.grid-gui.ip_filters` in the server configuration (see the spine developer
   guide, §7).
2. It reads the configuration. `grid-files.configFile`, `colorMapFiles`, `colorFile`,
   `animationEnabled` and the three `imageCache.*` keys are mandatory.
3. It calls `Identification::gridDef.init()` and `Map::topography.init()` with its
   `grid-files.configFile`. `gridDef.init()` does nothing if the grid engine already
   initialised it, which is the normal case (see [§12](#12-known-pitfalls)). The
   topography provides the land and sea masks and shading.
4. It loads the colour maps and colours, and **deletes every
   `grid-gui-image_*` file in the image cache directory**.

**`init()`** gets the grid engine and takes the producer file name from it
(`getProducerFileName()`). The producer file limits which producers appear in the UI.

## 5. Request flow and the session string

grid-gui keeps **no server-side session**. The whole UI state is a string of short
`key=value;` pairs that travels in the `session` URL parameter:

```
/grid-gui?session=pg=main;pi=12;g=345;p=T-K;lt=2;l=850;pre=Image;cm=None;...;&p=RH-PRCNT
```

`request()` does this:

1. It builds a `Session` from `session=`, or with the defaults from `initSession()` if
   the parameter is missing.
2. For every other URL parameter that **is already a session attribute**, it updates
   the value and keeps the previous value under `#name`. The page code checks
   `session.findAttribute("#", name)` to see which selector the user changed, and
   resets the selectors that depend on it (a new producer clears the generation, the
   parameter, …).
3. If the grid engine is disabled, it returns a short HTML notice.
4. It dispatches on the `pg` attribute (`ATTR_PAGE`): `main`, `image`, `streams`,
   `streamsAnimation`, `map`, `info`, `message`, `download`, `table`, `coordinates`,
   `value`.
5. It sets `Cache-Control: public, max-age=N`: 600 s for images, maps, streams, tables
   and coordinates (their URL fully identifies the content), 1 s for the rest.

`requestHandler()` adds `Access-Control-Allow-Origin: *` and turns exceptions into
`400 Bad Request`.

The attribute names are the `ATTR_*` macros at the top of `Plugin.cpp`, for example
`pi` (producer id), `g` (generation), `p` (parameter), `lt` / `l` (level type and
level), `ft` / `fn` (forecast type and number), `gm` (geometry), `pro` (projection),
`t` (time), `f` / `m` (file id, message index), `pre` (presentation), `cm` (colour
map), and `hu`, `sa`, `bl`, `op` (hue, saturation, blur, opacity).

## 6. The main page

`page_main()` (about 1 700 lines) builds a two-part HTML page: the selectors on the
left, and the presentation on the right.

1. **Selectors.** Each drop-down is filled from the Content Server through the engine
   (`getContentServer_sptr()`):
   * producers: `getProducerInfoList()`, limited to the producer file's list;
   * generations: `getGenerationInfoListByProducerId()`, grouped by the time group type
     (`tgt`: All, Day, Month or Year);
   * parameters: `getContentParamKeyListByGenerationId()`;
   * then `getContentListByParameterAndGenerationId()`, from which `getLevelIds()`,
     `getLevels()`, `getForecastTypes()`, `getForecastNumbers()` and `getGeometries()`
     extract the remaining choices in turn;
   * projections: all geometries except the `blockedProjections`, unless one is
     already selected.

   Each `<SELECT>` has an `onchange` handler that reloads the page with the current
   session string plus the changed attribute.
2. **Selected field.** The chosen time picks a content record, and its `f` and `m`
   (file id and message index) identify the field for every rendering page.
3. **Presentation.** Depending on `pre`, the right side contains an `<IMG>` whose URL is
   the same session with `pg=image`, `pg=map`, `pg=streams` or `pg=streamsAnimation`, or
   an `<IFRAME>` for `info`, `table`, `coordinates` or `message`. The rendering options
   (colour map, hue, saturation, blur, land and sea colours and shading, coordinate
   lines, stream parameters) are selectors below it.
4. **JavaScript** (`page_main_writeJavascript()`): the mouse handlers call
   `pg=value` with the pixel position through XHR to show the value under the cursor;
   other helpers switch images and pages.

## 7. Rendering pages

| Page | Data calls (through the engine's Data Server) | Output |
|------|---------------------------------------------|--------|
| `image` | `getGridData()` for the native grid; with a projection, `getGridValueVectorByGeometry()` with `grid.geometryId`, plus `gridDef` coordinates for the target geometry | PNG, or animated WebP when animation is on |
| `map` | `getGridData()` | PNG with land and sea masks from `Map::topography` |
| `streams`, `streamsAnimation` | the same as `image` | PNG of streamlines, or an animated WebP of moving particles (`webp_anim_save`) |
| `value` | `getGridValueByPoint()` | plain text for the hover display |
| `table`, `coordinates` | `getGridValueVectorByRectangle()`, `getGridCoordinates()` | HTML table of a sample area |
| `info` | `getGridAttributeList()` and the content, file, generation and producer records | HTML |
| `message` | `getGridMessageBytes()` | HTML dump |
| `download` | `getGridMessageBytes()` | the message bytes as a file download |

`saveImage()` does the actual painting. It maps each value to a colour (the colour map
with `getSmoothColor()`, or a hue and saturation scale computed from the value range
when no colour map is chosen), then applies the land and sea colouring and shading in
the configured layer order, the land borders and coordinate lines (`ImagePaint`), the
blur and the opacity. It writes the PNG or WebP to the image cache directory.

## 8. Image cache

Rendering is expensive, so `image`, `streams` and `streamsAnimation` cache their output
as files:

1. The page builds a **key** from everything that affects the picture: file id, message
   index, projection, all the colour and shading options, the colour map name **and its
   modification time**, and so on. The hash of the key is sent as the `ETag`, and a
   matching `If-None-Match` gets a 304.
2. If `itsImages` (key → file name) has the key, it serves that file.
3. If another request is already rendering the same key (the key is in
   `itsImagesUnderConstruction`, a ring of 100 slots), it waits (up to 30 s) and checks
   again.
4. Otherwise it renders to `grid-gui-image_<time>.png` in `imageCache.directory`,
   serves it, and records the key.
5. `checkImageCache()` removes the oldest files once there are more than
   `imageCache.maxImages`, down to `imageCache.minImages`.

The cache is per process and is emptied on startup. When you add a rendering option,
**add it to the key**, or users get stale images for the old setting.

## 9. Colours and colour maps

* **Colour file** (`colorFile`): `name;RRGGBB` or `name;AARRGGBB` hex lines that give the named colours for
  the land, sea, border, coordinate-line and stream colour selectors. It is reloaded
  when its modification time changes.
* **Colour map files** (`colorMapFiles`): each file holds one or more maps. A map starts
  with a `NAME,<map name>,` line, followed by `value;A,R,G,B` (or `value;R,G,B`, or
  `value;#RRGGBB`) lines in ascending value order:

  ```
  NAME,Dali Precipitation (0..10),;
  0;0,10,155,255
  0.1;255,10,155,255
  1.0;255,240,240,20
  ```

  `getSmoothColor()` interpolates between the entries, and `getColor()` takes the entry
  at or below the value. Existing files are reloaded when they change (each has its own
  modification lock), but a new file in the list needs a restart.

## 10. Configuration

All keys are under `smartmet.plugin.grid-gui`:

| Key | Meaning |
|-----|---------|
| `grid-files.configFile` | grid-files configuration. In practice the engine's is used; see [§12](#12-known-pitfalls). |
| `colorFile` | Named colours. |
| `colorMapFiles` | List of colour map files. |
| `animationEnabled` | Offer the animated WebP variants. |
| `imageCache.directory`, `.maxImages`, `.minImages` | Rendered image cache. |
| `blockedProjections` | Geometry ids that are too large to offer as reprojection targets. |

The producer list comes from the grid engine's producer file, not from this
configuration.

## 11. Common tasks

### 11.1 Adding a UI option

1. Add an `ATTR_…` name (keep it short; it goes into every URL) and a default in
   `initSession()`.
2. Add the selector in `page_main()`. Copy an existing `<SELECT>` so that its
   `onchange` carries the session string.
3. Read the attribute in the rendering page, pass it through `ImagePaintParameters`,
   and use it in `saveImage()`.
4. **Add it to the image cache key** in every page that caches ([§8](#8-image-cache)).

### 11.2 Adding a presentation mode

Add the name to the `modes[]` list in `page_main()`, a branch where `page_main()`
builds the right-hand side, a `page_xxx()` handler, and a dispatch case in `request()`
with a suitable `expires_seconds`.

## 12. Known pitfalls

* **`grid-files.configFile` is effectively ignored.** `Identification::gridDef.init()`
  returns at once if it has already been initialised, and the grid engine (loaded
  before plugins) always does that first. The plugin's setting only matters if its file
  differs from the engine's, and then it silently has no effect.
* **Request values are not escaped on output.** Session attributes are written into the
  generated HTML and JavaScript without escaping or URL encoding. `request()` therefore
  rejects (400) any parameter name or value containing `` < > " ' ` \ & `` or control
  characters before it reaches the session (`isSafeRequestValue()`). Keep that check in
  place, and escape values yourself if you add output of data that does not come through
  it. The page has no authentication of its own, so restrict `/grid-gui` with
  `plugins.grid-gui.ip_filters`: being a private handler only hides it from the
  frontends.
* **`itsImagesUnderConstruction` is used without the lock.** The slot scan and the slot
  writes in the image pages happen outside `itsThreadLock`, so concurrent requests race
  on those `std::string`s. The worst outcome is a duplicate render, but it is still a
  data race.
* **Image files are deleted at startup.** Every `grid-gui-image_*` file in
  `imageCache.directory` is removed when the plugin starts. Do not point it at a
  directory that another server instance also uses.
* **Large geometries.** Reprojecting to a very large geometry allocates the whole target
  grid for every request; that is what `blockedProjections` is for.
