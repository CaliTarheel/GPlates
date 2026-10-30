# Design: Feature Property Change

**Status:** design only — no implementation.
**Target release:** SR5, once the work already in flight there lands.
**Requested by:** Derek, 2026-08-10.
**Related:** `ChangeFeatureTypeDialog` (upstream); D7 active feature types
([#19](https://github.com/CaliTarheel/GPlates/pull/19),
[#47](https://github.com/CaliTarheel/GPlates/pull/47));
double-click Begin/End ([#43](https://github.com/CaliTarheel/GPlates/pull/43));
`PlateIdReassignmentOperation` ([#11](https://github.com/CaliTarheel/GPlates/pull/11),
[#12](https://github.com/CaliTarheel/GPlates/pull/12) — already in the SR line, and the reason Plate
ID is deferred rather than included); Area Select
([#45](https://github.com/CaliTarheel/GPlates/pull/45), the eventual second input).

This document exists so the shape of the feature is agreed before any code is written.

---

## The request

A window under **Tools > Feature Property Change**. Set begin time, end time and feature type in it
once. Then click a feature on the globe and those values land on it. Click the next one, same values
land. The window stays open.

Plate ID was in the original request and is **deferred** — see below. Its row is built, visible and
disabled, marked WIP, so the layout the tool will eventually have is the layout it ships with.

## What it costs today

Four fields, three different dialogs, per feature:

| Field | Where it is edited today |
| --- | --- |
| `gml:validTime` begin | Feature Properties dialog, `EditTimePeriodWidget` |
| `gml:validTime` end | same widget, same trip |
| feature type | Feature Properties dialog > change-type toolbutton > `ChangeFeatureTypeDialog` (modal `exec()`) |
| `gpml:reconstructionPlateId` | per-property edit widget; or **Features > Reassign Focused Feature Without Jumping…**; or **Bulk Plate ID Operations…** |

Everything that does this in bulk today does **Plate ID only**: upstream **Assign Plate IDs**
(cookie-cuts by polygon), the fork's **Bulk Plate ID Operations…** (scoped by collection or
visibility, not by clicking), and the fork's no-jump reassignment (one focused feature, one dialog
per feature). Nothing sets a feature type or a valid time on more than one feature, ever, and
nothing at all is driven by pointing at the features you mean.

That is the gap, and note where it is not: Plate ID already has three tools. **The two fields with no
bulk path at all are exactly the two this ships with.**

---

## Shape

A modeless window, per the standing convention: it shows its state and the next step, and it does
not vanish. A row per field, each with a tick box that arms *that field*:

```
[x] Begin time (Ma)   [ 180.0      ]  (double-click = current reconstruction time)
[x] End time (Ma)     [ 0.0        ]
[ ] Feature type      [ gpml:Basin ▾]
[ ] Plate ID          [            ]  WIP — not yet implemented
                                      (changing it moves the feature; see the design note)

        ( ) Off   (o) Armed — clicks write
        [ Apply to focused feature ]

Last: "Kerguelen fragment" — begin 180.0, end 0.0 written. Type skipped: gml:name has no
       valid target property in gpml:Basin.
```

The Plate ID row is disabled, not hidden. Hiding it means the window changes shape when the field
lands and the user has to relearn it; showing it disabled says *this is coming, and it is not
finished*, which is the true statement. The WIP label carries a tooltip explaining the jump, so
nobody reads the disabled row as an oversight.

Unticked rows are not written. That matters more than it looks: "set the end time on these nine
features and change nothing else" is the actual common job, and a tool that writes every field every
time cannot do it.

### Arming, and why it is not optional

A plain left-click already means *focus this feature so I can look at it*. If an open window wrote on
every focus change, opening the window would silently convert inspection into editing, and the first
thing a user does with a new tool is click something to see what happens.

So: an explicit **Off / Armed** state, unmissable in the window, plus an **Apply to focused feature**
button that always works regardless of arming. Off is the state the window opens in.

Not a modifier key. Shift already means "keep the selection and add to it" and nothing else, and
spending Ctrl or Alt on this buys nothing the armed toggle does not already give.

---

## Why Plate ID is deferred

Changing `gpml:reconstructionPlateId` at a non-zero reconstruction time leaves present-day geometry
alone and applies a different rotation to it, so **the feature jumps on screen**. That is the entire
reason [#11](https://github.com/CaliTarheel/GPlates/pull/11) exists, and its fix — reverse-reconstruct
the displayed geometry through the new Plate ID so it reconstructs back to where it already is — is
in the SR line now as `PlateIdReassignmentOperation`.

The fix is available, so the deferral is not "this is unsolved". It is that including the field drags
in everything the fix carries:

- **Eligibility rules.** The no-jump path accepts rigid non-topological features only, with exactly
  one active geometry at the current time and an existing Plate ID property. Click a topological
  boundary and the tool has to refuse — a whole second class of skip, on the field a user is most
  likely to point at everything with.
- **A mode that is not a mode anywhere else.** Keep-position on or off, defaulting differently by
  reconstruction time, is a real decision the user must understand before their first click.
- **Geometry writes.** The no-jump path rewrites present-day coordinates. Every other field in this
  window writes a scalar.

Leaving it out buys the single most valuable property v1 can have: **nothing this tool writes can
move anything on screen.** A mis-click sets a date or a type on the wrong feature, and one undo puts
it back. That is a tool that can be handed to someone mid-project without a warning attached.

When it does land, it lands by calling `PlateIdReassignmentOperation` — not by reimplementing the
reverse-reconstruction, and not by writing the raw property.

---

## The two things that make the rest harder than it sounds

### 1. Feature type cannot be written unattended

`FeatureHandle::set_feature_type()` is one line, and calling it on click would be wrong.

`ChangeFeatureTypeDialog` is not ceremony — it exists because a type change can invalidate the
feature's existing properties. It walks every property and asks the GPGIM:

- property *name* valid for the new type → nothing to do;
- name invalid but *structural type* valid → it offers the user a per-property rename
  (`ChangePropertyWidget`);
- type invalid → the property is listed as one that will be left invalid.

Applying a type change on a click means making those decisions with nobody watching, and the result
is features that violate the GPGIM in ways that surface much later.

**Recommendation: apply the type only when it is clean** — every existing property valid for the new
type, nothing to rename, nothing orphaned. Otherwise write the other ticked fields, skip the type,
and say why in the window's log. Never migrate properties silently. A user who wants the migration
has the existing dialog for it, and the log tells them which features need it.

The same GPGIM check governs `gml:validTime`: it is not valid on every feature type, so on a type
that does not take it the write is skipped and reported rather than forced.

### 2. Undo has to be watched, not assumed

`ReplaceFeaturePropertiesUndoCommand` already does exactly what is needed — snapshot the feature's
property set, restore it on undo. It currently lives inside `PlateIdReassignmentOperation.cc`, but
nothing about it is Plate-ID-specific; it should be lifted into its own header and shared, since this
tool is its second caller and does not otherwise touch that file.

**One click is one undo step.** Not one step per session of clicking: the user's mental model is
"undo the last thing I did", and merging a run of clicks into a single command makes a twenty-click
run un-pickable.

Per the standing SR rule, undo is not shippable because it is wired. It ships when it has been
watched putting a feature back — both times and the type — in a build someone ran.

---

## What comes free

- The feature-type combo is `ChooseFeatureTypeWidget`, which already honours the D7 active feature
  types preference. The list is the user's working set, not all of the GPGIM.
- Double-click-to-current-reconstruction-time on the begin and end fields is
  [#43](https://github.com/CaliTarheel/GPlates/pull/43)'s behaviour, already built for
  `EditTimePeriodWidget`. Same gesture, same meaning, and it is the gesture this tool is used with
  most — "end everything I click at the time I am looking at".
- `FeatureFocus::focus_changed` is the click signal. No new canvas tool is needed; the window listens
  and the existing Choose Feature tool does the picking.

## Scope

**In:** begin time, end time, feature type. Tick-to-arm per field, armed/off, per-click undo, a
running log of what was written and what was skipped, and a disabled Plate ID row marked WIP.

**Deferred:** Plate ID, as above.

**Out of v1:** conjugate / left / right plate IDs, name, description, arbitrary properties. Each is a
reasonable later row and none of them is this release.

**The second input, later:** the apply core should take a *list* of features, with the click path
passing a list of one. Area Select ([#45](https://github.com/CaliTarheel/GPlates/pull/45)) is
designed as a transient selection that needs an action wired end to end, and "apply these fields to
everything in the lasso" is that action with no new logic. Building the core against one feature and
a `for` loop costs nothing now and is the whole of the integration later.

## Where it goes

**Tools**, as requested. The menu already holds the fork's editor windows (Rotation File Editor,
Configure Geometry Rendering) while Features holds the one-shot Plate ID operations, and this is a
window you leave open, not an operation you run.

## Open questions

1. **Does an armed click also focus?** If clicking writes *and* moves focus, the Feature Properties
   dialog follows along and shows the result, which is good feedback and also means a stray click
   both edits and navigates. Probably yes, but it should be a decision, not a side effect.
2. **Re-clicking a feature already carrying the values** — no-op silently, or log "no change"? A
   no-op that is invisible reads as a failed click.
3. **Does the log persist across arming?** A run of thirty clicks wants a list to check, not one line
   that keeps being overwritten.
4. **Begin after end.** The tool can be armed with a begin time later than the end time and write it
   to everything clicked. Refuse at arming time, or let `EditTimePeriodWidget`'s existing validation
   handle it per write?
