# MOR Ocean-Crust Gap Filling

This workflow extends `CreateOceanCrustOperation`; it does not introduce a
parallel basic crust generator.

Shift-clicking a `gpml:MidOceanRidge` whose reconstruction method is
`HalfStageRotationVersion3` stores it as the operation selection. The selection
survives ordinary feature-focus changes so the user can inspect other features
before choosing **World Building > Generate Oceanic Crust from MOR**.
Shift-clicking the same MOR again clears it.

The current View time is the younger bound. The older bound defaults to the
next older Project Timestamp and is editable. If no project schedule is
available, the current animation increment is used and labelled as a fallback.
The selected MOR must be valid and reconstructable as one active polyline at
both bounds, with distinct left and right plate IDs.

For each side plate, `OceanCrustBandBuilder` carries the older ridge edge into
the plate's current-time frame and closes a ruled polygon against the current
MOR. It then subtracts the union of all active reconstructed
`gpml:OceanicCrust` polygons. An already filled side therefore creates nothing;
partial overlap creates only the remaining components.

The preview reports the left/right component counts, number of existing crust
polygons checked, and whether overlap was removed. Accepted components are
reverse-reconstructed into their owning plate frames and written to the user-
selected collection as one undoable command. The last accepted collection is
preselected on the next run. Existing collections retain their custom layer
names; only a newly created destination receives the default age-band name.

The operation creates no Plate ID, changes no rotation, and does not edit or
move the MOR. The reusable band builder is the geometry seam for later
triple-junction and Pacific-mode workflows.
