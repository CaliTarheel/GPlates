# Design: Merge loaded feature collections into one file (no code)

**Status:** Design proposal; no implementation code.
**Source:** GPlates user-reported missing desktop workflow.
**Target:** `CaliTarheel/GPlates` (reviewing upstream applicability separately).

## Problem and user evidence

A GPlates project can *load* multiple feature collections, but users want to save the contents of several loaded collections as one native data file. Current advice sends them to pyGPlates/GPlately or direct GPML editing, even when everything is already loaded into the desktop application.

- October 6, 2026, GPlates forum: "How to merge feature collections" — asks to combine loaded collections in the GUI; reply recommends GPlately: https://discourse.gplates.org/t/how-to-merge-feature-collections/3280
- August 20, 2021, GPlates forum: similar request, answered with a pyGPlates script or manual XML feature-member copying: https://discourse.gplates.org/t/merge-feature-collections/359
- Existing non-GUI approach: `gplately combine`, see https://gplates.github.io/gplately/v2.0.0/sphinx/html/command_line_interface.html#combine

These requests are similar enough that they should not be assumed independent, but they demonstrate a persistent five-year desktop workflow gap.

## Proposed UX

Add **File > Merge Loaded Feature Collections…**, also discoverable from **Manage Feature Collections**.

1. **Select sources:** show currently loaded collections with name/path, source format, feature count, and unsaved/read-only flags. Allow two or more selections regardless of display-layer visibility.
2. **Choose destination:** select a new `.gpml` or `.gpmlz` file. GPML/GPMLZ only in v1, because lossy Shapefile/GeoJSON output would not preserve all GPlates feature/property types.
3. **Mandatory read-only preflight:** show source and output counts, feature classes, duplicate Feature IDs, unresolved references (especially topology dependencies), unsaved in-memory source edits and any input-to-GPML preservation warnings.
4. **Save one merged copy:** serialize the actual stored features and typed properties, not presently displayed/reconstructed geometry. Keep original loaded collections, active layers, and dirty flags unchanged. The new output is deliberately **not automatically loaded** alongside the sources, to avoid duplicate Feature IDs in the running session.
5. **Success messaging:** explain where the file was written and recommend unloading originals before opening the merged output. Cancellation and failure leave the existing session and files untouched.
6. **Safe destination handling:** confirm overwrites explicitly; stage output and replace the target only after a successful write, using the existing platform-safe save mechanism.

## Scope

- Two or more **loaded** feature collections, including unsaved edits in their current in-memory state.
- Preserve feature types, Feature IDs, feature properties, validity times, coordinates, rotation samples, and topology reference values exactly wherever the existing native writer supports them.
- Combine feature *records*, not polygon shapes or map layers.
- Block export on duplicate Feature IDs across selected collections; show source and ID collision details rather than silently renumbering them.
- Show unresolved reference dependencies on unselected collections. If export may leave external references intact, require explicit acknowledgement and explain that the output may not be self-contained.
- Validate unsupported or potentially lossy imported property types before saving; never drop metadata silently.

## Non-goals

- No Boolean union, polygon clipping, topological re-resolution, collision repair or automatic Plate-ID reassignment.
- No editing/deleting/moving source features, no rewriting source files, no automatic unloading/reloading, and no changes to reconstruction-time or display state.
- No bulk merge of files not presently loaded; no directory crawling.
- No automatic topology/rotation-circuit repair or feature-identity regeneration.
- No arbitrary `.rot`, Shapefile, GeoJSON, or raster output support in v1.
- No inferred scientific interpretation or reconstruction at the current time.

## Acceptance criteria

1. Two valid GPML sources with disjoint Feature IDs produce one GPML/GPMLZ file with their complete expected feature inventory and unchanged typed properties; output reloads alone successfully.
2. Merging rotation and geometry collections, then loading only the output, produces equivalent reconstructed geometries at representative times within numerical tolerances.
3. Unsaved source edits appear in the merged output, while the original source files and dirty flags remain unchanged.
4. Colliding Feature IDs stop export with a specific report naming the IDs and both source files; no automatically regenerated IDs.
5. A topology referring to a feature in an unselected collection triggers a preflight warning rather than silently changing/dropping the reference.
6. Both Cancel actions are no-ops; the active collections, layer graph, selection, reconstruction time and visibility remain unchanged after export.
7. Output serialization error, disk-full, or rejected overwrite leaves prior destination content intact, without a partial replacement.
8. Successful export does not automatically load the output, to avoid duplicate IDs in memory.
9. UI clearly distinguishes *merge collections* from polygon Boolean union.
10. Round-trip tests include native collections, mixed rotation/geometry inputs, large collections, unresolved references, duplicate IDs and failure injection.

## Likely affected components

File-menu and Manage Feature Collections action wiring; current loaded-collection registry; typed feature iteration and identity/reference queries; native GPML/GPMLZ writer; save/overwrite/atomic-file utilities; preflight UI and diagnostics; serialization/round-trip and reconstruction regression tests. Exact C++ classes/files should be identified during implementation review.

## Suggested implementation

**Phase 1: plan only.** From a stable snapshot of selected in-memory collections, build a `CollectionMergePlan` with source handles, feature counts, IDs, inbound/outbound dependencies, diagnostics and destination path. Do not mutate live feature collections.

**Phase 2: validate.** Block on conflicting IDs, unsupported property serialization and errors; surface warnings for references to unselected collections and require informed acknowledgement. Detect if source collections changed after the preview and invalidate the plan.

**Phase 3: native export.** Construct a new output feature collection from existing typed records or stream through the existing native writer, preserving Feature IDs and references. Reuse the same error reporting used for normal saves; stage to a temporary sibling and commit atomically where supported.

**Phase 4: verify.** Round-trip and reconstruction equivalence tests, plus UI no-op and filesystem-failure tests. Prefer reusing GPlates' current serializer over writing raw GPML/XML concatenation or shelling out to an external CLI.

## Risks / open questions

- How reliably can the planner discover topology references, unresolved dependencies, and cross-collection references without changing the layer graph?
- Should duplicate IDs always block, even if content is identical? Proposed v1: **yes**.
- Which source-specific attributes from Shapefile and rotation-text imports cannot round-trip to native GPML? Show per-format warnings and block unpreservable records.
- Can large collections be streamed to avoid doubling peak memory?
- How can the output remain an internally consistent snapshot if a source changes between preflight and serialization?
- What is the repository's proven atomic-replacement strategy on Windows, macOS, Linux and remote filesystems?
- Should v2 optionally load the merged file after offering to unload originals? Deliberately excluded from v1.

## Duplicate check (October 25, 2026)

Checked open/closed issues and PRs in `CaliTarheel/GPlates` and `GPlates/GPlates` for "merge loaded feature collections", "feature collections merge", "combine collections" and related terms. No matching desktop GUI design or implementation found. Fork Boolean Polygons PR #26 handles **geometry**, not feature-collection file consolidation. GPlately's `combine` is a workaround, not a duplicate GUI workflow.
