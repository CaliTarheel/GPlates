# Oceanic-Crust Retirement

`World Building > Retire Subducted Oceanic Crust` specializes the existing
Subduction Cutter workflow for `gpml:OceanicCrust`. It does not introduce a
second clipping service.

The current View time is the fixed younger bound. The older bound defaults to
the next older timestamp in the active Project Timeline. If no project
schedule is available, the current animation increment is used and the dialog
labels that fallback. The user can edit the older bound and the number of
equal detection checks.

The operation reconstructs from the older bound toward the current View time.
At every check it finds candidate OceanicCrust polygons on the selected
subducting plate and clips their first overlap with polygons on the overriding
plate. Outside pieces survive; overlapping pieces become separate features
with a disappearance time immediately younger than the detected overlap.

Before feature data changes, a preview report gives the number of affected
features, cut events, surviving pieces, retired pieces and detection checks.
The user must explicitly apply that preview. Apply is one undoable command;
Cancel leaves feature data unchanged. The original View time is restored after
preview, apply, cancel and error paths.

Detection checks are sampling points for finding overlap. They are not project
timestamps and do not alter the authoritative Project Timeline.
