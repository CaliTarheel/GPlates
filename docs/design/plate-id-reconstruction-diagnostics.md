# Design: Plate ID reconstruction diagnostics

**Status:** design only — no implementation in this PR.
**Target release:** 2.6.0-dev*-SR1a
**Source:** [Points not reconstructing correctly in GPlates after assigning PlateID in ArcGIS
Pro](https://discourse.gplates.org/t/points-not-reconstructing-correctly-in-gplates-after-assigning-plateid-in-arcgis-pro/3192)
(user `yazizzz`)
**Related:** [#18](https://github.com/CaliTarheel/GPlates/pull/18) Rotation Hierarchy viewer,
[#11](https://github.com/CaliTarheel/GPlates/pull/11) no-jump Plate ID reassignment,
[#12](https://github.com/CaliTarheel/GPlates/pull/12) bulk Plate ID operations

---

## The problem

A user built a point shapefile in ArcGIS Pro, added a `PlateID` column following tutorial 4.1, and
loaded it into GPlates. Dragging the time slider: **some points did not move at all, and the ones
that did move went to the wrong places.**

They eventually worked out the cause themselves — they had treated PlateID like `FID` in ArcGIS, a
per-row unique identifier, rather than a reference to a plate in the rotation model. They then
abandoned the GUI entirely, installed pyGPlates, converted the shapefile to text, and reassigned
plate IDs in Python to get their 50 Ma reconstruction.

The misunderstanding is theirs. **The silence is ours.** At no point did GPlates say that the plate
IDs in the file did not correspond to anything in the loaded rotation model — not on import, not on
the shapefile attribute mapping dialog where the column was bound, not when the features failed to
move. Every piece of information needed to detect the problem was already loaded in memory.

## Root cause

`ReconstructionTree::get_composed_absolute_rotation()` returns the **identity rotation** for any
plate ID the tree does not describe. The header says so plainly
(`src/app-logic/ReconstructionTree.h:377-396`):

> If the motion of `moving_plate_id` is not described by this tree, then the identity rotation will
> be returned.

`ReconstructMethodByPlateId.cc:447` calls that overload, not the
`get_composed_absolute_rotation_or_none()` variant sitting directly above it that returns
`boost::none`. So a feature carrying an unmodelled plate ID reconstructs to exactly where it already
is, at every time, forever — visually identical to a plate that genuinely has not moved yet.

That produces two distinct failure modes, and only one of them looks like a failure:

1. **Plate ID absent from the rotation model** → identity → the point never moves. Looks like the
   time slider is broken.
2. **Plate ID present but semantically wrong** — an FID that happens to collide with a real plate
   number — → the point moves smoothly and confidently to a completely wrong palaeoposition. **This
   is the dangerous one.** It looks like success, it survives export, and it ends up in a PaleoDEM
   analysis.

The identity fallback itself is defensible: it avoids throwing from inside a tight reconstruction
loop and it lets partial rotation models render. **This proposal does not change the maths.** The
gap is that nothing above that layer ever reports what happened.

---

## Proposal

Four pieces, roughly in order of value per unit of work.

### 1. Reconstruction diagnostics report

A report that answers "why isn't this moving?" by comparing two sets that are both already in
memory: the plate IDs *referenced by loaded features*, and the plate IDs *described by the current
reconstruction tree*.

Buckets:

- **Matched** — reconstructs normally.
- **Unmatched** — referenced by features, absent from the rotation model. These are the points that
  never move. Show the ID, the count of features using it, and a way to select them.
- **No plate ID at all** — features with no `gpml:reconstructionPlateId` property. Different cause,
  same symptom, worth separating.

For `yazizzz`, this would have read something like *"143 features reference 143 plate IDs; 143
unmatched"* — diagnosis complete in one glance.

Do not build a second list of the rotation model's plate IDs; the Rotation Hierarchy viewer
([#18](https://github.com/CaliTarheel/GPlates/pull/18)) already presents that tree, and the
diagnostics report should link to it.

### 2. Notify on load — carefully

Warning at import is the obvious move and the ordering makes it harder than it looks: **rotation
files are frequently loaded *after* the geometry files**, so a check that runs once at import will
fire a false alarm on every normal session. The check has to run against layer-connection and
anchored-plate changes, not against the import event.

**Recommendation:** re-evaluate when the reconstruction tree or layer connections change; surface at
most one non-modal notification per file per session, dismissible, linking to the report. Never a
modal dialog that interrupts loading — this is information, not an error.

### 3. Validate at the shapefile attribute mapping dialog

`ShapefileAttributeMapperDialog` is where the user binds a column to `reconstructionPlateId`. It is
the earliest possible point of detection and currently gives no feedback at all.

If a rotation model is loaded when the mapping is made, the dialog can show how many of the column's
distinct values exist in it: *"0 of 143 distinct values match the loaded rotation model."* That one
line ends the forum thread before it starts.

**Open question:** is scanning distinct values affordable for large shapefiles at import time, or
does this need sampling with an explicit "sampled" label? A wrong reassurance is worse than silence.

### 4. Point at the tool that already solves this

GPlates has shipped the correct answer to this workflow for years: **Features > Assign Plate IDs…**
(`AssignReconstructionPlateIdsDialog`), which cookie-cuts plate IDs onto features by spatial location
from a static plate polygon set. `yazizzz` never found it, and hand-entering plate IDs in ArcGIS was
never going to work.

Naming it in the notification and offering it as an action from the diagnostics report is nearly
free and is probably the single highest-value line of text in this whole document.

---

## Open questions

1. **Where does the report live?** Recommendation: under the **Reconstruction** menu next to the
   Rotation Hierarchy viewer ([#18](https://github.com/CaliTarheel/GPlates/pull/18)), so the "what
   does my rotation model contain" and "what do my features ask for" views sit together. Dock or
   dialog?
2. **Notify, or report only on demand?** Silence is what caused this thread; nagging is what makes
   users stop reading dialogs. Is one dismissible per-file notification the right balance?
3. **Should unmatched features be visually marked on the canvas?** Seeing instantly which points are
   dead is powerful, and it collides with the draw-style system. Worth it, or too invasive?
4. **Time coverage is a possible third bucket.** A plate ID may exist in the rotation model but have
   no sequence covering the current reconstruction time. **This needs verifying against how
   `ReconstructionTree` is built before it is designed** — it may already fall into the unmatched
   bucket, or it may be a separate, time-dependent case that makes the report change as the slider
   moves. Do not assume; check.
5. **Scope of plate ID kinds.** Only `reconstructionPlateId`, or also the conjugate/left/right plate
   IDs used by flowlines and mid-ocean ridges? Those have their own silent-failure modes — the fork
   already fixed one in flowlines — and the same report structure would cover them.
6. **Anchored plate.** Changing the anchored plate changes the tree. Does the report recompute, and
   is that cheap enough to do live?

## Non-goals

- **Not changing the identity fallback.** The numerical behaviour stays; only reporting is added.
- Not auto-correcting plate IDs. Assign Plate IDs already exists for that; this feature's job is to
  send the user there.
- Not a shapefile attribute editor.
- Not validation of rotation-file internal consistency (real, separate, larger).
- No pyGPlates-side changes.

## Testing

This needs a fixture that does not currently exist: a small point file with deliberately bogus plate
IDs — some unmatched, some colliding with real plates, some absent — plus a rotation model. Failure
mode 2 in particular cannot be tested by eye, because it renders as a plausible reconstruction.

## Follow-up outside the code

Reply to the thread. `yazizzz` solved it alone and the resolution is buried in their own last post,
where the next person hitting this will not find it. The reply worth writing names
**Features > Assign Plate IDs…** as the GUI path, explains that a plate ID is a reference into the
rotation model rather than a row identifier, and says outright that GPlates giving no warning is a
bug we are tracking.
