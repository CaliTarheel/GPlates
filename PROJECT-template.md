---
gplates:
  schema_version: 1

  # The planet this project is built on. Required.
  #
  # In kilometres, like every other distance here. Earth is 6371. A larger world means a degree
  # of arc covers more ground, so this is what lets GPlates report real distances rather than
  # angles that quietly mean something different from one project to the next.
  #
  # 'radius_m' in metres is still accepted for documents written before this changed.
  planet:
    radius_km: 6371

  # Intended resolution - the level of detail you mean to work at, expressed as the longest
  # segment you want a line to have, in kilometres.
  #
  # This is optional. Leave the whole section out and GPlates uses its own default; it will not
  # invent a number on your behalf.
  #
  # It is a statement of intent, not a rule the program enforces behind your back. Tools consult
  # it - for example, holding Shift while placing a vertex clamps that vertex to this distance
  # from the previous one, so a line cannot quietly become coarser than you meant.
  resolution:

    # Used for a new feature, and for any feature type not listed below.
    default_km: 500

    # Per-feature-type overrides, for the things that need finer or coarser detail than the rest
    # of the project. Used when you are adding to a feature that already exists, based on that
    # feature's type.
    #
    # Write the type name without its "gpml:" prefix - a colon inside a YAML key has to be
    # quoted, and that is an easy thing to get wrong by hand.
    #
    # Uncomment and adjust whichever of these you actually use. The names must match GPlates
    # feature types exactly; see Edit > Preferences > Active Feature Types for the full list.
    by_feature_type:
      # Coastlines and rifts are where detail shows, so they want shorter segments.
      # Coastline: 100
      # ContinentalRift: 100
      # OrogenicBelt: 150

      # Mid-ocean ridges and transforms carry real structure but at a larger scale.
      # MidOceanRidge: 250
      # Transform: 250
      # SubductionZone: 250

      # Ocean floor and craton interiors can be coarse without anyone minding.
      # OceanicCrust: 1000
      # Craton: 1000

  # The reconstruction times, in Ma, you intend to work through in order - oldest first, strictly
  # descending, comma-separated.
  #
  # This is optional. Leave it out and ordinary timeline stepping is unaffected. Set it and
  # holding Alt on the timeline's step buttons jumps to the next older/younger entry here instead
  # of stepping by the ordinary increment, so you cannot drift past a time you meant to stop at.
  #
  # Defaults to every 50 My from 1000 Ma to present - a coarse scaffold meant to be replaced with
  # the actual timestamps your world's history calls for.
  reconstruction:
    required_timestamps_ma: "1000, 950, 900, 850, 800, 750, 700, 650, 600, 550, 500, 450, 400, 350, 300, 250, 200, 150, 100, 50, 0"

    # How big a step you intend to evolve the world by, in millions of years.
    #
    # This is a statement of how finely you mean to work, not a limit. Take it down to 1 and you
    # will chew through a great deal more history by hand, and see processes that a coarser step
    # skips straight over; leave it at 5 or 10 and the same processes arrive as single events.
    # Both are legitimate ways to run a world.
    #
    # It matters most for anything that takes a known amount of time to happen. Subduction
    # initiation runs about 10 My from onset to a working arc, so at 1 My steps it is something
    # you watch unfold over ten of them, and at 10 My steps it is done the moment you place it.
    # Tools that model a process consult this to decide which of those two they are giving you.
    granularity_my: 5

  # How quickly subduction spreads once it exists.
  #
  # Optional; leave the section out and tools use these same defaults. The numbers below are
  # Earth's, and they are rules of thumb rather than constants - a world with hotter mantle or
  # weaker lithosphere has every right to different ones. They are here so that "how long should
  # this take?" has an answer written down in the project rather than guessed at each time.
  subduction:

    # Onset to a working volcanic arc, in My. Earth's best-dated case is the Izu-Bonin-Mariana
    # system: forearc basalts about 52 Ma, boninites 48-45 Ma, ordinary arc volcanism by 44-43 Ma.
    initiation_my: 10

    # How fast a trench lengthens along its own strike, in km per My. New subduction far more
    # often grows sideways from the end of a trench that already exists than it starts from
    # nothing, and it does so at roughly the speed the plates themselves move - a few cm/yr.
    # At this rate crossing a thousand kilometres of margin takes some tens of My.
    propagation_km_per_my: 30

    # How long a polarity reversal takes once an arc or a plateau jams a trench, in My.
    reversal_my: 8

    # How long after a continental collision the oceanic slab detaches and sinks free, in My.
    # The surface keeps the suture; the pull that was driving the plate disappears.
    breakoff_my: 15
---

# Project notes

Everything below the `---` markers is ordinary Markdown and is yours. GPlates reads only the
front matter above; it never rewrites this file.

Associate this document with a project through the project documents panel, and mark it as the
Primary Project Document. GPlates then reads the settings above whenever the file is saved or
reloaded.

## What this world is

Write whatever is useful to you here - the premise, the constraints you have set yourself, what
you have decided and what is still open. It travels with the project, which is the point.
