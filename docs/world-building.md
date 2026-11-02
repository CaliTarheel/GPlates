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
are not offered. Each cutting line must cross each affected piece's boundary exactly twice.
Contacts at vertices, overlapping edges, cuts
through holes, and lines that do not split a piece are reported without changing the model.
Several cutters are applied in list order, including to pieces produced by earlier cutters.

The cutting polylines remain unchanged. The resulting polygons stay in the original
collection and retain its feature properties, including plate IDs and valid times. One
**Edit > Undo** restores the original polygon and removes the generated pieces. Save the
modified collection when you want to keep the result.

## Make Ocean Crust — Normal

1. Set the reconstruction age to the start of your turn, such as **1000 Ma**.
2. Choose **Make Ocean Crust > Normal** and check the mid-ocean ridges to use.
3. Enter a **Future duration (Myr)** and **Step interval (Myr)**.
4. Click **Create Flowlines**.

Future means toward 0 Ma: a duration of 10 Myr from 1000 Ma ends at 990 Ma. A 3 Myr step
samples 1000, 997, 994, 991, and 990 Ma; the final interval can be shorter. Both endpoints
are included, and a duration beyond 0 Ma is rejected.

Every vertex of each selected ridge seeds a pair of native GPlates flowlines, one on each
plate, using symmetric 50/50 spreading. A ridge needs distinct left and right plate IDs,
with rotations covering the requested ages. The original ridges remain unchanged. The
view advances to the end age so that the completed flowlines are visible.

The new **Ocean crust flowlines** collections contain one multipoint-seeded Flowline feature
per ridge geometry, grouped by their source reconstruction layer. Save each as **GPML** to
preserve the seed points, time samples, and import age. Outputs retain their source ridges'
reconstruction-model connections; save the project to retain those layer connections too.
One **Edit > Undo** removes all generated flowlines. The empty output collections stay
loaded and the view stays at the end age. Pacific and Triple Junction are reserved for
later versions; this version generates flowlines only.

## 0MY

Choose **0MY**, select a loaded `.rot` or `.grot` file, and click **Apply**.

For each moving plate, this copies its youngest enabled rotation at a positive age to
0 Ma. For example, if its youngest nonzero age is 1000 Ma, that pole supplies its 0 Ma
rotation. A zero-angle rotation is a valid source: “nonzero” refers to the age.

The tool also preserves the source reference plate. It replaces existing enabled 0 Ma
samples or adds one when missing, leaving older and disabled samples alone. It reports
plates it cannot update unambiguously. The change is undoable and takes effect in the
loaded model; **save the rotation file** to keep it on disk.
