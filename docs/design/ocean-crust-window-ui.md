# Design: Ocean Crust Creation window — destination dropdown and click selection

**Status:** design settled, implementation not started.
**Scope decision:** the destination dropdown serves all three tools in the window.
**Reported by:** Derek, 2026-08-09, against the SR3 build.

---

## What is wrong today

**Three tools, three destination choosers.** `CreateOceanCrustOperation`,
`CreateTripleJunctionCrustOperation` and `CreatePacificPlateOperation` each build their own
`ChooseFeatureCollectionWidget` inside their own modal dialog, and each keeps its own
`d_last_output_collection`. The three share one window and one MOR selection but share nothing about
where their output goes, so the collection is chosen three times in three places.

**The destination is validated last.** `CreateOceanCrustOperation.cc:588` calls
`collection_widget->get_file_reference()` *after* the parameter dialog, after the band computation
and after the user has accepted the confirmation. If nothing is selected it throws
`NoFeatureCollectionSelectedException` and the whole workflow is discarded at the final step. That is
the "select the feature layer first or nothing is built" complaint.

**Shift-click is the only way to select, and it also removes.** `ClickGeometry.cc:127`
`handle_shift_left_click` is the sole entry to `try_select_worldbuilding_mor()`, and
`CreateOceanCrustOperation::select_focused_mor()` toggles — Shift-click an unselected MOR adds it,
Shift-click a selected one removes it. This contradicts the standing convention that Shift means
"keep the selection and add to it" and nothing else.

**Every outcome reports to the same label.** `OceanCrustCreationDialog.cc:147-150` sets
`d_message_label` from `result.message` for success, cancellation and every error alike. A failure is
typographically identical to a success, in a corner of the window, while the user is watching the
globe. This is why failures read as "nothing happened."

## The target

1. **Destination dropdown at the top of the window**, as step 1, before any MOR is picked. Loaded
   collections plus a "create new collection" entry so nothing is lost from the current chooser. All
   three buttons use it. The three modals stop asking.
2. **Plain click selects one MOR, replacing the selection. Shift-click adds.** Shift never removes;
   use Clear Selection, or plain-click something else.
3. **Validate the destination before doing any work**, so a missing selection is reported before the
   parameter dialog rather than after the confirmation.
4. **Distinguish errors from successes** in the message label — at minimum colour and a prefix.

## The one non-obvious constraint

**Plain-click capture must be gated on the window being open.** `ViewportWindow.cc:2504` currently
notes that selection is driven "whether or not this window is open." That is harmless while Shift is
the trigger, because Shift-click on a MOR is otherwise unused. It stops being harmless the moment
plain click captures MORs: every ordinary click on a ridge anywhere in GPlates would reset the crust
selection and swallow the normal focus-and-inspect behaviour.

So `try_select_worldbuilding_mor_exclusive()` returns false unless the Ocean Crust Creation window is
visible. Shift-click may keep its current always-on behaviour or be gated the same way; gating both
is more predictable and is the recommendation.

## Preserving "no empty collection on cancel"

`ChooseFeatureCollectionWidget::get_file_reference()` is not a pure query — if the "create new"
entry is current it calls `d_file_io.create_empty_file()` (`ChooseFeatureCollectionWidget.cc:304`).
Today that is called only after the user confirms, so cancelling creates nothing.

A dropdown read eagerly at button-press would lose that. Keep it by giving the operations a small
interface rather than a resolved collection:

```cpp
class OceanCrustDestination
{
public:
    virtual ~OceanCrustDestination() {}

    //! Non-committing: is a usable destination selected? Called before any work is done.
    virtual bool has_selection() const = 0;

    //! Committing: resolve the destination, creating a new collection if that is what was
    //! chosen. Called only after the user confirms, exactly where get_file_reference() is
    //! called today.
    virtual boost::optional<
            std::pair<GPlatesAppLogic::FeatureCollectionFileState::file_reference, bool> >
    resolve() = 0;
};
```

`OceanCrustCreationDialog` implements it. Each `trigger()` gains an `OceanCrustDestination &`
parameter, calls `has_selection()` up front and `resolve()` where it used to call
`get_file_reference()`. The embedded `ChooseFeatureCollectionWidget` and its layout row come out of
all three operations.

## Files

| File | Change |
|---|---|
| `qt-widgets/OceanCrustCreationDialog.h/.cc` | `ApplicationState &`; destination `QComboBox`; populate/refresh; implement `OceanCrustDestination`; error styling on the message label |
| `view-operations/OceanCrustDestination.h` | New. The interface above |
| `view-operations/CreateOceanCrustOperation.h/.cc` | `trigger()` takes the destination; drop the chooser; validate early; add exclusive select |
| `view-operations/CreateTripleJunctionCrustOperation.h/.cc` | Same, minus the selection change |
| `view-operations/CreatePacificPlateOperation.h/.cc` | Same, minus the selection change |
| `qt-widgets/ViewportWindow.h/.cc` | Pass `ApplicationState` to the dialog; `try_select_worldbuilding_mor_exclusive()`; visibility gate |
| `canvas-tools/ClickGeometry.cc` | `handle_left_click` calls the exclusive path |
| `view-operations/CMakeLists.txt` | Add the new header |

## Testing this needs

It touches two features that shipped as SR3's theme, so both need re-running even though no geometry
changes:

1. Destination dropdown lists loaded collections; "create new" works; the choice persists across all
   three buttons.
2. Cancelling any of the three after choosing "create new" leaves **no** empty collection behind.
3. Plain click on a MOR selects exactly one and replaces any previous selection.
4. Shift-click adds a second and a third; a fourth is refused with a message.
5. Plain click on a MOR with the window closed does the ordinary focus thing and does not touch the
   selection.
6. RRR triple junction still generates its three sectors into the dropdown's collection.
7. Pacific-style plate birth still works, including the void-seed click.
8. With no destination selected, all three report it *before* the parameter dialog opens.
9. An error message is visually distinguishable from a success message.

Per the standing rule: preview must be observed rendering and undo observed restoring. Neither ships
on the strength of this document.
