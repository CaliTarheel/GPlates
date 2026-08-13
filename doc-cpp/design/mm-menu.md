# MM menu integration

The `MM` menu is a native GPlates integration of the workflows described by
`mochi-moshi/gplates-utilities` v0.8.0-pre. No Python or PyQt source is copied
from that GPLv3 project. The implementation uses the GPLv2-or-later C++
operations already developed in the CaliTarheel GPlates worldbuilding series,
adapted to the 2.6.0-dev8-SR1 release line.

## Menu map

| Utilities capability | Native MM entry point |
| --- | --- |
| Project/session persistence and collection management | Guided Workflows: Worldbuilding Pasta, Open Project, Save Project, Manage Feature Collections |
| Polygon/plate splitting | Plate & Geometry: Split Plate |
| Polygon union, difference, intersection and symmetric difference | Plate & Geometry: Boolean Polygons |
| Initial continent and plate geometry | Plate & Geometry: Create Initial Continent, Place Circular Features |
| Continental rifting | Rifting & Spreading: Build Voronoi Rift Network, Make Rift |
| Seafloor spreading | Rifting & Spreading: Generate Oceanic Crust from MOR |
| Triple-junction spreading | Rifting & Spreading: Generate RRR Triple-Junction Crust |
| Oceanic plate birth | Rifting & Spreading: Create Pacific-Style Plate |
| Flowline generation and maintenance | Rifting & Spreading: Manage Flowlines |
| Rotation initialization and plate birth | Rotation & Motion: Create Initial Rotation Model; Rotation File Editor |
| Rotation validation and motion planning | Rotation & Motion: Advance Plate Motion, View Rotation Hierarchy, reconstruction-pole tools |
| Plate ID assignment | Rotation & Motion: Assign Plate IDs |
| Filtering, topology inspection and geometric validation | Native GPlates collection/layer controls plus the Worldbuilding Audit and boundary-section graph in Guided Workflows |
| GPML, rotation and snapshot export | Export submenu and reviewed project export profiles in Guided Workflows |

The guided Worldbuilding Pasta palette also exposes event history, audit and
repair queues, project timestamps, collision/accretion, mantle events, and
portable export profiles. These are the implied lifecycle and data-integrity
requirements behind the utilities' one-shot file processors.

## Subduction workflow

Subduction receives first-class entries under `MM > Subduction & Collision`:

1. **Create Initial Subduction Zone** infers a far-margin trench from a selected
   continent, half-stage MOR and reconstructed motion. It previews curvature,
   endpoint buffer and continental clearance before creating a feature.
2. **Subduction Effects & Lifecycle** previews island arcs or Andean/Laramide
   belts and requires a valid lifecycle plan before commit. Supported events
   are continued subduction, trench extension, rollback, trench jump, polarity
   reversal, plate invasion, flat-slab intervals and final retirement.
3. **Retire Subducted Oceanic Crust** previews the Boolean split at the current
   and older project timestamps, versions consumed crust rather than deleting
   history, and applies the result as one undoable edit.
4. **Collision & Accretion** and **Post-Collision Rifting** carry convergent
   margins into sutures, orogenic lifecycles and reviewed rerifting without
   silently rewriting older plate history.

Lifecycle commits preserve source/successor feature lineage, record event
metadata, validate distinct overriding/subducting Plate IDs and explicit
polarity changes, and delegate new plate IDs/rotation sequences to the common
plate-birth workflow. Boundary-section and motion diagnostics are advisory and
must be reviewed before a mutation is committed.

## Validation

Geometry and planner tests cover initial trench placement, smoothing, segment
length, continental clearance, overriding-side polarity probes, contained
orogenic belts, island-arc determinism, lifecycle reversal/retirement gates,
collision guardrails, boundary-section succession, ocean-crust band creation,
RRR junctions, plate birth and project/export integrity.
