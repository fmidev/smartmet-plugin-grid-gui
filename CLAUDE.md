# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SmartMet Grid-GUI plugin — an admin-only browser-based visualization tool for meteorological grid data (GRIB1/GRIB2/NetCDF/QueryData). It generates server-side HTML with color-mapped grid images, streamline animations, map overlays, and data tables. Originally built as a developer tool for verifying grid support, now published for wider use.

The plugin registers at `/grid-gui` and is loaded dynamically by SmartMet Server as a shared object (`grid-gui.so`).

## Build commands

```bash
make                  # Build grid-gui.so (release)
make debug            # Build with debug flags
make clean            # Remove build artifacts
make install          # Install to $(plugindir)
make rpm              # Build RPM package
make configtest       # Validate cfg/grid-gui-plugin.conf syntax
make format           # Run clang-format on source files
make test             # Run tests (no test/ directory currently exists)
```

## Dependencies

**pkg-config (`REQUIRES`):** `gdal`, `configpp` (libconfig++), `webp` (libwebp)

**SmartMet libraries linked directly:** `grid-files`, `grid-content`, `spine`, `macgyver`

**Runtime engine dependency:** `smartmet-engine-grid` (loaded by server at runtime, not linked)

## Architecture

There are only 4 source files — the entire plugin is ~6K lines of C++.

### Plugin.h / Plugin.cpp (~5,200 lines)

The monolithic plugin class inherits `SmartMetPlugin` from spine. It implements:

- **Page handlers** dispatched by the `page` session parameter:
  - `page_main` — HTML form UI with dropdowns for producer/generation/parameter/level/time selection
  - `page_image` — renders grid data as color-mapped images
  - `page_streams` / `page_streamsAnimation` — streamline/vector field visualization (WebP animation)
  - `page_map` — grid overlaid on geographic map with borders/coordinates
  - `page_info` — metadata about grid content
  - `page_message` — raw GRIB message display
  - `page_download` — GRIB file download
  - `page_table` — tabular grid data
  - `page_coordinates` — coordinate lookups
  - `page_value` — point value queries

- **Session management** — UI state passed via HTTP POST parameters, stored in a `Session` object
- **Image caching** — hash-keyed file cache in `/tmp/` with configurable size limits and thread-safe generation
- **Dynamic config reload** — color map files and producer lists auto-reload on file modification

### ColorMapFile.h / ColorMapFile.cpp (~530 lines)

Manages CSV-based color mapping files that translate grid values to colors. Supports:
- Multiple named color maps per file (e.g., "Dali Temperature (Celcius)")
- Exact lookup (`getColor`) and interpolated lookup (`getSmoothColor`)
- Hot-reload on file modification

### Engine integration

The plugin talks to two server APIs via the grid engine (`Engine::Grid::Engine`):
- **Content Server** — queries metadata: producers, generations, parameters, levels, geometries, projections
- **Data Server** — retrieves actual grid values and file/message data

## Configuration

Main config: `cfg/grid-gui-plugin.conf` (libconfig format with SmartMet `@include`/`@ifdef` extensions)

Key settings:
- `grid-files.configFile` — path to grid-files library config (via `GRID_FILES_LIBRARY_CONFIG_FILE` env var)
- `colorFile` — named color definitions CSV
- `colorMapFiles` — list of value-to-color mapping CSVs in `cfg/colormaps/`
- `blockedProjections` — geometry IDs too large to display
- `imageCache` — directory, maxImages, minImages for cached rendered images

Color map CSVs can be updated at runtime (hot-reloaded), but adding new files to the list requires restart.

## Formatting

`.clang-format` has `DisableFormat: true` and `SortIncludes: false` — auto-formatting is intentionally disabled. Do not reformat existing code.

## CI

CircleCI builds RPMs for RHEL 8 and RHEL 10 using `ci-build deps` / `ci-build rpm`, then uploads to S3.
