# Design: Inspect numerical raster grid values from the GPlates globe/map

**Status:** Proposal only; no implementation code.  
**Canonical user request:** [GPlates/GPlates issue #25](https://github.com/GPlates/GPlates/issues/25) (12 June 2026).

## Problem statement

The Choose Feature tool (`Ctrl+F`) reports the longitude and latitude beneath the cursor, but not the underlying **numeric value** of a displayed raster/grid. A user looking at bathymetry, topography, age, strain, or other scientific raster data must leave this interactive visual context or export/process the grid elsewhere simply to inspect a cell. Colour at a pixel is not a substitute for its source numerical value, especially when colour mapping, scaling, opacity, or lighting is active.

## Evidence and provenance

* [GPlates/GPlates #25: Extract grid cell values with the Choose feature tool](https://github.com/GPlates/GPlates/issues/25) explicitly requests z-values from displayed grids next to the existing lon/lat readout, either continuously or on click.
* [GPlates raster documentation](https://gplates.sourceforge.net/user-manual/DataFileTypes.html) describes display of numerical floating-point rasters, georeferencing, and reconstructed raster layers.
* [GPlates 2018 technical overview](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2018GC007584) documents raster colouring, dynamic reconstruction and numerical-raster analysis. This proposal is for a lighter-weight **single-position** inspection gesture, not a new numerical-analysis engine.

This is one concrete reported request, not evidence of a widespread user vote. Keep the upstream issue as the canonical discussion and link this design there if its scope is accepted. The issue describes the requirement but contains no implementation/design PR.

## Proposed UX/functionality

1. In the existing Choose Feature tool, preserve normal feature picking. Add a read-only **Grid value** readout beside the current longitude/latitude display when a numerical raster layer is visible beneath the pointer.
2. **Version 1 is click-to-inspect**, to avoid expensive per-mouse-move grid reads. Clicking on the globe/map at a point reports the value at the current reconstruction time and location. A hover/continuous mode may follow if measurement and caching show it is responsive.
3. For a single numerical raster, show `Layer name · band: value [unit]`; omit the unit when the data do not provide one. Use a stable, documented numeric precision rather than interpreting the current palette colour. Show `No data` for nodata cells and `Outside grid` for uncovered points, rather than fabricating 0.
4. Where several visible numerical rasters overlap, show an unobtrusive summary in the status bar (e.g. `3 grid values`) with a small inspector panel listing **every layer and band actually sampled**. Keep layer order deterministic and honour layer visibility; do not silently choose the topmost coloured layer as the only scientific source.
5. An optional `Copy inspected values` action copies coordinates, reconstruction time, source layer/band, numeric value and unit as tabular text. This is a read-only operation and does not edit features, raster data or the project.
6. Interpret the click in the **displayed reconstructed scene**: sample the data corresponding to the displayed raster layer at the current time, including its existing plate reconstruction, anchor/reference frame and georeferencing. Never take the screen pixel's RGB(A) as a scientific numeric value.
7. Preserve the existing Choose Feature selection semantics. If a feature and grid overlap, both the normal feature selection and grid inspection should be available, without one silently disabling the other.

## Scope (v1)

* Desktop GPlates, numerical single- and multi-band raster layers already supported by the application.
* The current globe/map projection and reconstruction time, using existing layer visibility and reconstruction logic.
* Click-to-inspect, a compact readout, an expanded list for multiple overlapping grids and copy functionality.
* Explicit handling of invalid/unavailable values and deferred sampling.

## Non-goals

* No raster import/export change, raster editing, bulk extraction, gridding, or time-series charting.
* No conversion of RGB colour imagery into guessed scientific values; colour-only rasters should say `No numeric band` or be excluded.
* No new reconstruction algorithm or scientific resampling rule. Reuse the current raster reconstruction and sampling behavior, document whether values represent nearest source cells or existing interpolation.
* No change to raster palettes, opacity or lighting, and no automatic downloading of missing rasters.
* No pyGPlates API surface in v1.

## Acceptance criteria

1. Given a small numerical fixture with known corner/centre cell values and a nodata cell, the inspector returns the correct values with expected coordinates, band identity and nodata reporting.
2. For a georeferenced regional raster, locations outside its footprint report `Outside grid`; the antimeridian, poles and 2D/3D projection changes do not produce a false numeric value.
3. A reconstructed numerical raster returns the value that corresponds to the **displayed** scene at two distinct times, including when the reconstruction anchor is nonzero. A nonreconstructed raster follows its existing reference-frame behavior; do not silently add a new reconstruction policy.
4. When two numerical raster layers overlap, both appear with stable names/order, respecting hidden layers and the currently selected band(s).
5. Palette inversion, opacity, colour remapping and surface-relief shading do not change the reported underlying numerical value.
6. Feature picking in Choose Feature continues to work normally; a sample does not modify GPML, the rotation model or project dirty state.
7. A large tiled raster must not freeze the UI: show `Loading…` for deferred reads and cancel/discard obsolete results if the user changes the point, time, or layer visibility. A failed read yields a distinct error, not zero or the last sample.
8. Copying an inspected value produces source-identifiable, machine-readable text with explicit units/time and no invented unit.

## Likely affected components

* Choose Feature tool and globe/map coordinate picking / current status-bar position display.
* Rendered raster layer and existing raster reconstruction/georeferencing path (including multiband numerical data).
* Source numerical grid access or tile cache; asynchronous request/response and cancellation.
* A lightweight Qt inspector view and clipboard integration.
* GUI-level and lower-level fixtures for sampled raster values.

These are logical touch points rather than assertions about class names; confirm the actual interfaces in the current upstream `main` before implementation.

## Risks and open questions

* **Meaning of a sample:** a screen point, raster pixel centre, interpolation point, and coloured display texel can differ. Specify and test the existing sampling semantics, and report them if ambiguous.
* **Reconstruction mapping:** reverse lookup of displayed reconstructed rasters can be ambiguous or discontinuous at plate boundaries. Prefer the same result as the visible reconstruction; explicitly report ambiguity/unavailability rather than quietly sampling an unrelated present-day cell.
* **Performance:** compressed NetCDF/GeoTIFF, GPU tiles, large grids and remote/slow files may make sampling costly. Start with click sampling, throttle/cancel as necessary, and cache conservatively by layer, time, band and location.
* **Scientific metadata:** unit strings, nodata sentinels, value scales and multiple bands may be absent or inconsistent. Never guess physical units or equate colour with value.
* **Multiple layers:** decide whether hidden sublayers count and how to show an inspected band's value if the renderer is currently blending multiple bands.

## Suggested implementation approach

1. Identify and document the existing numerical raster sampling and reconstruction interfaces. Build a read-only `RasterSampleResult` abstraction carrying layer identity, band, location, reconstruction time, value/unit or an explicit status.
2. Implement a sampler using the existing layer mapping/tile cache (not framebuffer readback) and add small synthetic fixtures for georeferencing, nodata, multiple bands and reconstructed/nonreconstructed scenes.
3. Connect an on-demand request to the Choose Feature click path, leaving selection and lon/lat behavior intact. Make delayed results versioned/cancellable against changes of point, time and visible layers.
4. Add the compact readout and expanded inspector/copy action. Ship click-to-inspect before considering continuous hover based on performance measurements.
5. Compare output with independent direct raster cell reads and the GPlates displayed reconstruction for a small suite of known cases.

## Relationship to existing work

* [Upstream issue #25](https://github.com/GPlates/GPlates/issues/25) is the authoritative feature request; this PR provides implementation-ready design detail and does not claim the feature is already available.
* CaliTarheel [PR #4](https://github.com/CaliTarheel/GPlates/pull/4) adds **feature** statistics, not numerical raster sampling.
* CaliTarheel [PR #45](https://github.com/CaliTarheel/GPlates/pull/45) designs area selection; it is not a value probe.
* Upstream [issue #14](https://github.com/GPlates/GPlates/issues/14) concerns an existing nonreconstructed raster/anchor-plate behavior. Grid inspection must expose existing behavior, **not silently attempt to fix that separate issue**.
