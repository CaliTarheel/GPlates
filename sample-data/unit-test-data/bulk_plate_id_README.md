# D5/D6/D13 bulk Plate ID synthetic fixture

Load these three files into GPlates:

- `bulk_plate_id_rotations.rot`
- `bulk_plate_id_scope_a.gpml`
- `bulk_plate_id_scope_b.gpml`

Set reconstruction time to **25 Ma**, source Plate ID to **101**, and target Plate ID to **202**.

With **All loaded collections**, a dry run should report three eligible `gpml:UnclassifiedFeature` records split across the two GPML collections. It should report `GPlates-d5d6d13-expired-point-a` as skipped because its valid time ended at 50 Ma. The Plate ID 202 control feature is outside the source scope and should not appear in either count.

Use a fresh reload of the fixtures for each destructive scenario:

1. **Copy to target Plate ID**, with copy begin time enabled: three copies should be created on Plate ID 202 without moving at 25 Ma. Their begin valid time should be 25 Ma.
2. **Move to target Plate ID**: three source features should move to Plate ID 202 without moving at 25 Ma.
3. **End valid time at current time**: three source records should remain in place and end at 25 Ma. The feature without a valid time should receive a distant-past-to-25-Ma period.
4. **Delete feature records**: three current source records should be removed. Undo should restore all three as one batch.
5. **Selected loaded collections**, with only `bulk_plate_id_scope_a.gpml` selected: two features should be eligible and the expired feature should be reported as skipped.

For copy/move, also compare positions before and after at 25 Ma and at an adjacent time such as 20 Ma. The no-jump guarantee applies at 25 Ma; adjacent-time differences are expected after changing the reconstruction Plate ID.
