# Pacific-style oceanic plate birth

This is the third mode of the Worldbuilding Pasta ocean-generation family. It is stacked on the basic MOR age-band workflow (PR #28) and RRR workflow (PR #29); it does not introduce another polygon clipper, crust-band generator, MOR feature constructor, project scheduler, naturalizer, or rotation-file editor.

## User workflow

1. Shift-click three `HalfStageRotationVersion3` `MidOceanRidge` features. They must describe the three unique pairs among exactly three old plates.
2. The third selection arms the next ordinary globe/map click. Click inside the intended local central void.
3. Choose **Create Pacific-Style Plate** from **World Building**.
4. Confirm the older interval bound, new plate ID, birth-time parent, loaded rotation collection, geometry collection, local-void radius, maximum MOR segment, squiggle amplitude/wavelength, smoothing, and deterministic seed.
5. Preview the ordinary side-crust fill, central plate, and three bounding MORs.
6. Accept once to commit the rotation, all crust, new MORs, and the valid-time ends of the three selected MORs as one undo step; reject to change nothing.

The interval starts at the next older authoritative Project Timestamp when available. The existing animation increment remains the fallback. A new plate ID is suggested deterministically but remains an editable, user-confirmed value.

## Shared services and anti-duplication boundary

`RotationPlateCreation` is the extracted non-UI plate-birth service. Both this operation and the Rotation File Editor use it to reject a duplicate/self-parented plate, verify the selected parent at the interval endpoints and all authoritative Project Timestamps through 0 Ma, and prepare a normal unattached `TotalReconstructionSequence`. The sequence contains identity poles at the birth time and 0 Ma, so the caller can add it inside a larger atomic command rather than editing `.rot` text.

`MORFeatureBuilder` centralizes the normal half-stage `MidOceanRidge` property set. Existing Make Rift and Post-Collision Rift operations now use the same constructor as Pacific birth.

The Pacific operation also reuses:

- the basic operation's bounded Shift-click MOR selection;
- `OceanCrustBandBuilder` for the ordinary six side fills, existing-crust inventory, overlap subtraction, rigid crust construction, and stored-frame conversion;
- `NaturalizeCoastlineGeometry` for deterministic endpoint-preserving MOR vertices and the confirmed maximum segment length;
- `GeometryIntersect::Graph` to reject a naturalized boundary crossing away from its intended shared endpoint; and
- the normal application undo stack, notification guard, and rendered-geometry update guard.

## Local-void rule

The operation first runs the shared ordinary side-band builder over all three selected MORs. Accepted side pieces are added to the occupied-crust mask as they are generated, so later sides cannot duplicate earlier sides.

For the central plate, `PacificPlateGeometry` chooses the end of each selected MOR nearest the captured seed. An endpoint outside the confirmed maximum angular radius is rejected. The three ends are connected in selection-cycle order, and every new edge is naturalized with fixed endpoints. The resulting closed spherical polygon must contain the seed and have no unintended boundary crossings.

Loaded crust and the just-generated ordinary side crust are subtracted from that candidate. Only one uncovered component containing the seed is accepted. The code never chooses the global spherical complement, never guesses between multiple seeded components, and never fills a component whose seed is already covered by existing crust.

Each new bounding MOR pairs the new plate with the one old plate common to the two selected MOR pairs at that edge. The validated three-pair graph makes that neighbour unambiguous.

## Rotation and storage assumptions

The first implementation deliberately creates the central plate coincident with its explicitly selected old parent at birth and keeps it fixed relative to that parent toward younger times. The central rigid-crust polygon is therefore converted to stored coordinates with the parent's birth-time absolute rotation before it is assigned the new plate ID.

New MOR geometries are stored at their `geometryImportTime`, which is the birth time, with explicit left/right plate IDs and `HalfStageRotationVersion3`. The three selected old MOR features retain their geometry and older history but have their `gml:validTime` periods ended at the birth time. The operation does not rewrite unrelated MOR sections or invent later plate motion.

## Atomicity and failure behavior

One composite `QUndoCommand` owns:

- the prepared rotation feature and selected rotation collection;
- all ordinary side-crust components;
- the central `OceanicCrust` feature;
- the three new half-stage MOR features; and
- before/after valid-time properties for the three superseded MORs.

Undo removes every created feature and restores the original MOR valid times. Redo re-adds the same feature objects, preserving feature IDs and geometry. Invalid pair metadata, a missing parent, duplicate plate ID, invalid collection, failed ordinary band, nonlocal/open void, covered seed, naturalization crossing, failed Boolean, rejected preview, or cancellation leaves the model unchanged.

## Focused verification

Focused Release object builds cover the operation, geometry service, rotation service, shared MOR builder, Rotation File Editor, both refactored rift operations, click tool, generated UI action, and `ViewportWindow`. The Pacific geometry tests cover a valid seeded local void, an outside seed, and a radius rejection. The test translation unit compiles directly; the full linked unit-test executable is not required for this focused branch check.

Manual save/reload and GUI reconstruction checks remain required before merge, particularly rotation persistence, valid-time orientation, Project Timestamp traversal, and visual continuity of half-stage MORs after birth.
