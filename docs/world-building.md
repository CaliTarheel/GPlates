# World Building turn tools

Open **WB** at the bottom of the left tool strip. The same commands are available in the
**World Building** menu. WB switches the canvas to feature picking; unfinished digitising
geometry remains available in the Digitisation tab.

## Split

At the reconstruction age you are editing:

1. Choose **Split** and pick the closed polygon.
2. Select one or more existing cutting polylines. Use Ctrl-click to select several.
3. Click **Split**.

The polygon and polylines must be available in active, visible reconstruct layers. Split
supports rigid polygon geometry; deformed polygons and time-window geometry properties
are not offered. Each new cut must enter and leave an affected piece exactly once. A clear
crossing through an exact boundary vertex is allowed, including a junction with an earlier cut.
Tangencies, lines ending inside or on the boundary, partial boundary overlaps, cuts through
holes, self-crossing cuts and ambiguous near-vertex paths are rejected without changing the model.
Very detailed cut paths can exceed the validation limit and need simplifying.

Several cutters are applied in list order, including to pieces produced by earlier cutters.
Exact duplicate lines and lines already following an existing boundary are skipped; at least
one selected line must make a new cut. A nearby parallel line is not treated as the boundary.
The tool does not extend short lines or change their coordinates to force a cut.

The cutting polylines remain unchanged. The resulting polygons stay in the original
collection and retain its feature properties, including plate IDs and valid times. One
**Edit > Undo** restores the original polygon and removes the generated pieces. Save the
modified collection when you want to keep the result.
**Redo** restores active polygon pieces. Assign new plate IDs separately if the pieces should
move on different plates; Split preserves the original polygon's plate ID.

## Flow lines

1. Set the reconstruction age to the start of your turn, such as **1000 Ma**.
2. Choose **Flow lines** to open the dialog directly, and check the mid-ocean ridges to use.
3. Enter a **Future duration (Myr)** and **Step interval (Myr)**.
4. Choose the **Output collection / file**: create new collections or add to a loaded collection.
5. Click **Create Flowlines**.

Future means toward 0 Ma: a duration of 10 Myr from 1000 Ma ends at 990 Ma. A 3 Myr step
samples 1000, 997, 994, 991, and 990 Ma; the final interval can be shorter. Both endpoints
are included, and a duration beyond 0 Ma is rejected.

Every vertex of each selected ridge seeds a pair of native GPlates flowlines, one on each
plate, using symmetric 50/50 spreading. A ridge needs distinct left and right plate IDs,
with rotations covering the requested ages. The original ridges remain unchanged. The
view advances to the end age so that the completed flowlines are visible.

Each ridge geometry produces one multipoint-seeded Flowline feature. New collections are
grouped by source rotation model. An existing destination must be unsaved or a writable
GPML/GPMLZ collection using the same rotation model as the selected ridges. Incompatible
destinations are reported without changing their features or layer connections.

Creation changes the loaded model; **save the output collection** to keep it on disk.
Use **GPML** or **GPMLZ** to preserve seed points, time samples and import age. New outputs
retain their source ridges' reconstruction-model connections; save the project to retain
those connections too. One **Edit > Undo** removes the generated flowlines while retaining
any features already in the destination. Empty output collections stay loaded, and the view
stays at the end age.

## Make Ocean Crust

Choose **Make Ocean Crust**, select the mid-ocean ridges, and enter a future duration and
step interval. **Normal** and **Triple Junction** default to **50 Myr** and **10 Myr**;
near 0 Ma the default span is shortened to the available age. Choose an output collection/file,
then create the crust. Normal produces
**OceanicCrust polygon features**, using normal symmetric (50/50) spreading.

For every adjacent pair of MOR vertices and each time interval, the tool creates a sliver
on each plate side. The polygons follow the same spreading calculation as the paired
flowlines. They are stored with their respective plate IDs and move with those plates at
later ages. The source MORs remain unchanged, and the view advances to the end age.

A sliver becomes visible when its time interval is complete. For example, the 1000–995 Ma
sliver first appears at 995 Ma. Its formation interval is recorded in its description;
smaller step intervals give finer age bands. These are discrete polygons, so they do not
grow continuously while scrubbing within a single interval.

Use a rotation model that describes spreading. Degenerate or folded cells are rejected
before any crust is added. This command does not perform collision or subduction clipping,
or implement the Pacific variant. With changing rotation poles, the native
flowline curves can differ slightly from the rigid crust when viewed between sample times.

The destination and Save rules are the same as for **Flow lines**. One **Edit > Undo**
removes all crust generated by the operation and preserves the destination's earlier features.

### Triple Junction

Choose **Triple Junction** in the Make Ocean Crust dialog. Select three MOR arms whose
left/right plate pairs form one three-plate junction: A–B, B–C and C–A. All three must use
the same rotation model. Check the endpoint of each arm that approaches the junction.

Use the preview to calculate the initial meeting point from the terminal ridge directions.
The preview reports the junction coordinates, extension lengths and sampled future centers.
Review the result before creating the crust. Changing the selected arms, endpoints, time
range or rotation model invalidates the preview.

At each interval the generator moves the junction with all three plates, finds a new shared
center, extends the working ridge tips and fills the central space with plate-owned crust.
This accounts for unequal plate motion. The generated polygons join at the sampled ages;
they have the same discrete completion-age behavior as Normal crust. Your original MOR
features remain unchanged. The working extensions are used to build crust, and do not replace
the original MORs with a continuously connected animated ridge network.

The command rejects ambiguous initial intersections, stationary or degenerate motion,
configurations requiring ridge retreat, and generated overlaps. It does not clip new crust
against existing continents or subduction zones. A rejected preview changes no features.

## Free Plate

Choose **Free Plate** in WB or the World Building menu. Select the rotation model, moving
plate ID and release age. The dialog starts with the focused plate and current time when
available; any valid plate and positive release age can be entered.

Click **Review changes**. The proposed change releases the plate to Plate 0 at that age and holds
its current orientation through 0 Ma. Older motion and descendant relative rotations remain
intact. Boundary poles are added along the former parent chain where necessary, so later
younger edits to that parent do not alter the release position.

Review and acknowledge replacement of existing younger motion before applying. Those younger
rows remain as disabled records. Applying creates one undoable model edit; save the listed
rotation collections normally afterward. The command does not write rotation files itself.

A moving display anchor can still move the freed plate on screen. MORs and flowlines may
depend on an integrated rotation history: if the proposed release would move an affected
feature at the release age, the review names it and refuses the change. Review that feature's
import time and motion before trying again. Existing independent plates are reported as
already free. Saved rotation text has finite precision, so tiny numerical differences can
remain after save/reload.

## Rift Maker

Draw a polyline with the existing digitising tool, or select a saved polyline, then choose
**Rift Maker**. Select one or more polygons and a new or compatible existing output collection
for the MORs. The complete sketch is retained.

Preview the cuts. The table identifies each resulting piece with its position and area;
review its proposed plate ID. Adjacent pieces must have distinct plate IDs before a spreading
MOR can be created. The direction of the sketch determines left/right adjacency. Plate IDs
must have valid rotations at the current age; Rift Maker does not create rotation circuits
or release plates automatically.

**Pieces inherit the source polygon's valid time.** A changed plate ID therefore applies
throughout that interval, including older ages. Review those assignments before accepting;
this geometry operation does not create a separate pre-rift/post-rift feature history.

Create applies the polygon changes and the trimmed MORs as one undoable operation. MORs follow
only accepted interior cut spans and begin at the current age. Separate polygons or repeated
entries into a concave polygon produce separate spans; outside gaps are not connected.
Reassigned pieces and MORs are stored in their appropriate reconstruction frames so they
match the preview at creation time. Save the changed source and output collections afterward.

Self-crossing sketches, dangling ends inside a polygon, ambiguous boundary contacts,
overlapping target ownership and cuts through hole boundaries are rejected before any edit.
Holes that the cut does not touch are preserved. Undo restores the source polygons and removes
the generated pieces and MORs; Redo restores active, reconstructable outputs. The sketch is
preserved throughout.

## 0MY

Choose **0MY**, select a loaded `.rot` or `.grot` file, and click **Apply**.

For each moving plate, this copies its youngest enabled rotation at a positive age to
0 Ma. For example, if its youngest nonzero age is 1000 Ma, that pole supplies its 0 Ma
rotation. A zero-angle rotation is a valid source: “nonzero” refers to the age.

The tool also preserves the source reference plate. It replaces existing enabled 0 Ma
samples or adds one when missing, leaving older and disabled samples alone. It reports
plates it cannot update unambiguously. The change is undoable and takes effect in the
loaded model; **save the rotation file** to keep it on disk.

## Reconstruction Pole speedometer

With **Modify Reconstruction Pole** active, the **Reconstruction Pole** panel shows the
maximum stage speed across the selected plate in **cm/year**. It updates while you drag,
including the existing model motion and your preview adjustment before you click Apply.

The interval starts at the preceding enabled older rotation keyframe: editing 990 Ma after
a 1000 Ma keyframe measures the 1000-to-990 Ma stage. An existing keyframe at the current
age is not used as the start, so the interval is never zero. The interval and current anchor
plate are shown beside the readout; speeds include motion inherited from parent plates and
use the project's planetary radius.

The maximum covers rigid geometry with the selected Plate ID in active reconstruction
layers using the same rotation model. Hidden geometry is included; child plates and
deforming geometry are excluded. Polygon interiors and line segments are included, so the
maximum is not limited to stored vertices. A dash indicates that no usable older keyframe
or plate geometry is available. This is a stage speed over the displayed interval, not an
instantaneous velocity or the distance travelled by your mouse.

## Time arrows

The small up/down arrows on the top **Time** field snap to multiples of **10 Ma**. From
997.25 Ma, down goes to 990 Ma and up goes to 1000 Ma. You can still type fractional ages.
The adjacent animation controls and Alt timestamp navigation keep their existing behavior.
