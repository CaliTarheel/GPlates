# Design: subduction boundary classifier (L1)

**Status:** design only — no implementation.
**Target release:** not yet assigned.
**Related:** D9 kinematics-aware Ridge-Transform Generator (`Deric.md`) — this is the convergent-side
counterpart and deliberately mirrors its staging; `SubductionCutterOperation`;
`src/app-logic/PlateBoundaryStats.h`; `RenderedSubductionTeethPolyline.h`.

This document exists so the shape of the feature is agreed before any code is written.

---

## The problem

Adding a subduction zone to an existing project is currently a drawing exercise with no feedback.
Nothing in GPlates tells the user:

- whether the plate motions they have already set actually produce convergence along the line they
  are about to draw on;
- which of the two plates should be overriding;
- whether the shape they have drawn is a shape a descending slab could produce.

All three questions are answerable from data already in the model. There is no way to get the
answers out.

The consequence is that a subduction zone is drawn on intuition and then, if it is wrong, stays
wrong silently. A trench drawn across a boundary that is diverging is not flagged. A trench drawn
with its teeth on the buoyant side is not flagged. A trench drawn as one long straight line — a
shape no slab can make — is not flagged. These are not exotic errors; they are the normal result of
drawing a 3D object as a 2D line without a readout.

**GPlates already computes the hard part.** `calculate_plate_boundary_stats()` returns, for sample
points along every shared sub-segment of a resolved boundary: the left and right plate velocities,
each one's obliquity relative to the boundary normal, its orthogonal and parallel components, and
the velocity of the boundary itself. Convergence rate, strike-slip partitioning and trench migration
are all in there.

It has exactly one consumer: `api/PyTopologicalSnapshot.cc`. There is no GUI path to any of it. The
classifier described here is, for its first half, a window attached to numbers that already exist.

## What the feature is actually for

It is a **readout, not an automation.** The user still places the boundary, still nucleates, still
decides polarity. What changes is that the consequences of their rotations become visible while they
work instead of inferable afterwards.

This distinction decides the scope below, and it explains the one deliberate difference from D9:

> **D9 generates geometry. This measures it.**

On the spreading side it is reasonable to rebuild a hand-drawn line into kinematically exact ridge
and transform legs, because the correct answer is fully determined by the relative pole. On the
convergent side the correct answer is not determined — dip depends on age, thermal history and
mantle flow, and the source data here is hand-made and known to be imperfect. So v1 reports
deviation and never moves a vertex. The drawn line stays authoritative.

## We are working forward, and that changes what is knowable

This project evolves plate motion **forward** from a canonical pre-Pangaea configuration. That is
the opposite of terrestrial practice, and it removes a constraint that dominates the real-world
literature.

On Earth, past trenches are unknowable because subduction destroys its own record — the crust that
would prove a trench existed has been consumed, which is why the seafloor record stops around 180 Ma
and why pre-Pangaea reconstructions are argued about rather than computed. **None of that applies
here.** The trench is declared, and the crust record is generated from it. There is no inverse
problem.

Three consequences for this design:

1. **Crust age on the subducting side is exact**, because the ocean-crust generator produced it.
   Age is not sampled from an uncertain grid; it is read from the model's own output. This largely
   settles the age-source question below and makes the cusp work considerably more reliable than the
   equivalent exercise on real data would be.
2. **Cusps can be predicted, not just detected.** Because both the crust geometry and the plate
   motions are known ahead of time, the kernel can answer *when* a given fracture zone will reach
   the trench and where the resulting cusp will fall. This is a forward-only capability with no
   terrestrial equivalent, and it is probably the most interesting thing the kernel can do.
3. **Consistency matters more, not less.** Working backward, an implausible trench is just a guess.
   Working forward, it seeds every subsequent timestep — the crust it consumes and the crust it fails
   to consume both propagate. Warnings are worth more here than they would be in a reconstruction
   tool.

The measure-don't-move rule below still stands, for a different reason: the source geometry is
hand-made art and the user's line is the intent. But the case for surfacing deviation loudly is
stronger than it would be on Earth data.

## The hard dependency, stated first because it can sink the feature

`calculate_plate_boundary_stats()` takes `resolved_topological_sections`,
`resolved_topological_boundaries` and `resolved_topological_networks`. It operates on **resolved
topological plate polygons**. If the project's plates are ordinary closed polygons rather than
topological ones, it returns nothing, and layer 1 has no input at all.

**This has now been checked against Aesin, and the answer is that it has no topologies at all.**
Every `.gpml` in `Artifexia/Aesin/Aesin/` was inventoried: 280 `OceanicCrust`, 233
`UnclassifiedFeature`, 74 `MidOceanRidge`, 26 `ContinentalRift`, 24 `SubductionZone`, 6
`OrogenicBelt`, 4 `IslandArc`, 4 `Raster`. Not one topological feature of any kind. The 48
subduction-zone geometries are plain `gpml:centerLineOf` polylines carrying a
`gpml:reconstructionPlateId` — exactly the hand-made, non-topological case.

So outcome 2 below is not a contingency, it is the feature. Layer 1 must compute relative velocity
from a boundary line plus two plate IDs, without resolved topologies, or it cannot run on the world
it is being built for. `PlateBoundaryStats` remains a useful reference for the velocity
decomposition, and its formulas carry over directly, but it cannot be the entry point.

**And the plate pair is already the established pattern in this very project.** `MORs.gpml` carries
`gpml:leftPlate` and `gpml:rightPlate` on all 65 of its ridge features. The trench features do not —
they carry `gpml:reconstructionPlateId` only. So the gap is not topology. It is two missing
properties on one feature class, properties GPlates already understands and this project already
uses next door.

That makes L1's real input list short, and topology-free:

| What L1 needs | Where it comes from |
|---|---|
| Boundary geometry | `gpml:centerLineOf` on the trench — present |
| The two plates either side | `gpml:leftPlate` / `gpml:rightPlate` — **missing on trenches** |
| Which side is overriding | The user's step-1 selection |
| Trench's own motion, for rollback | `gpml:reconstructionPlateId` on the trench — present |

Everything except the plate pair is already in the data. Adding `leftPlate`/`rightPlate` to trench
features is therefore the entire prerequisite for L1 — and note that the step-1 overriding-plate
selection *is* that assignment, made persistent. The selection and the stored property are the same
act, so the tool can write what the user chooses rather than asking twice.

Resolved topological polygons stay desirable for other reasons — plate area, velocity fields, the
ocean lifecycle work — but they are not on L1's critical path.

The original three outcomes, kept for the record:

1. The target data is topological, or can be made so. Build as described.
2. It is not, and the classifier grows a fallback path that computes relative velocity directly from
   a boundary line plus two plate IDs, without resolved topologies. This is a real amount of extra
   work — no shared sub-segments, so no ready-made notion of "the boundary between A and B" — and it
   should be a decision, not a discovery made halfway through.
3. It is not, and the feature requires topological input. Acceptable, but then that requirement is
   the headline of the release notes, not a footnote.

**Nothing else in this document matters until this is checked.**

## The one decision that shapes everything else

**Is a subduction zone one polyline, or a chain of segments joined at cusps?**

They are different features, not variations.

*One polyline* is what GPlates does today. It is simple, it matches `gpml:SubductionZone` as it
stands, and it makes cusps purely cosmetic — a kink in a line, with nothing behind it.

*A chain of segments* gives each segment somewhere to carry its own dip, width and rollback. Those
three numbers are the compact surface proxy for the 3D slab, and from them the arc's curvature,
the arc-trench gap, and whether a back-arc basin opens all follow. Cusps stop being decoration and
become what they physically are: the joins where dip changes along strike.

**This document assumes the chain**, because without it the cusp work has nowhere to live and the
feature reduces to colouring existing lines. But it is the decision to make first, and it is the one
with real data-model cost.

The compromise that probably wins for v1: **do not invent new GPML.** Keep the stored geometry a
single polyline, and have the kernel return the segmentation as a computed result — a list of
index ranges into the line, each with its own attributes — which the window displays and which the
user can accept as a set of separate features if they want them. Segmentation as an output, not yet
as a storage format.

## The workflow

1. **Select the overriding plate.** This is step one, and it is doing more work than it appears to.
   It fixes the reference frame in which trench migration is meaningful, which is what makes the
   Mariana-type / Andean-type distinction computable at all. It also resolves the left/right
   ambiguity already present in `PlateBoundaryStat` — left and right are defined by the digitizing
   direction of the shared sub-segment and carry no tectonic meaning until someone says which side
   is overriding.

2. **The window shows that plate's own boundaries, classified.** Convergent stretches highlighted
   with rate and obliquity; divergent and transform greyed. If no boundary of the selected plate is
   converging at the current reconstruction time, the window says exactly that, and says it as the
   primary message rather than showing an empty list. This is the single most valuable failure to
   catch and it is currently invisible.

3. **The user picks a convergent stretch.** The subducting plate is implied by whatever is on the
   other side. One selection instead of two, and it is impossible to pick a pair that do not
   actually share a boundary.

4. **The window reports the segmentation and the warnings.** Polarity check, obliquity check, arc
   geometry deviation, cusp positions. Everything is advisory; see L2's override.

The window is modeless and stays open, showing current state and the next step.

## Cusp segmentation — the part that is new

### Why cusps exist

Trench curvature is set by slab dip. Frank's construction: a spherical shell bent through dip δ
traces a small circle of angular radius δ/2, concave toward the overriding plate — so ~45° dip gives
a radius of roughly 2400 km. Dip in turn is set largely by the age of the incoming plate, because
older lithosphere is colder and denser and hangs steeper.

Therefore **a cusp is where the age of the subducting plate changes abruptly along strike** — which
is to say, where a fracture zone enters the trench. Older and steeper on one side, younger and
shallower on the other, two different arc radii, and a corner where they meet. Beneath a large cusp
the slab is generally torn rather than folded.

This is a satisfying result for the tool, because the same fracture zones that make the best
nucleation sites are also what segments a trench once it exists, and the age data needed to find
them is data the project already has.

### Detection

For each sample point along the selected convergent stretch:

1. Take the crust age on the **subducting** side. (Source is an open question below.)
2. Map age to an implied dip through a monotonic curve. This must be exposed and tunable, and
   documented as a heuristic — it is a rule of thumb, not a solved relation.
3. Map dip to an implied arc radius, `R_earth · sin(dip/2)`.

Then segment: group consecutive samples into runs of near-constant implied dip; the joins between
runs are the cusp candidates. Fit each run to a small circle of its implied radius, concave toward
the overriding plate, and report the drawn line's maximum deviation from that fit.

### Prediction, not just detection

Because the model runs forward, the same machinery answers a question no terrestrial tool can. Given
the crust geometry ahead of the trench and the relative motion, the kernel can project *when* each
fracture zone in the incoming plate arrives at the trench, and where along strike the resulting cusp
will fall.

That turns cusp handling from bookkeeping into a planning aid: the user can see that a segment
boundary is due in some tens of millions of years and decide whether to let it happen. Worth
designing the kernel's outputs so this falls out naturally — it is the same age-along-strike
calculation evaluated at a future time rather than the present one.

### What the kernel says, and does not do

It reports, per segment: implied dip, implied radius, actual fitted radius, maximum deviation,
convergence rate and obliquity, along-strike length. Per cusp: position, dip change across it, and
whether the change is large enough that the slab beneath is more likely torn than folded.

It moves nothing. A deviation warning on hand-made geometry is information; a silent re-fit of
someone's coastline-following trench is vandalism.

### Warnings worth emitting

- **Curvature of the wrong sign** — concave *away* from the overriding plate. This requires the slab
  to stretch and is the clearest single indicator that polarity is backwards.
- **A long stretch with no curvature at all.** Straight trenches of thousands of km are not a thing.
- **Obliquity above ~30–40°.** Past that, nature partitions the motion into a trench taking the
  normal component plus a trench-parallel strike-slip fault in the fore-arc. This is a
  transpressional boundary, not a clean subduction zone, and drawing teeth on it is wrong.
- **Segment length as a curvature expectation.** Narrow systems (under roughly 1500 km) let mantle
  flow around their edges, roll back fast and curl tightly; wide ones are anchored and stay
  comparatively straight. A short trench drawn straight and a very long one drawn tightly hooked are
  both suspect.

## PR A: the geometry kernel

`SubductionArcGeometry.h/.cc`, built and tested independently of any UI, mirroring D9-A.

**Inputs:** the resolved shared sub-segment (or a polyline plus two plate IDs on the fallback path),
which side is overriding, reconstruction time, subducting-plate age sampled along the line, the
age→dip curve, the cusp dip-change threshold, and sample spacing.

**Outputs:** per-segment attributes as listed above, cusp positions with confidence, the full warning
list with each warning naming its case, and a clean failure reason when the input cannot be
classified at all.

**Algorithm:**

1. Validate: plate pair resolves, relative rotation is non-degenerate, boundary is not too close to
   the Euler pole, age data is available along the line.
2. Sample the boundary at uniform spacing, reusing the existing `uniform_point_spacing` machinery.
3. Compute per-point kinematics from `PlateBoundaryStat`; map left/right to overriding/subducting
   using the user's selection.
4. Classify each sample convergent / divergent / transform from the sign and obliquity of the
   orthogonal component.
5. Compute implied dip and radius per sample from age.
6. Segment into runs of near-constant dip; mark joins as cusps.
7. Fit each run to a small circle of implied radius, concave toward the overriding plate; measure
   deviation.
8. Emit segments, cusps, warnings and diagnostics.

**Minimum tests:**

1. A synthetic pure-convergence boundary classifies as convergent along its whole length.
2. A synthetic pure-transform boundary produces no convergent samples and no teeth.
3. Obliquity is measured correctly for a boundary at a known angle to the relative motion.
4. Swapping which plate is nominated as overriding flips polarity warnings and mirrors curvature
   sign, and changes nothing else.
5. A step change in subducting-plate age produces exactly one cusp, at the step.
6. A smooth age gradient produces no cusp.
7. An arc drawn on the small circle implied by its dip reports zero deviation.
8. A straight line across a long convergent stretch reports the no-curvature warning.
9. Polar, antimeridian and near-Euler-pole cases are handled or rejected cleanly, not silently
   wrong.
10. Zero or near-zero relative motion, unresolved plate circuits, and missing age data are rejected
    with distinct, nameable reasons.
11. Changing the reconstruction anchor does not change the physical result.

## PR B: the window

Modeless, opened from **World Building**, stays open across reconstruction-time changes and updates
with them.

1. Overriding-plate selection, with the boundaries of the selected plate drawn classified.
2. Per-segment table: length, convergence rate, obliquity, implied dip, deviation from implied arc.
3. Cusps marked on the globe and listed.
4. Warnings listed with their named case, never as a generic "invalid".
5. Teeth previewed on the correct side via `RenderedSubductionTeethPolyline`.
6. Trench migration relative to the overriding plate reported, with the extension/compression
   consequence stated in words — back-arc basin opening, or fore-arc shortening.
7. No geometry is written in v1. If a later version does write, it goes through the existing
   reverse-reconstruct / preserve-properties / single-undo pattern.

## Non-goals

- **Terminations.** Pinned. The tool is for adding zones to existing hand-made data that may not be
  perfectly closed, and demanding valid terminations would reject exactly the data it is for.
  STEP faults, triple junctions and collision terminations are a later document.
- **Nucleation.** The user nucleates. Guidance ships as a separate PDF and as in-app help.
- **Rate enforcement.** Handled elsewhere or chosen by the user.
- **Global area balance.** Ocean-crust generation covers it.
- **Ridge consumption / slab windows.** Notes only for now; the interaction between an arriving
  ridge and an existing trench is its own problem.
- **Moving or regenerating geometry.** Explicitly D9's verb, not this one.
- **Anything below the surface.** No slab depth, no 3D, no tomography. Dip is a parameter that
  explains map-view curvature, nothing more.
- **Generating the volcanic arc.** Worth doing later, and it must be a separate feature drawn *with
  gaps* — flat-slab segments have no volcanoes above them despite active subduction, so an arc that
  mirrors the trench one-for-one is wrong.

## Open questions

1. **Is the target data topological?** Blocking. See above.
2. **Crust age comes from the ocean-crust generator's own output** — settled by the forward-modelling
   note above. What remains open is the *interface*: does the kernel query the generator, or read a
   property written onto crust features? The latter is looser coupling and probably right.
3. **What is the age→dip curve, and is it user-editable?** A default plus a tunable curve seems
   right for an art project; a fixed relation would be pretending to a precision that does not
   exist.
4. **What dip change constitutes a cusp** rather than a gradual bend? Needs a default and a setting.
5. **Do cusps become features, or stay annotations?** Tied to the polyline-versus-chain decision.
6. **Degrees or kilometres** for deviation and segment length, and does it match whatever
   max-segment-length settles on?
7. **What happens with no age data at all** — refuse to segment, or classify kinematically and
   report cusps as unavailable? The latter is probably right, since kinematic classification is the
   more valuable half and should not be gated on the less available data.
