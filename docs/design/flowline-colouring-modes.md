# Design: Colour reconstructed flowlines by fixed, categorical, or spreading-rate values

**Status:** proposal only. No executable code, API changes, or scientific model data changes.

## Problem / user evidence

- [GPlates upstream issue #24 — Improvement request: Colouring flowlines](https://github.com/GPlates/GPlates/issues/24), opened 29 May 2026, explicitly requests **velocity-based colours**, a **single colour**, and **categorical colours through a user CPT**.
- The [GPlates Flowlines manual](https://www-old.gplates.org/user-manual/Flowlines.html) defines flowlines as paired, half-stage reconstructed tracks with sample times and left/right plate IDs. They are not ordinary static geometries.
- The [GPlately motion-path/flowline notebook](https://gplates.github.io/gplately/latest/notebook-html/09-CreatingMotionPathsAndFlowlines.html) demonstrates the scientific use of flowlines and quantification of their rates; the desired visual comparison is meaningful beyond fictional-world workflows.

Currently the request cannot be satisfied in the desktop flowline display through the requested styles. Users should not need to export tracks, attach derived fields externally, and re-import them just to visually compare a plate pair or its spreading-rate variation.

## Proposed user experience

In the existing layer/draw-style colouring interface, make reconstructed **gpml:Flowline** geometry eligible for a Flowline Colouring section. Options:

1. **Single colour (v1):** one selectable RGBA colour for the complete reconstructed feature, including both halves. This must be supported even if no time samples are valid.
2. **Categorical (v1):** colour by left Plate ID, right Plate ID, or ordered *left/right pair*. Reuse the application's existing categorical/colour-scheme infrastructure. Support user-supplied categorical CPTs where the current colour pipeline supports them; display an explicit unmatched-category/no-data fallback. Preserve left/right ordering for a pair key; do not invent a numeric Plate ID by string concatenation.
3. **Segment half-spreading rate (v1 only after scientific review):** colour each displayed half-flowline segment by its **mean half-spreading rate over that segment's two geological sample times**, not by an unlabelled "velocity." For a segment bounded by samples at times t0 and t1 Ma, compute the length of the reconstructed spherical arc between those two vertices divided by |t1-t0|; report in **mm/yr**, numerically equivalent to km/Myr. Associate the computed value with the *segment*, not the entire feature or the current cursor's time. Both halves use the same definition and may have different values. Draw a continuous palette with labelled units and editable range; provide a no-data style when there are insufficient, invalid, or non-monotone time samples.
4. **Tooltips/legend:** report the active mode, Plate IDs or numerical band, sampling interval for rate values, unit, and any no-data reason; make it clear that the rate is an interval-averaged **half-flowline track speed**, not instantaneous full plate divergence or mantle-frame absolute velocity.

Default style remains the present GPlates appearance until the user chooses another. Switching styles must not alter the flowline feature's properties, times, rotation data, or geometry.

### Scientific-definition gate before implementation

Confirm with upstream maintainers that the geometric arc length / sample-interval definition matches the scientifically intended half-stage flowline convention, including seed points, reconstructed geometry import times, and past/present truncation (see [upstream flowline consistency PR #103](https://github.com/GPlates/GPlates/pull/103)). If it does not, **ship fixed/categorical modes first** and defer the numeric mode; never label a heuristic as a physically defined velocity.

## Scope and non-goals

**Scope:** reconstructed flowline visualisation in globe and map projections; per-layer style configuration and legend; read-only derived colouring; normal redraw on view-time, reconstruction tree, and style changes. A rendered/exported image should use the same colours when supported by the existing renderer.

**Non-goals:** modifying GPML/rotation files; editing flowline geometries or seed points; changing half-stage interpolation; adding a new velocity-field solver; writing new per-segment attributes to GPML; promising that every vector export format can preserve the segmented colours; or using the measured rate as a substitute for the plate-boundary convergence/spreading velocity.

## Acceptance criteria

1. In a fixture with two Flowline features and multiple seed points, a chosen fixed RGBA colour is applied to **both halves**; it survives redraw, zoom, projection switch, and normal layer-style persistence.
2. Categorical colours are deterministic for Plate IDs and ordered ID pairs, can be selected through the existing colouring workflow, and support valid CPT mappings without hard-coded palette choices. Missing mappings have a visible documented fallback.
3. Where rate colouring is enabled, a fixture with known spherical arc lengths and 5/10 Myr intervals shows the expected values and colour bands in mm/yr, separately per half; a changed reconstruction time does not silently reinterpret the geological sample intervals.
4. Zero-length or repeated time intervals, missing samples, unresolvable rotation circuits, and non-finite coordinates produce explicit no-data rendering, not division by zero, infinite rates, or a silently misleading colour.
5. Colouring never changes stored flowline samples, plates, rotations, reconstructed geometry, or GPML on save. Opening the model without the new style reproduces the prior geometry.
6. A normal non-flowline layer retains existing colouring functionality and its behaviour is unchanged. Multiple flowline layers can have independent styles.
7. Interactive performance remains acceptable for realistic densely sampled tracks; colour computations are cached against the rotation model, reconstruction time/geometry revision, and style inputs, and invalidated when they change.
8. Documentation includes a tiny example distinguishing a 20 mm/yr *half-spreading* segment from a 40 mm/yr full spreading rate, and explains that rate interpolation is averaged over time samples.

## Likely affected areas

- Feature draw-style / colouring configuration and categorical/CPT adapter.
- Flowline reconstructed geometry renderer, particularly per-segment colouring (the main new rendering seam).
- Flowline reconstruction sample-time metadata already used for rendering/export; stage/half-stage math and source-data provenance.
- Legend/tooltips, layer-style persistence, cache invalidation, and globe/map drawing tests.
- Optional image export validation; **no** GPML serialization changes.

## Suggested implementation approach

1. Inspect current renderer/style paths and determine why reconstructed flowlines bypass existing feature colouring. Add **fixed** and **categorical** modes by reusing the common style abstractions instead of introducing a second flowline palette system.
2. Write fixtures with distinguishable left/right Plate IDs, a known time sample sequence, and nonzero 0 Ma rotations; verify that both halves are rendered consistently in globe and map views.
3. Write a **pure numerical rate evaluator** over time-stamped rendered segments, with an explicit return type representing valid value (mm/yr) or no-data reason. Review its semantics with upstream before exposing it in the GUI.
4. Cache evaluated segment values and map them to the existing continuous palette infrastructure. Keep the legend's scientific definition and units next to the palette.
5. Add screenshots/regression tests for zoom/view-time/projection changes and ensure display-only styles leave model data untouched.

## Risks and open questions

- **Ambiguous "velocity":** absolute plate speed, full relative spreading rate, and per-half flowline sample rate are distinct scientific quantities. The proposed label and validation gate are deliberate.
- **Time geometry:** upstream #103 changed flowline birth/import-time handling. The evaluator must use the same samples and visible segments as the current renderer, not an obsolete time approximation.
- **Plate-pair symmetry:** categorical ordered pairs distinguish left/right orientation, which may be useful scientifically; add a separate canonical unordered pair option only if requested.
- **CPT compatibility:** confirm that existing GPlates schemes support categorical strings/pairs directly; otherwise use stable labelled category mapping and document the limitation.
- **Segment rendering:** a current one-colour-per-feature path may need a new per-segment colour buffer; avoid slowing all other reconstruction geometries.
- **Export:** some vector formats may not preserve per-segment colours without splitting output features. Treat that as a separate, explicit future decision.

## Prior-work / duplicate audit

- This is design elaboration for [upstream issue #24](https://github.com/GPlates/GPlates/issues/24), **not** a competing issue or an implementation.
- [CaliTarheel #22 Flowline Manager](https://github.com/CaliTarheel/GPlates/pull/22) generates and manages tracks, not their draw-style colouring.
- [CaliTarheel #72 Flowline/rotation diagnostics](https://github.com/CaliTarheel/GPlates/pull/72) explains disconnects and continuity, not colour schemes.
- [CaliTarheel #2 Absolute Age style](https://github.com/CaliTarheel/GPlates/pull/2) handles a different colour dimension.
- Searches of **open and closed** upstream GPlates and CaliTarheel PRs/issues for "flowline colour/color", "CPT" and "velocity colouring" found no direct implementation or overlapping design PR as of 22 November 2026.
