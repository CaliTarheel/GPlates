# Boolean Polygons

`World Building > Boolean Polygons` is a modeless selection workflow for rigid
reconstructed polygon features.

1. Select the first polygon. It is the only feature that can be edited, and it
   supplies the feature type and every non-geometry property of all outputs.
2. Add one or more operand polygons. Operands are read-only and remain in their
   original feature collections.
3. Choose Union, Subtract, Intersection, or Symmetric difference, then select
   **Apply and Finish**. A successful operation closes the dialog.

Subtract means `first - union(operands)`. Intersection means
`first intersect union(operands)`. All outputs are reverse-reconstructed with
the first feature's reconstruction before being stored. The largest result
replaces the first geometry; additional disconnected results are cloned from
the first feature. An empty result removes the first feature. The entire edit
is one undo command.

The geometry implementation extends the spherical clipping service introduced
for Subduction Cutter. It tessellates great-circle arcs, uses a locally centred
Lambert azimuthal equal-area projection, performs the Boolean operation, and
maps valid rings and holes back to the sphere. An operation is rejected when an
input reaches the projection antipode or the output is degenerate.
