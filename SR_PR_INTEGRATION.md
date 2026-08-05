# GPlates 2.6.0-dev6-SR PR Integration

This branch starts from official `GPlates/GPlates:gplates` commit
`27f9a63baa457de5fab3918d8aeeb034e88efc3a` and combines every pull request
that was open in `GPlates/GPlates` and `CaliTarheel/GPlates` when inventoried
on 2026-08-04.

The integration uses `CaliTarheel/GPlates:codex/wave1-playtest` as the base for
the already-resolved Wave 1 feature set. Current PR hardening commits are then
applied on top. Mirrored PRs are included once, not duplicated.

## Official GPlates PRs

| PR | Change | SR treatment |
|---|---|---|
| [GPlates/GPlates#19](https://github.com/GPlates/GPlates/pull/19) | pyGPlates CI | Included; CI-only PR targeting the separate `pygplates` branch |
| [GPlates/GPlates#33](https://github.com/GPlates/GPlates/pull/33) | Active Feature Types preferences | Wave 1 implementation plus current hardening |
| [GPlates/GPlates#34](https://github.com/GPlates/GPlates/pull/34) | Absolute Age colouring | Wave 1 implementation plus current hardening; same head as CaliTarheel #2 |
| [GPlates/GPlates#35](https://github.com/GPlates/GPlates/pull/35) | Linux desktop entry | Included directly |
| [GPlates/GPlates#36](https://github.com/GPlates/GPlates/pull/36) | Flowline seed-frame fix | Included directly |
| [GPlates/GPlates#37](https://github.com/GPlates/GPlates/pull/37) | Non-ASCII file paths | Included directly; same head as CaliTarheel #16 |

## CaliTarheel PRs

| PR | Change | SR treatment |
|---|---|---|
| [#1](https://github.com/CaliTarheel/GPlates/pull/1) | World Building Split Plate | Wave 1 implementation plus current hardening |
| [#2](https://github.com/CaliTarheel/GPlates/pull/2) | Absolute Age draw style | Covered once by official #34; exact same head |
| [#3](https://github.com/CaliTarheel/GPlates/pull/3) | World-building and geometry tools | Included by Wave 1, including Naturalize Coastline and Subduction Cutter |
| [#4](https://github.com/CaliTarheel/GPlates/pull/4) | Feature statistics | Wave 1 implementation plus current hardening |
| [#5](https://github.com/CaliTarheel/GPlates/pull/5) | Multi-vertex editing | Wave 1 implementation plus current hardening |
| [#6](https://github.com/CaliTarheel/GPlates/pull/6) | Rotation file editor | Wave 1 implementation plus current validation hardening |
| [#7](https://github.com/CaliTarheel/GPlates/pull/7) | Rotation keyframe copy | Wave 1 implementation plus current metadata-preservation hardening |
| [#8](https://github.com/CaliTarheel/GPlates/pull/8) | Drift correction | Wave 1 implementation plus current finalization hardening |
| [#9](https://github.com/CaliTarheel/GPlates/pull/9) | Select last-created feature | Wave 1 implementation plus current reporting hardening |
| [#10](https://github.com/CaliTarheel/GPlates/pull/10) | Human-readable rotations | Wave 1 implementation plus comment round-trip hardening |
| [#11](https://github.com/CaliTarheel/GPlates/pull/11) | No-jump Plate ID reassignment | Wave 1 implementation plus current hardening |
| [#12](https://github.com/CaliTarheel/GPlates/pull/12) | Bulk Plate ID operations | Included by Wave 1; shared Plate ID hardening comes from current #11 |
| [#13](https://github.com/CaliTarheel/GPlates/pull/13) | Advanced point alignment | Wave 1 implementation plus current hardening |
| [#14](https://github.com/CaliTarheel/GPlates/pull/14) | Plate direction arrows | Included by Wave 1 |
| [#15](https://github.com/CaliTarheel/GPlates/pull/15) | Project Markdown and planetary radius | All five current commits included directly |
| [#16](https://github.com/CaliTarheel/GPlates/pull/16) | Non-ASCII file paths | Covered once by official #37; exact same head |
| [#17](https://github.com/CaliTarheel/GPlates/pull/17) | Malformed GPML coordinate reporting | Included directly |
| [#18](https://github.com/CaliTarheel/GPlates/pull/18) | Rotation Hierarchy viewer | Included directly |

## Version identity

The semantic/package baseline remains `2.6.0-6`. The source appends the local
user-visible label `SR`, so the GUI reports `2.6.0-dev6-SR` in its title and
About/version surfaces.

This is a local integration branch, not an assertion that the upstream PRs are
merged or officially released. No Aesin project or rotation data is modified by
the integration.

## Local integration hardening

The combined branch also carries the minimum fixes needed to compile and run
the PR collection together on the current Windows development environment:

- Work around CMake 4.4.0's malformed Ninja resource-compiler rule on Windows.
- Keep the GCC/Clang-only `#include_next` Python wrapper out of MSVC's include
  path while retaining the existing `LONG_BIT` compatibility definition.
- Match rotation-file proxy updates by both moving and fixed plate IDs and use
  the actual `RotationPoleData::fix_plate_id` member.
- Apply reconstruction-pole adjustments before the Qt dialog closes, report
  invalid/no-op choices, and retain the original pole when updating the
  rotation-file proxy.

These are local integration fixes, not additional upstream PR claims.
