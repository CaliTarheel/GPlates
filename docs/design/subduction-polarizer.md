# Design: Subduction Polarizer

**Status:** design only — no implementation.
**Target release:** candidate for the next one. This is the shippable slice that unblocks L1.
**Related:** `subduction-boundary-classifier.md` (L1, which this is a prerequisite for);
`RenderedSubductionTeethPolyline.h`; `gpml:subductionPolarity`.

---

## The problem

Aesin's 24 subduction zones do not record which way they dip. They carry geometry and a
`reconstructionPlateId` and nothing else. So nothing in the project knows which plate is overriding,
the teeth cannot be drawn on a meaningful side, and L1 has no plate pair to compute convergence
from.

Fixing 24 features by hand is possible — it is 24, not 2400 — but the polarity of most of them is
inferable from data already present, and the ones that are not inferable are exactly the ones worth
a human decision. A tool that does the easy 80% and asks about the rest is a better use of the
effort than either extreme.

## What it writes, and why that matters

**`gpml:subductionPolarity`.** This already exists in GPlates as a `SubductionPolarityEnumeration`
with values `Left`, `Right`, `Unknown` and `None`, and it is already consumed by
`GlobeRenderedGeometryLayerPainter`, `MapRenderedGeometryLayerPainter` and
`ReconstructionGeometryRenderer` — it is **what decides which side the teeth are drawn on.**

That is the single most useful fact about this feature. Writing the property is the whole job; the
rendering is already wired. The reverse-polarity button is a `Left` ↔ `Right` toggle and the teeth
flip on screen with no new drawing code at all.

Left and Right are relative to the direction the polyline was digitized. That is an arbitrary
convention, which is exactly why an inference tool is worth having: nobody can look at a line and
know which way it was drawn.

## The workflow

**World Building > Subduction Polarizer…** opens a modeless window that stays open while you work.

1. **Nominate the continental layers.** A layer list with checkboxes — see the data note below for
   why this is necessary rather than optional.
2. **Select trenches on the globe.** The tool restricts selection to polylines. Shift adds to the
   selection, per the standing convention.
3. **The window lists what is selected**, one row per feature: current polarity, proposed polarity,
   confidence, and the reason for the proposal in words.
4. **Apply** writes the proposals. **Reverse** flips the selected rows. Both are one undoable step.
5. **Rows that could not be decided stay in the list, flagged**, with the next action stated. The
   user clicks a side of that line on the globe to nominate the overriding plate.

Step 5 is deliberately **not** a pop-up. The standing rule is that select-then-act tools get a
modeless window showing state and the next step, never a dialog that vanishes — and a warning that
disappears when dismissed is the wrong shape for something you may want to work through 24 times.
The unresolved rows simply remain visible until dealt with, which also makes "how many are left"
answerable at a glance.

## Inference

Four signals, in priority order. The first that fires decisively wins; otherwise they vote.

### 1. Crust type contrast — decisive when available

Continental one side, oceanic the other: the ocean subducts, the continent overrides. Continental
crust is far too buoyant to be carried down and kept down, so this admits no exceptions worth
modelling.

### 2. Crust age contrast

Ocean both sides: the older, colder, denser plate goes under. Confidence scales with the size of the
difference — a few My either way is noise, tens of My is decisive.

Age comes from `gpml:geometryImportTime`, which is populated on all 289 crust features. Note that it
disagrees with the age written into `gml:name` by one 50 My step (a polygon named
"Ocean Crust Plate 900 (1000.0 Mya)" carries `geometryImportTime` 950). **That discrepancy does not
affect this rule**, because only the *difference* across the trench is used and both sides carry the
same offset. It is flagged here because it will matter to something else eventually, and because
production code should read the structured field rather than parse numbers out of names.

### 3. Curvature

Trenches are concave toward the overriding plate. For hand-drawn data this is a better signal than
it sounds: the person drawing an arc usually curves it the way it looks right, and it looks right
because it is. Confidence comes from how consistently the curvature keeps its sign along the line —
a clean arc votes strongly, a wiggle votes not at all.

### 4. Neighbour agreement

A trench sharing an endpoint with an already-polarized trench usually matches it. Subduction does
not casually reverse polarity along a chain; where it does, there is a tear, and that is worth the
user seeing rather than the tool assuming.

### Combining them

Rule 1 decides alone. Otherwise sum the votes and require a margin — if the winner does not clear
the runner-up by a set amount, the feature is **undecided**, not guessed. Undecided is a useful
answer here and should not be tuned away; the whole point of the flagged rows is that a human looks
at the genuinely ambiguous cases.

Every proposal carries its reason as text: *"continent to the left"*, *"crust 80 My older to the
right"*, *"concave to the left along 90% of its length"*. A proposal the user cannot audit is a
proposal they have to check by hand anyway.

## The data note that shapes step 1

**Continental crust in Aesin is not typed as continental.** `Cratons.gpml` contains 12
`gpml:UnclassifiedFeature`; `Pangea super continent.gpml` contains 1. There is no `ContinentalCrust`
or `Craton` feature anywhere in the project.

So rule 1 — the decisive one — cannot fire from feature type. Three ways out:

1. **Nominate layers as continental in the window.** Works today, no data changes, and it is why
   step 1 exists. The user knows which layer is the cratons.
2. **Retype the features** to `gpml:ContinentalCrust` / `gpml:Craton`. The better long-term answer,
   a one-time cleanup, and it would benefit everything downstream that wants to know what is
   continental — including L2's polarity validation and the ocean lifecycle work.
3. Infer from file name. Fragile. Not recommended.

Ship with 1, because it is the version that works on the data as it stands, and treat 2 as a
separate tidy-up worth doing on its own merits.

## Non-goals

- **Assigning `leftPlate`/`rightPlate`.** Polarity is what the teeth and L1's overriding-plate
  question need. The plate pair is a related but separate property and adding it here would widen
  the feature past one thing.
- **Editing geometry.** The polarizer writes one enumeration property. It does not move, split,
  reverse or resample a line. Reversing *polarity* is not reversing the *line*.
- **Validating that the boundary is convergent at all.** That is L1's job. A trench drawn across a
  diverging boundary will be polarized happily here and flagged there.
- **Building plate polygons.** Considered and dropped — the crust polygons already partition the
  sphere by plate ID, so a dissolve would be convenience rather than capability.
- **Batch-polarizing the whole project without selection.** Everything goes through the selection,
  so the user always sees what they are about to change.

## Open questions

1. **Confidence margin for "undecided."** Needs a default and a setting. Err toward asking.
2. **How is the age contrast weighted against curvature** when they disagree? They will disagree,
   and disagreement is itself informative — possibly it should force undecided rather than letting
   the stronger vote win.
3. **What happens to features already carrying a polarity?** Leave them, propose over them, or show
   the disagreement. Showing it seems right; silently overwriting an existing decision does not.
4. **Does the reverse button write `Left`/`Right`, or is there a case for `None`?** `None` presumably
   means a boundary that is convergent but not subducting — a collision. Worth having if L2 will use
   it.
5. **Should nominating continental layers persist in the project** rather than being re-picked each
   session? If retyping the features happens, the question goes away.
