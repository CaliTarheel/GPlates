# GPlates frontier workspace

This checkout is the integration workspace for the current upstream GPlates source and the open
feature pull requests maintained in `CaliTarheel/GPlates`.

## Repository layout

- `origin`: `https://github.com/CaliTarheel/GPlates.git` (writable fork)
- `upstream`: `https://github.com/GPlates/GPlates.git` (canonical source)
- integration branch: `codex/frontier`
- initial upstream base: `7f4b8fa85208c27fc69e660bd88bcb5ecf84363a`
- project-local Conda environment: `E:\deskPlate\.conda-env`
- development build: `E:\deskPlate\build-frontier`
- standalone application: `E:\deskPlate\install\gplates.exe`

## Integrated pull requests

The branch preserves every distinct open-PR head as an ancestor. The two Absolute Age PRs point at
the same commit, so eight PR records correspond to seven distinct branch tips.

| Repository | PR | Feature | Head commit |
| --- | ---: | --- | --- |
| `GPlates/GPlates` | #33 | Active Feature Types preferences | `b951d8b228e90cf92432922a445721af2f9edae0` |
| `GPlates/GPlates` | #34 | Absolute Age colouring | `0f429a9fb70343bdcdb3c48f95f39b168032c378` |
| `CaliTarheel/GPlates` | #1 | Split Plate | `e2ee2eebae91b67b847e7d109d20869a59b9c018` |
| `CaliTarheel/GPlates` | #2 | Absolute Age draw style (same commit as upstream #34) | `0f429a9fb70343bdcdb3c48f95f39b168032c378` |
| `CaliTarheel/GPlates` | #3 | World-building and geometry-editing bundle | `437ffa202dbbb6d7e5c27038993f752da6e94c32` |
| `CaliTarheel/GPlates` | #4 | Feature Statistics context menu | `a3767fc25f030b941b6bca1e72a309d6d2dccef3` |
| `CaliTarheel/GPlates` | #5 | Multi-vertex geometry editing | `3f120b99cc1997d5fbb2bef4b48faa6b274a7c1d` |
| `CaliTarheel/GPlates` | #6 | Rotation File Editor | `69b8098134dcc4df4fbec6a7b9ca0fad7ec5c7db` |

PR #3 contains earlier versions of Split Plate, Absolute Age, and multi-vertex editing. The focused
PRs were merged afterward for explicit provenance. Where their integration hunks overlapped, the
frontier branch keeps the superset UI and build registration. Rotation File Editor was combined with
the World Building and Feature Statistics actions in `ViewportWindow`.

## Build

From an Anaconda Prompt or Command Prompt:

```bat
call C:\Users\rider\miniforge3\Scripts\activate.bat E:\deskPlate\.conda-env
cd /d E:\deskPlate
cmake -S . -B build-frontier -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGPLATES_USE_PRECOMPILED_HEADERS=ON
cmake --build build-frontier --parallel 4
```

The repository's CMake setup detects the active Conda prefix and locates Qt, Boost, GDAL, PROJ,
CGAL, Python, and the other dependencies automatically. The frontier branch keeps the project's
precompiled-header acceleration compatible with Qt 6 and excludes OpenGL translation units so they
retain their required GLEW-before-Qt include order.

To install a standalone application after a successful build:

```bat
cmake --install build-frontier --prefix E:\deskPlate\install
```

The current standalone bundle was generated from the `RelWithDebInfo` build. It contains GPlates
2.6.0-dev6 plus Qt, Python, NumPy/OpenBLAS, GDAL, PROJ, and the other runtime dependencies, so it does
not require Conda activation.

## Validation

- Every distinct open-PR head listed above is an ancestor of `codex/frontier`.
- CMake configured with MSVC 19.44, Qt 6.11.1, Qwt 6.3.0, GDAL 3.13.2, PROJ 9.8.1,
  Python 3.14.6, and NumPy 2.5.1.
- The complete `gplates` target compiled and linked successfully.
- `absolute-age-draw-style-test`: 5 of 5 Python tests passed.
- `ViewOperationsTestSuite/NaturalizeCoastlineTestSuite`: 5 of 5 C++ cases and 155 of 155 assertions passed.
- Both the development executable and the standalone executable returned version/help output successfully.
- Standalone artifact: `E:\deskPlate\install\gplates.exe` (4,211 files, 338,282,992 bytes total).

## Updating the frontier

Fetch both remotes and merge upstream first:

```bat
cd /d E:\deskPlate
git fetch --prune origin
git fetch --prune upstream
git switch codex/frontier
git merge upstream/gplates
```

Merge each new or updated PR head separately with `--no-ff`. This keeps feature provenance visible
and makes it possible to verify inclusion with `git merge-base --is-ancestor <head> codex/frontier`.
Resolve shared UI registration files by retaining all actions, then run the targeted tests and the
full application build before pushing the frontier branch.
