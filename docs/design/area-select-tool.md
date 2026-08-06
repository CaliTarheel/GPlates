# Design: Area Select tool

**Status:** design only — no implementation in this PR.
**Target release:** 2.6.0-dev*-SR1a
**Depends on:** [CaliTarheel/GPlates#5](https://github.com/CaliTarheel/GPlates/pull/5) (spherical
lasso, selected-point rendering)
**Consumers:** [#12](https://github.com/CaliTarheel/GPlates/pull/12) bulk Plate ID operations,
[#4](https://github.com/CaliTarheel/GPlates/pull/4) feature statistics

This document exists so the shape of the tool is agreed before any code is written.

---

## The problem

GPlates focuses **one** feature at a time. Every bulk operation the fork has added —
reassigning Plate IDs across a region, retiring subducted crust, auditing a set of boundaries —
currently starts with the user clicking features one by one, or with a filter expressed in terms of
feature properties rather than *where things are on the globe*.

"Everything inside this region" is the natural way to express a worldbuilding edit, and there is no
way to say it.

## The one decision that shapes everything else

**What does the drag select — vertices, or features?**

- **Vertices**, within the geometry currently being edited. *This already exists.* PR #5 added
  Shift-drag spherical lasso selection to the Move Vertex workflow. Building a second vertex-area
  selector would duplicate it.
- **Features**, across everything visible on the canvas. *This does not exist, and cannot exist
  without answering a harder question:* GPlates' selection model is `FeatureFocus`, which holds
  exactly one feature. There is nowhere to put a set.

So the real content of this tool is feature-level selection, and the architectural fork is:

**(a) A first-class selection set.** New model object, decoupled from `FeatureFocus`, persisted in
the session, observable by any tool or dialog. Powerful; invasive; touches focus handling,
rendering, session transcription, and every tool that assumes single focus.

**(b) A transient selection that feeds one operation.** The drag produces a feature list, the tool
immediately hands it to a chosen action (assign Plate ID, delete, statistics, export), and the list
is gone. No persistent state, no change to `FeatureFocus`, no session transcription.

**Recommendation: (b) for v1.** It is a fraction of the work, it carries no risk of destabilising
focus handling across the whole application, and it delivers the actual workflow — the value is in
*doing something* to a region, not in holding a selection. (a) remains open afterwards if a real
need for persistence shows up; nothing in (b) forecloses it.

**Open question 1:** Transient-and-act, or a real selection set?

---

## Shape of the region

PR #5 already samples cursor positions into a great-circle ring and tests containment. Reuse and
generalise that; do not write a second region primitive.

Rectangle is tempting and is the wrong default: a screen-space rectangle on a globe is a spherical
quadrilateral whose meaning changes with rotation and projection, and it degenerates badly near the
poles. In *map* view it is well-behaved. A lasso is well-behaved in both.

**Recommendation:** lasso as the primitive, with a drag-rectangle convenience that constructs a
four-corner spherical polygon in map view only.

**Open question 2:** Is a map-view-only rectangle worth the extra path, or is lasso-only cleaner?

### Known weaknesses to fix while generalising

PR #5's lasso "silently yields an empty selection if the sampled ring is invalid or
self-intersecting." Silent emptiness is indistinguishable from "nothing was in there," and a user
will read it as the tool being broken. Generalising the lasso must include:

- **Reporting** invalid/self-intersecting rings instead of returning empty.
- **Auto-closing** the ring from last sample to first, and de-duplicating near-coincident samples
  before constructing the `PolygonOnSphere` (adjacent identical samples are the usual cause of an
  invalid ring).
- **Back-face rejection on the globe.** A screen-space lasso projects onto both hemispheres.
  Features on the far side, invisible to the user, must not be selected. This needs to be an
  explicit, tested rule, not an accident of the projection code.
- **Antimeridian and polar behaviour**, stated and tested rather than discovered.

---

## Containment

Point-in-polygon on the sphere: `GPlatesMaths::PointInPolygon`, not a new implementation.

For a *feature* — which is a whole geometry, not a point — "inside" needs a rule:

- **Fully contained**: every vertex inside the region.
- **Intersecting**: any part inside.
- **Centroid inside**.

These give very different results for large plates: a lasso drawn inside the Pacific selects nothing
under *fully contained*, and selects the Pacific under *intersecting*.

**Recommendation:** *fully contained* as the default (predictable, safe for destructive follow-up
actions), with *intersecting* available as an explicit modifier or option, clearly labelled.

**Open question 3:** Right default? Modifier key, or a task-panel option so it is visible rather than
remembered?

### What is eligible

Selection operates on what is **visible and reconstructed at the current time** — respecting layer
visibility and the active feature-type filters from [#19](https://github.com/CaliTarheel/GPlates/pull/19)/[#36](https://github.com/CaliTarheel/GPlates/pull/36). Selecting hidden features is
the fastest way to produce a bulk edit the user did not intend and cannot see. Rasters, scalar
fields, and 3D layers are out of scope.

---

## Interaction

Shift-drag is already taken by PR #5's lasso inside Move Vertex. Overloading modifiers across tools
that are both about "drag a region" is how users end up making the wrong edit.

**Recommendation:** a distinct toolbar tool ("Select Area"), so the mode is visible in the UI at all
times and there is no modifier ambiguity. Within it, Shift-drag adds to the pending selection and
Alt-drag removes.

The selected set is drawn highlighted, with a count, and the task panel offers the available actions.
An action is only enabled if it is valid for every selected feature.

---

## v1 must ship with a real consumer

A selection that cannot do anything is untestable and unfalsifiable — it will look like it works.
v1 ships the selector **plus at least one action wired end to end**, with bulk Plate ID assignment
([#12](https://github.com/CaliTarheel/GPlates/pull/12)) the obvious first candidate since that code
already exists and currently lacks a spatial way to choose its inputs.

Given SR0/SR1 experience, both of these are requirements rather than expectations:

- The highlighted selection must be **observed rendering** on the canvas, not merely wired up.
- Any destructive action driven by the selection must have an undo that has been **exercised
  interactively**, not merely compiled.

---

## Where it lives

- Region construction and containment: generalise PR #5's lasso out of `MoveVertexGeometryOperation`
  into a shared `src/view-operations/` region-selection helper, so both vertex and feature selection
  use one implementation.
- Canvas tool: `src/canvas-tools/SelectArea.{h,cc}`, registered alongside the existing tools.
- Containment: `GPlatesMaths::PointInPolygon`, `PolygonOnSphere`.
- Actions: task-panel widget dispatching to existing bulk operations.

## Non-goals

- No persistent or cross-session selection (that is option (a), deferred).
- No attribute-based selection ("all features with Plate ID 801") — separate feature, and the two
  should eventually compose rather than duplicate.
- No selection inside rasters or 3D scalar fields.
- No topology-aware expansion ("select this plate and everything bounding it").

---

## Questions to settle before code

1. Transient-and-act, or a first-class selection set?
2. Lasso only, or lasso plus map-view rectangle?
3. Fully-contained default — modifier or visible option for intersecting?
4. Which action ships in v1 alongside the selector?
5. Should the region itself be savable/reusable (draw once, apply several operations)? That is a
   small addition to (b) and most of the practical value of (a), which may make the persistent
   selection set unnecessary.
