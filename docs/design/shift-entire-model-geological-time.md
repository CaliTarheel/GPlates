# Design: Shift an entire GPlates model in geological time (no code)

**Status:** Design proposal only. No implementation code. The reference-frame semantics need explicit technical review before implementation.

## Problem and evidence

A user with a model covering 0–850 Ma wants to translate its entire history backward by **+150 Myr**, so the old 0 Ma configuration is at 150 Ma and the old 850 Ma configuration is at 1000 Ma. Current forum guidance recommends manually editing rotation and feature times via a long sequence of find/replace operations, which is brittle and could modify unrelated numerical attributes.

- September 16–20, 2026: [Adjusting Timescale of Simulation Backwards?](https://discourse.gplates.org/t/adjusting-timescale-of-simulation-backwards/3278). User explicitly requests **old 0 → new 150 Ma**; the proposed workaround is external text replacement.
- February 2024: [External GPlates date-shifting script](https://gist.github.com/hersfeldtn/812ba0ae4a02cae519eb4479a6ca287a) demonstrates related demand, but its policy preserves 0.0; that is **not** equivalent to this user's requested semantics.
- Existing fork PRs #6–#8 edit rotation sequences, #64 addresses individual feature properties, and #58 navigates timestamps. None translates an entire model's temporal datum.

## Proposed UX

**Model > Shift Geological Time…** (precise menu placement subject to review).

1. Select a signed offset `Δt` in Ma. Explicitly display the mapping: **new age = old age + Δt**. For the reported use case, `Δt = +150` changes old 0 Ma to new 150 Ma and old 850 Ma to 1000 Ma.
2. Choose a clearly defined scope: selected **loaded editable feature collections and rotation collections** which together define one reconstruction model. Show collections excluded from the plan; warn if an external dependency makes the operation incomplete. No unloaded files are edited.
3. Run a mandatory **dry-run**. Summarize before/after min/max finite ages, counts of changed values by type (feature valid/begin/end time; finite-rotation sample; geometry import time; time-sampled values, when supported), missing/read-only collections, time-boundary violations, and unsupported temporal-property representations.
4. **Review reference-frame consequences before Apply.** GPlates stores geometries in a present-day reference system and often relies on a rotation anchor/identity sample at 0 Ma. Simply adding a constant to textual sample times may NOT preserve reconstructed geometry after shifting the time axis. The plan must show which rotation hierarchy anchors, geometry import times, and any implicit present-day conventions need special treatment. If exact equivalence cannot be demonstrated for the selected model, v1 must **refuse Apply** with an actionable diagnostic; never silently produce a superficially plausible but altered animation.
5. After validation, Apply is one atomic undoable transaction. Loaded collections are marked dirty, and the user saves through the normal GPlates workflow. Companion view/animation times and Primary Project Document timestamp schedules may be offered as **separate explicit checkboxes**.

The tool must distinguish a **time-axis translation** from rescaling geological rates or changing pole angles.

## Semantic requirements

- Transform supported **typed finite geological times**, not matching numbers in raw text. Do not alter Plate IDs, coordinates, angles, rates, ordinary numeric attributes, dates embedded in descriptions, or comments.
- Retain sentinel values for distant past/future or unsupported infinity conventions; preflight reports each case.
- Preserve the ordering and intervals between finite sample times: `(t2 + Δt) - (t1 + Δt) == t2 - t1`.
- Do **not** silently keep old 0 Ma fixed: that would violate the motivating case. A model-internal reference at geological 0 Ma is a distinct concern from a *stored event* timestamp of 0 Ma; any necessary anchor-row insertion or geometric re-referencing must be visible in the plan, scientifically justified, and explicitly confirmed.
- Reject or explicitly exclude time-dependent properties that cannot be safely translated as typed model values.
- Avoid negative-age support in v1 unless the underlying reconstruction code and temporal constraints are proven to support it; preflight must reject out-of-domain offsets.

## Scope

Selected loaded editable model collections and all supported *typed* temporal properties required for a consistent transformation, including rotation samples, feature validity, and explicit geometry-import ages. Preserve reconstruction topology and geometry within the stated equivalence tolerance if feasible. Time-dependent rasters and external model assets only when safely supported and included in the plan; otherwise block or exclude with an explicit dependency warning.

## Non-goals

- No time stretching/scaling, per-plate independent shifts, inferred dates from arbitrary text, point-by-point pole interpolation redesign, future/negative times, direct editing of unloaded files, or silent changes to project-specific notes.
- No automatic changing of a file's scientific provenance or claiming the shifted history is a historically valid Earth reconstruction.
- No promise that adding an offset to rotation times alone is sufficient; any required present-day reference-frame treatment must pass the validation contract first.

## Acceptance criteria

1. On a synthetic multi-plate model with fully supported typed times and a verified reference-frame construction, every reconstructed test geometry at the old time `t` agrees with the shifted model at `t + Δt` to defined spherical distance/angle tolerances. Verify at sample times and between samples, not only at keyframes.
2. All finite feature ages, geometry-import ages, and rotation sample times map according to the displayed affine translation. Distant-past/future sentinels and unrelated numeric values remain unchanged.
3. Zero-age records **that represent real model events** shift with other finite times (old 0 → 150 in the example). Implicit 0-Ma reference-frame rules are handled explicitly and tested, not mistaken for ordinary temporal events.
4. Unsupported/read-only collections, missing dependencies, invalid anchor transitions, out-of-domain times or unverifiable equivalence prevent silent modification; affected items are reported.
5. Previewing or cancelling does not mutate anything. A successful Apply is one undo/redo transaction, and one Undo restores the exact source collections.
6. Normal save/reload preserves both the new timestamps and equivalence tests; source files on disk remain unchanged until ordinary Save.
7. Time intervals between adjacent finite rotation poles and valid-time endpoints are unchanged.
8. Two-column temporal configurations, interval lists, temporally sampled properties, and cross-collection rotation hierarchies receive dedicated fixtures where supported.

## Likely affected components

Typed GPML feature-property visitors, finite rotation sequences/reference-frame reconstruction model, geometry import-time handling, loaded collection registry and dirty state, model-level grouped undo commands, Primary Project Document timestamp integration, preflight UI, and reconstruction-equivalence tests. Confirm exact code paths during implementation review.

## Suggested implementation strategy

1. **Feasibility/semantics spike first, no UI:** construct a two-plate controlled case with non-identity rotations and non-zero geometry-import times; shift all typed ages and compare reconstructions at old `t` and new `t + Δt`. Identify any required explicit 0-Ma anchor or re-referencing operation.
2. **Pure planning pass:** traverse selected loaded typed values and build a `ModelTimeShiftPlan` of proposed replacements, anchor/reference-frame transformations, unsupported cases, and validation fixtures. No mutation.
3. **Reference-frame-aware validation:** compare sampled reconstruction transforms for each affected plate/time, including inter-sample interpolation; reject ambiguous and externally incomplete models.
4. **Atomic commit:** apply validated replacements via existing typed property change and rotation writers in a single grouped undo; do not edit GPML or rotation text by regular-expression substitution.
5. **Post-apply verification:** run geometry/rotation invariance checks, record diagnostics, and keep normal save operations separate.

## Risks and open questions

- **Most important:** does a fully general `old t → new t+Δ` transformation exist without modifying geometry coordinates when the reconstruction assumes present-day 0 Ma? What additional anchor rows or transforms are necessary?
- Which finite time objects are ordinary historical data and which are internal reference-frame constraints?
- Can resolved topologies, dynamic networks and motion paths be proven equivalent over the whole shifted interval?
- Are unloaded dependencies common enough that v1 should require all model collections to be present?
- How should negative offsets be handled when any event crosses 0 Ma?
- How should time-dependent raster snapshots and project export profiles be reconciled?

## Duplicate check (October 25, 2026)

Searched open/closed CaliTarheel and upstream GPlates PRs/issues for retiming, model time shift, time translation and offset. No equivalent model-wide design/implementation PR found. Individual feature edit and rotation editors are adjacent, not duplicates. **No code.**
