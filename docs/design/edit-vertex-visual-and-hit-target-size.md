# Design: Configurable geometry-edit vertex size and click tolerance (no code)

**Status:** Design proposal, no implementation code. Target: CaliTarheel/GPlates.

## Problem and evidence

Ordinary polygon/polyline edit vertices are difficult to see and select reliably. Users report having to click precisely on tiny handles, particularly when removing many points. Drawing a clearer symbol or changing a feature colour is not enough if the *actual edit picking target* remains tiny.

- October 23, 2020: [Setting to change the size of vertices to make them easier to grab](https://discourse.gplates.org/t/setting-to-change-the-size-of-vertices-to-make-them-easier-to-grab/208) asks for a larger selectable vertex.
- August 1, 2026: [Increase the Size of Vertices](https://discourse.gplates.org/t/increase-the-size-of-vertices/3273) distinguishes topology-vertex size controls from ordinary geometry manipulation and reports that the selected-geometry size control does not affect the edit tools. A September 20 reply suggests symbol files/colours, which may improve appearance but do not necessarily enlarge the hit area.

Fork PR #3 (merged October 7, 2026) includes multi-vertex selection and deletion originally developed in PR #5, plus a Split Plate tool. Those make cleanup easier once a vertex can be targeted; they do **not** address handle visibility or picking tolerance.

## Proposed UX

Provide two clearly related but independently configurable screen-space edit-tool preferences:

1. **Edit vertex visual radius** — how large ordinary polyline/polygon geometry-edit handles are drawn (in logical device-independent pixels), with a visible live preview in Preferences and the same effective radius in Move Vertex, Delete Vertex and related manipulation modes.
2. **Edit vertex click radius** — how close a pointer event may be to a vertex to count as a hit (also logical device-independent pixels). Default should not be smaller than the visual handle, unless the user explicitly chooses it.

The settings apply to **edit handles**, not the scientific feature geometry or its symbology. This avoids conflating symbol-file drawing with the control points used in Modify Geometry.

- Distinguish **hover**, **selected**, and **unselected** states using existing application theming/colour conventions rather than inventing different geometry.
- Preserve the current on-screen defaults for existing users unless they change the preference; optionally make a sensible accessible preset available.
- High-DPI/retina screens use device-independent units converted through Qt's display scale; resizing the window or moving it between monitors cannot unexpectedly alter the logical target size.
- When click regions overlap, select the **closest projected vertex** to the cursor; tie-break stably (feature/geometry/vertex order), and show which vertex currently wins on hover. No hidden change to vertex positions.
- In a dense cluster, don't allow an enlarged radius to silently select a completely unrelated, farther vertex when a closer eligible handle exists.
- Different tools may apply eligibility rules (e.g. active edited feature vs topology), but the configured hit radius should be shared consistently by **ordinary geometry manipulation** tools. Existing topology controls remain intact and are not silently reinterpreted.
- Preferences persist at the application level, not in GPML data or the user's project file.

## Scope

Ordinary geometry-editor vertex handle rendering, hit-testing and pointer feedback; preference persistence and application UI; 2D map and 3D globe; logical DPI scaling.

## Non-goals

No modification of coordinates, Feature IDs, topology rules, model reconstruction, feature symbol files, drawing style, or save formats. Not a replacement for lasso/multi-vertex selection in #3/#5; not digitising-time snapping; not mesh simplification, vertex welding or an automatic feature repair tool.

## Acceptance criteria

1. Changing visual radius enlarges edit handles in all supported manipulation tools without moving the underlying feature or modifying project/collection dirty flags.
2. Changing hit radius alone changes which pointer events can select an edit handle, not the rendered feature's geometry.
3. The hit radius matches on-screen logical pixels in map/globe views and behaves consistently under zoom, panning, HiDPI and monitor scale changes.
4. A hover highlight clearly identifies the vertex that will be selected on click.
5. Overlapping eligible hit regions resolve deterministically to the nearest projected handle; stable tie-breaks apply to exact equal distances.
6. The same click in existing/default settings maintains old behavior to the extent compatible with other already-merged editor fixes.
7. The preference persists across application restarts without changing feature or project formats. Existing topology-specific symbol controls still work.
8. Vertex selection, drag, lasso and delete actions from #3/#5 continue to work; a click on a larger handle must not break Shift-lasso detection.
9. Tests cover boundaries of the hit radius, overlapping handles, high DPI and projection extremes. Manual test on both map and globe confirms perceived click targeting.

## Likely affected components

Modify Geometry/Move Vertex/point-delete canvas event handling, nearest-vertex proximity and hit-testing, selected/hover overlay rendering, Qt application preferences (likely QSettings or existing equivalent), tooltip/help, and GUI interaction tests. Determine exact source paths from code inspection during implementation.

## Suggested implementation

Create a small shared `EditVertexHandlePolicy` that supplies logical visual and hit radii for standard geometry-edit tools. Keep feature-space spherical proximity and screen-space pointer hit-testing distinct: the new setting controls a **screen-space handle selection target**, not kilometre-based snapping or geometric proximity. Reuse the existing rendering and picker pathway with configurable tolerances instead of duplicating vertex traversal. Invalidate hover state when the view/projection or tool state changes. Apply settings immediately and persist on confirmation through existing preference infrastructure. Add small geometry-tool tests with injected pointer positions and DPI scale; verify that all changes remain UI-only.

## Risks / open questions

- A larger pointer target can make fine editing of very dense coastlines harder if nearby vertices overlap; hover feedback and nearest-vertex precedence are essential. Should v2 offer keyboard cycling among ambiguous vertices?
- Do canvas tools already define inconsistent screen-space vs angular proximity rules? Normalize edit-handle behavior without accidentally affecting picking of *features* or topological constructs.
- What is the appropriate default radius for accessibility without increasing visual clutter? Preserve the old default, review an optional larger preset.
- How should the hit target behave when a handle lies just beyond the clipped globe silhouette? Never select a vertex that is not visibly editable at the current view.
- Can the existing general selected-geometry size preference be reused or should it remain separate? Avoid an unexpected change to normal Select Feature styling.

## Duplicate check, October 25, 2026

Checked open/closed upstream and fork issues/PRs for vertex handle size, hit target and geometry-edit sizing. Fork PR #3/#5 added bulk vertex editing, not an enlarged visual/click target. No directly equivalent design/implementation PR found. **This document contains no code.**
