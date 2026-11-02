# Design: Diagnose flowline discontinuities and rotation-circuit changes (no code)

**Status:** proposal for upstream consideration; implementation intentionally absent.
**Primary motivation:** a user who reports abandoning approximately five worldbuilding reconstructions after oceanic-plate flowlines disconnected or behaved unpredictably at plate reparenting.

## Problem statement

A flowline can look broken, displaced, or implausibly fast when the feature's left/right plate motion, seed-point frame, sampling/import-time semantics, or an underlying rotation sequence is misunderstood or discontinuous. The graphical result and Kinematics plot do not currently give a user one targeted explanation linking a particular flowline to the plate-pair circuit and offending reconstruction interval. Users then edit rotation text repeatedly, sometimes changing a 0/1 Ma drift correction or moving a parent-plate boundary, without knowing what changed or whether the problem is an input discontinuity or a software defect.

Crucially, **a disconnection does not prove a bad rotation file**. Upstream corrected genuine flowline reconstruction defects in PRs #36 and #103. The proposed feature reports what the current engine and input data say; it must not diagnose all visually odd flowlines as erroneous, nor silently rewrite the input to make them look connected.

## Public user evidence

- **October 27, 2026, GPlates forum:** [Ongoing Problem with Flowlines and Subducting Ocean Crusts](https://discourse.gplates.org/t/ongoing-problem-with-flowlines-and-subducting-ocean-crusts/3286). The user reports recurring disconnected flowlines after splitting a plate from a parent, provides example Plate 801 rotations with an 0/1 Ma parent switch and two entries at 2875 Ma, and points to a suspicious velocity spike. They say five projects were abandoned since 2024. The thread does not provide enough machine-readable data to prove its root cause.
- **March 22, 2026, GPlates forum:** [Odd flowline behavior?](https://discourse.gplates.org/t/odd-flowline-behavior/3229). Removing/re-adding flowline time samples changes the apparent geometry, despite repeated rotation and feature re-creation attempts.
- **February 26–29, 2024, Reddit:** [Need flowline help in GPlates](https://www.reddit.com/r/mapmaking/comments/1b0spvm). A user reports flowline halves far from the rift, and another reports that a 1 Ma rather than 0 Ma rotation workaround fixed their case.
- **December 9, 2023, GPlates forum:** [Random flowline spike?](https://discourse.gplates.org/t/random-flowline-spike/792). A short-lived flowline spike looks like a plate momentarily crossing the globe and returning.
- **Upstream corrections:** [GPlates #36](https://github.com/GPlates/GPlates/pull/36) fixed the reference frame of the right-hand flowline seed; [GPlates #103](https://github.com/GPlates/GPlates/pull/103) fixed geometry import-time/time-sample semantics, disconnected pieces, and related velocities. They demonstrate that some symptoms are engine defects, not user errors.

## Proposed UX

Add **Reconstruction → Diagnose Focused Flowline…** (or a context-menu command, with final placement decided during UX review). The tool is read-only and operates on the focused flowline plus the currently loaded rotation model and selected anchor. An optional interval mode examines the flowline's time samples and user-selected nearby times. A nonmodal findings panel provides:

1. **What feature is being reconstructed:** Feature ID, source collection, left/right Plate IDs, sampled ages, geometry import time when present, and whether legacy flowline semantics apply to this file/build.
2. **What the rotation circuit says:** moving/fixed plate sequence for each side at each sample; the parent plate and interval covering the sample; explicit warning for gaps, missing IDs, ambiguous duplicate timestamp transitions, or a change of parent at the boundary.
3. **What the geometry engine says:** for the current flowline, reconstructed seed/start-point residual between its two halves (geodesic km and angular degrees), plus a diagnostic for the precise time and two sample intervals at which a large displacement first arises. Display observations separately from inferred causes.
4. **What happens at a reparent boundary:** compare the **composed absolute rotations** immediately on either side of the boundary (not merely the raw Euler pole values) and calculate any positional jump on a representative seed/feature point; report a separate finite-difference velocity step if present. Parent changes with continuous absolute motion are normal and must not be flagged as errors.
5. **What can be checked next:** links to Rotation Hierarchy (where available), the focused Feature Properties dialog, the current rotation-sequence editor, and ordinary file reloading/version guidance; an explicit note when the symptom matches a bug addressed by upstream PR #36 or #103 but the running binary cannot be verified to include it.

Results are grouped as **missing/invalid input**, **observed discontinuity**, **unexpected rendered flowline residual**, and **informational/possibly intentional behavior**. Every finding includes Feature ID, Plate IDs, anchored plate, timestamps, units, and the data that justified it. Nonzero speeds alone are not errors. A velocity spike is a clue, not proof of discontinuity. All thresholds must be displayed with their units and can be adjusted for a world whose planetary radius differs from Earth.

A concise summary is copyable as text and exportable as Markdown or JSON, including program version, reconstruction model identifiers where available, feature IDs, test times, and threshold settings; never emit full user geometries into telemetry.

## Scope

- GPlates desktop; read-only diagnostics for one focused **gpml:Flowline** and its actual loaded rotation sequence/circuit.
- Support a selected reconstruction time and optional explicitly bounded interval; use the active anchor and any project planetary radius metadata.
- Older GPML lacking geometry import time must be handled using the actual legacy flowline semantics, not treated as invalid merely for being old.
- The diagnostic must work without the fork-only manager; later adapters can expose results inside fork PR #22 (Flowline Manager) or PR #39 (Worldbuilding audit).
- Give deterministic, testable numerical evidence before making a diagnosis; tolerate ordinary floating-point/rendering uncertainty.

## Non-goals

- No automatic modification of .rot files, pole values, seed points, time samples, left/right Plate IDs, or topology.
- No automatic snapping or physical extension of flowlines to subduction boundaries; flowlines record spreading history, not a guarantee of geographic coincidence with a present-day subduction line.
- No new reconstruction/interpolation mathematics, no unqualified rule that any parent change is bad, and no inference of true tectonic correctness solely from visual coincidence.
- No rewriting of upstream #36/#103, replacing the rotation editor (#6), Rotation Hierarchy (#18), Flowline Manager (#22), boundary-section audit (#34), or broader Worldbuilding audit (#39).
- No automatic batch scanning of all loaded files in v1; the scope is deliberately one focused flowline and a bounded time interval.

## Acceptance criteria and fixtures

1. A normal two-plate flowline (including a non-zero youngest time) has neither a false disconnection warning nor a false circuit-jump finding.
2. A fixture with a deliberate reparent at time T but continuous composed absolute rotations reports the parent change as informational, with near-zero geometric jump.
3. A second fixture changes the parent while leaving the composed absolute motion discontinuous: report exact plates, sample times, angular/geodesic jump, and reference anchor. An intentional jump is not automatically repaired.
4. A seed-frame mismatch fixture reproduces the pre-#36 two-half gap, but on a build with #36 reports near-zero gap. Tests should assert observed distances, not hard-code an obsolete engine diagnosis.
5. Fixtures reproduce #103's old-versus-import-time semantics, including a legacy feature without geometryImportTime and a new feature with it. Changing a modern flowline's sampled times must not be misreported as a rotation discontinuity.
6. Missing rotation coverage, absent Plate IDs, duplicate/ambiguous samples, and zero/one-Ma parent switches produce clear findings with exact provenance; never crash or silently substitute 'valid' identity motion as a conclusion.
7. A screenshot-only report must not be presented as proven; when model inputs are insufficient, say what file or property is missing from the diagnostic.
8. Rendering and model data, dirty flags, undo stack, and save output remain bit-for-bit unaffected by invoking or exporting a report.
9. Repeated runs on identical inputs are deterministic; long time ranges enforce a bounded user-selected sampling budget with warnings against overinterpreting gaps between samples.

## Likely affected components and suggested implementation

- Focused feature selection and reconstructed flowline metadata in the existing GUI/task-panel layer.
- Read-only access to the current **ReconstructionTreeCreator**, rotation-sequence/parentage data, and the existing flowline construction/velocity functions, avoiding a parallel implementation.
- The existing **Rotation Hierarchy** inspection pathway is a natural UI entry point in the fork, though an upstream implementation must not depend on the fork-only dialog.
- A core, GUI-independent **FlowlineDiagnosticPlan** takes a feature, active rotation context, anchor, radius and time samples and produces typed, immutable observations. A thin desktop presentation renders findings. This allows future pyGPlates or audit reuse without UI dependencies.
- Derive discontinuity by computing actual composed rotations just older/younger than the boundary, then apply to a representative point. Do not infer a jump from different Euler-axis parameters or a simple parent-ID change.
- Reuse pre-existing geometry/velocity tolerances and provide explicit km/degree/cm-per-year values and provenance; no new scientific algorithms.

## Risks and questions for review

- **False alarms:** real plate-motion history can have physical velocity changes, deliberately truncated ranges, and legitimate identity rotations. Separate observation from severity and avoid labelling inferred tectonic intent.
- **Definition of 'flowline gap':** calculate it from the engine's underlying reconstructed endpoints rather than pixel distance, with a consistent choice across time-sample boundaries.
- **Time sampling:** continuous misbehavior can lie between user samples; small epsilon around boundaries must respect rotation interpolation, timestep units and floating-point precision.
- **Topology/layer semantics:** an apparent connection to a subduction zone may be physically inappropriate. Avoid implying a flowline is required to intersect a trench.
- **Compatibility:** detecting that a particular binary contains #36/#103 may require an explicit behavior/version check; otherwise describe symptom categories without asserting release status.
- **Performance:** large worldbuilding rotations and multi-Gyr intervals need time/candidate bounds and cached read-only reconstruction trees without stale cached values after edits.
- **Privacy/reproducibility:** exported reports should exclude full geometry/rotation files by default while retaining enough metadata to reproduce findings if the author opts to share model files.

## Relationship to other PRs / duplicate check

Searched current open and closed PRs/issues in **GPlates/GPlates** and **CaliTarheel/GPlates** for flowline diagnostics, rotation-circuit discontinuity, reparent jumps, and velocity spikes before opening this design.

- [CaliTarheel #20](https://github.com/CaliTarheel/GPlates/pull/20) and [upstream #36/#103](https://github.com/GPlates/GPlates/pull/103) fix engine behavior, not model-specific explanations.
- [CaliTarheel #18](https://github.com/CaliTarheel/GPlates/pull/18) shows the current rotation hierarchy; it does not trace a particular flowline's sample-to-sample residual and reparent continuity.
- [CaliTarheel #22](https://github.com/CaliTarheel/GPlates/pull/22) generates and regenerates flowlines from recipes but does not diagnose the physical reason for an existing flowline disconnecting.
- [CaliTarheel #34](https://github.com/CaliTarheel/GPlates/pull/34) audits boundary-section graph connections and missing transforms, not this flowline seed/circuit investigation.
- [CaliTarheel #39](https://github.com/CaliTarheel/GPlates/pull/39) is a broad read-only cross-time Worldbuilding audit; this focused probe can feed it but should not duplicate the report infrastructure.
- [CaliTarheel #46](https://github.com/CaliTarheel/GPlates/pull/46) proposed Plate-ID coverage diagnostics; this design instead examines the **temporal continuity of existing plate pairs and a particular flowline**.

**This PR is design-only**. No implementation, behavior changes, build updates, or scientific output changes are included.
