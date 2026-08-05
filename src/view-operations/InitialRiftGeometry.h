/* $Id$ */

/**
 * \file
 * Deterministic, continent-clipped Voronoi networks for rift planning.
 */

#ifndef GPLATES_VIEWOPERATIONS_INITIALRIFTGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_INITIALRIFTGEOMETRY_H

#include <vector>

#include <boost/cstdint.hpp>

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace InitialRiftGeometry
	{
		struct Parameters
		{
			Parameters();

			boost::uint32_t random_seed;
			double target_cell_spacing_km;
			double minimum_edge_length_factor;
			double maximum_segment_length_km;
			double planet_radius_km;
		};

		struct Edge
		{
			Edge(
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
					double length_km_,
					bool touches_coast_,
					unsigned int start_node_,
					unsigned int end_node_);

			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline;
			double length_km;
			bool touches_coast;
			unsigned int start_node;
			unsigned int end_node;
		};

		struct Metrics
		{
			Metrics();

			unsigned int site_count;
			unsigned int edge_count;
			unsigned int craton_intersection_count;
			double average_edge_length_km;
			double maximum_edge_length_km;
			double maximum_segment_length_km;
		};

		struct Result
		{
			Result(
					const std::vector<Edge> &edges_,
					const std::vector<GPlatesMaths::PointOnSphere> &sites_,
					const std::vector<GPlatesMaths::PointOnSphere> &nodes_,
					const Metrics &metrics_);

			std::vector<Edge> edges;
			std::vector<GPlatesMaths::PointOnSphere> sites;
			std::vector<GPlatesMaths::PointOnSphere> nodes;
			Metrics metrics;
		};

		/**
		 * Builds a random Voronoi edge network in a Lambert equal-area projection,
		 * clips every edge to the supplied spherical polygon and tessellates the
		 * surviving edges for rendering on the globe.
		 */
		Result
		generate(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent,
				const Parameters &parameters);

		/**
		 * As above, but treats every supplied polygon as a hard exclusion zone.
		 * A complete candidate edge is discarded if any positive-length part of
		 * it enters an exclusion polygon.
		 */
		Result
		generate(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent,
				const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &exclusion_polygons,
				const Parameters &parameters);

		/**
		 * Joins selected network edges into maximal continuous polylines.
		 *
		 * A connected, non-branching selection produces exactly one polyline.
		 * Disconnected selections produce one polyline per component. Branching
		 * selections are split only at branch nodes, since a single polyline cannot
		 * encode a branch without repeating geometry or introducing a false segment.
		 */
		std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>
		join_edge_paths(
				const Result &network,
				const std::vector<unsigned int> &edge_indices);
	}
}

#endif // GPLATES_VIEWOPERATIONS_INITIALRIFTGEOMETRY_H
