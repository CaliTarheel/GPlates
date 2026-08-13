# RRR triple-junction ocean-crust generation

This is the connected-three-ridge mode of the Worldbuilding Pasta ocean-crust workflow. It is a child of the basic MOR gap-filling change (PR #28), not a second crust generator. It reuses that change's Shift-click MOR selection, `OceanCrustBandBuilder`, active `OceanicCrust` inventory, overlap subtraction, output-feature construction, Project Timestamp default, and normal undo stack.

## User workflow

1. Shift-click three `MidOceanRidge` features. The shared MOR selection accepts at most three features and visibly reports its count.
2. Choose **Generate RRR Triple-Junction Crust** from the Worldbuilding Pasta palette or World Building menu.
3. Review the three plate pairs, older interval bound, conservative maximum endpoint extension, and destination crust collection.
4. Preview the resolved yellow MORs and plate-coloured crust components.
5. Accept once to extend the three MORs and add all generated crust as one undoable edit, or reject without changing data.

The older bound defaults to the next older authoritative Project Timestamp. When no valid project schedule is active, the existing animation increment is the fallback.

## Required topology

The operation accepts exactly three loaded `HalfStageRotationVersion3` MOR polylines. Their `leftPlate` and `rightPlate` properties must describe exactly three distinct plate IDs and the three unique unordered pairs between them. Duplicate pairs, self-pairs, a fourth plate, missing properties, multiple active geometries, or a MOR that is invalid at either interval bound stop before preview.

This validation intentionally does not infer or rewrite plate IDs, rotations, reconstruction methods, or the plate circuit.

## Conservative endpoint resolution

`TripleJunctionGeometry` examines all eight combinations of the three line ends. For each combination it computes a normalized spherical consensus and selects the solution with the smallest maximum endpoint distance, then the smallest total distance. It rejects a solution outside the user-specified extension limit or with a terminal branch angle outside the conservative 15-170 degree range.

The accepted solution modifies only the selected terminal ends. Each resolved current-time line is reverse-reconstructed using its existing half-stage reconstruction properties before replacing that MOR's existing geometry property.

## Shared crust construction

For each resolved MOR, `OceanCrustBandBuilder` constructs both adjoining side-plate bands from the older bound to the current View time. The builder subtracts all active loaded `OceanicCrust` before returning components. Components belonging to the same plate are merged through the existing `SubductionCutterGeometry` Boolean service before they are reverse-reconstructed and stored as normal rigid `OceanicCrust` features.

The RRR operation therefore owns orchestration only. It does not duplicate band geometry, polygon clipping, existing-crust discovery, feature-property construction, or the generic Boolean engine.

## Atomicity and scope

The three MOR property replacements and every new crust feature are committed through one `QUndoCommand` under model-notification and rendered-geometry update guards. Undo removes the generated features and restores all three original MOR properties. A cancelled dialog, failed validation, failed Boolean, rejected preview, or invalid output collection changes nothing.

Out of scope:

- changing or creating rotation sequences;
- creating a new plate ID or Pacific-style spreading void;
- choosing a tectonic history for the user;
- silently connecting endpoints beyond the configured limit; and
- replacing local event keyframes or the authoritative project schedule.

## Focused verification

The geometry tests cover a clean connected RRR, selection-order permutation, a near-miss inside the extension tolerance, and rejection of a large endpoint gap. Operation-level compile checks cover shared selection access, half-stage reverse reconstruction, the shared band builder and Boolean service, the combined undo command, the palette action, and the `.ui` XML.
