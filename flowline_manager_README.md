# Flowline Manager deterministic fixture

The fixture exercises generation, recipe-based regeneration, diagnostics,
collection-layer visibility, deletion, and one-step undo without relying on a
large public reconstruction model.

## Load the fixture

1. Start GPlates and load
   `sample-data/unit-test-data/flowline_manager_rotations.rot`.
2. Load `sample-data/unit-test-data/flowline_manager_sources.gpml`.
3. Load `sample-data/unit-test-data/flowline_manager_output.gpml`. Keep this
   separate collection selected as the manager's output collection/layer.
4. Set the reconstruction time to `0 Ma`.

## Prove diagnostics and generation

1. Focus **Eligible half-stage source** on the globe or in Feature Properties.
2. Choose **World Building > Manage Flowlines...**.
3. Set **Sources** to **All eligible features in source collection** and
   **Output collection / layer** to `flowline_manager_output.gpml`.
4. Enter youngest `0`, oldest `100`, and step `10`; leave **Replace existing
   recipes for each source** selected and **Dry run only** selected.
5. Click **OK**. The report must show one eligible source, one skipped source,
   one planned generated feature, and this stable skipped Feature ID:
   `GPlates-flowline-manager-skipped-source` with the non-half-stage reason.
6. Reopen the manager with the same settings, clear **Dry run only**, and click
   **OK**. One `gpml:Flowline` is added to the output collection in one
   undoable operation. Its description records source Feature ID, schedule,
   and effective left/right Plate IDs. Its ten time periods cover 0--100 Ma.
7. Choose **Edit > Undo** once. The generated feature disappears. Redo restores
   it.

## Prove the saved recipe and lifecycle actions

1. Focus the generated flowline and reopen **World Building > Manage
   Flowlines...**. The manager resolves its saved source Feature ID and
   pre-fills youngest `0`, oldest `100`, and step `10`.
2. Change either test rotation at `100 Ma`, reload it, then run **Generate /
   regenerate** with replacement enabled. The old generated feature is replaced
   by one newly evaluated from the source and rotations; one undo restores the
   previous feature.
3. Select **Hide generated layer** (then **Show generated layer**) with
   `flowline_manager_output.gpml` selected. Only visual layers connected to that
   collection change visibility.
4. Select **Delete generated flowlines**. First keep **Dry run only** selected to
   inspect the matched count; then rerun with it cleared. One undo restores the
   deleted generated feature.

The manager intentionally scopes bulk work to all eligible features in the
focused source's collection. GPlates currently has a single focused feature,
so this does not claim an arbitrary multi-selection model.
