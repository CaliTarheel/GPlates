# D10 deterministic impact-crater fixture

This fixture proves the crater generator's schedule, seeded statistics,
time-aware Plate ID assignment, reverse reconstruction, reporting, metadata,
and one-step undo without claiming a calibrated chronology for a particular
planet.

The diameter sampler uses a truncated cumulative power law
`N(>=D) proportional to D^-b`. Its provisional default `b = 2.8` follows the
small lunar production example reported by Moore, Boyce, and Hahn (1980),
[doi:10.1007/BF00899820](https://doi.org/10.1007/BF00899820). Published crater
production functions vary with body and diameter range, so reviewers must
choose the exponent, diameter limits, event rate, and chronology appropriate
to their project. Here, **Impacts per timestamp** is a controlled synthetic
count, not a physical impact flux.

## Load and dry-run

1. Start GPlates and load
   `sample-data/unit-test-data/crater_generator_rotations.rot`.
2. Load `sample-data/unit-test-data/crater_generator_partition.gpml`.
3. Load the dedicated empty output collection
   `sample-data/unit-test-data/crater_generator_output.gpml`.
4. Choose **World Building > Generate Impact Craters...**.
5. Select `crater_generator_partition.gpml` as **Plate polygon collection**
   and `crater_generator_output.gpml` as **Output collection**.
6. Enter youngest `0`, oldest `100`, step `50`, impacts per timestamp `4`,
   seed `42`, radius `6371 km`, diameters `10--100 km`, cumulative exponent
   `2.8`, display lifetime `0`, and `48` circle vertices.
7. Keep **Uniform on sphere** and **Dry run only** selected, then choose **OK**.

The report must contain exactly three timestamps and twelve requested impacts.
It groups all twelve sampled diameters into four logarithmic bins, groups
assigned output by Plate ID, and lists every unassigned event (up to the first
50) with event index, time, diameter, latitude, and longitude. The fixture
polygon deliberately does not cover the globe, so the diagnostic path is
exercised. Repeating the run with the same settings must reproduce the same
counts and unassigned details.

## Apply, inspect, and undo

1. Reopen the dialog with the same values, clear **Dry run only**, and choose
   **OK**.
2. Inspect the output collection. Each assigned event is a circular
   `gpml:UnclassifiedFeature` with `gpml:reconstructionPlateId = 101`.
   `gml:description` records event index, event time in Ma, diameter in km,
   event-time centre, Plate ID, model, exponent, seed, and citation.
3. Set GPlates to each event time. The circle centre must lie on the moving
   test polygon because the event-time circle was reverse reconstructed before
   storage. With display lifetime `0`, each crater remains valid toward the
   present; a positive lifetime ends it that many My after impact, clamped at
   `0 Ma`.
4. Choose **Edit > Undo** once. The complete generated batch disappears. Redo
   restores it.

## Spatial-window check

Repeat the dry run with **Uniform equal-area latitude/longitude window** and,
for example, latitude `-20--20` and longitude `-40--40`. Sampling is uniform in
longitude and sine of latitude, avoiding the polar bias of uniform latitude.
The first implementation intentionally requires a non-wrapping longitude
window; split an antimeridian-crossing experiment into two runs.

Unassigned impacts are never silently forced to Plate ID 0. Select the same
polygon collection that the project treats as its plate-partition layer; the
first PR does not infer which of several loaded polygon collections is
scientifically authoritative.
