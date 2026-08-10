/* $Id$ */

#include "PacificPlateGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include <QObject>

#include "maths/GeometryIntersect.h"
#include "maths/MathsUtils.h"
#include "maths/PolygonOnSphere.h"
#include "maths/UnitVector3D.h"


namespace
{
	const double RADIANS_TO_DEGREES = 180.0 / GPlatesMaths::PI;

	double
	angular_distance(
			const GPlatesMaths::PointOnSphere &first,
			const GPlatesMaths::PointOnSphere &second)
	{
		return GPlatesMaths::calculate_distance_on_surface_of_sphere(first, second, 1.0).dval();
	}

	/**
	 * Spherical mean of the given points, normalized back onto the unit sphere. Same technique
	 * as TripleJunctionGeometry.cc's consensus() - kept as a separate small copy rather than a
	 * shared header, since the two callers otherwise have nothing in common and each already
	 * carries its own local angular_distance() duplicate.
	 */
	GPlatesMaths::PointOnSphere
	consensus(
			const std::vector<GPlatesMaths::PointOnSphere> &points)
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator point_iter =
				points.begin(); point_iter != points.end(); ++point_iter)
		{
			x += point_iter->position_vector().x().dval();
			y += point_iter->position_vector().y().dval();
			z += point_iter->position_vector().z().dval();
		}
		const double magnitude = std::sqrt(x * x + y * y + z * z);
		if (magnitude < 1e-12)
		{
			throw std::runtime_error("The candidate MOR endpoints have no stable spherical consensus.");
		}
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::UnitVector3D(x / magnitude, y / magnitude, z / magnitude));
	}

	bool
	intersection_is_intended_endpoint(
			const GPlatesMaths::GeometryIntersect::Graph &graph,
			const GPlatesMaths::PointOnSphere &endpoint,
			const GPlatesViewOperations::NaturalizeCoastlineGeometry::Parameters &parameters)
	{
		for (GPlatesMaths::GeometryIntersect::intersection_seq_type::const_iterator intersection_iter =
			graph.unordered_intersections.begin();
			intersection_iter != graph.unordered_intersections.end(); ++intersection_iter)
		{
			if (!GPlatesViewOperations::NaturalizeCoastlineGeometry::points_are_close(
					intersection_iter->position, endpoint,
					parameters.coincidence_tolerance_km, parameters.planet_radius_km))
			{
				return false;
			}
		}
		return true;
	}
}


GPlatesViewOperations::PacificPlateGeometry::Result::Result() :
	success(false),
	existing_overlap_removed(false),
	inserted_vertex_count(0),
	maximum_seed_distance_degrees(0.0),
	actual_maximum_segment_length_km(0.0)
{ }


GPlatesViewOperations::PacificPlateGeometry::BoundaryResult::BoundaryResult() :
	success(false),
	inserted_vertex_count(0),
	maximum_seed_distance_degrees(0.0),
	actual_maximum_segment_length_km(0.0)
{ }


GPlatesViewOperations::PacificPlateGeometry::BoundaryResult
GPlatesViewOperations::PacificPlateGeometry::compute_void_boundary(
		const polyline_seq_type &selected_ridges,
		const GPlatesMaths::PointOnSphere &seed,
		double maximum_seed_distance_degrees,
		const NaturalizeCoastlineGeometry::Parameters &naturalize_parameters)
{
	BoundaryResult result;
	if (selected_ridges.size() != 3 || maximum_seed_distance_degrees <= 0.0 ||
			naturalize_parameters.planet_radius_km <= 0.0)
	{
		result.error = QObject::tr("Pacific plate birth requires three ridges and positive geometry limits.");
		return result;
	}

	try
	{
		std::vector<GPlatesMaths::PointOnSphere> firsts;
		std::vector<GPlatesMaths::PointOnSphere> lasts;
		firsts.reserve(3);
		lasts.reserve(3);
		for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
		{
			if (selected_ridges[ridge_index]->number_of_vertices() < 2)
			{
				throw std::runtime_error("Every selected MOR needs at least two vertices.");
			}
			firsts.push_back(*selected_ridges[ridge_index]->vertex_begin());
			GPlatesMaths::PolylineOnSphere::vertex_const_iterator last_iter =
					selected_ridges[ridge_index]->vertex_end();
			--last_iter;
			lasts.push_back(*last_iter);
		}

		// Choose whichever endpoint of each ridge actually converges with the other two - a
		// triple junction is a property of the ridges themselves, not of exactly where inside
		// it the user happened to click the seed. Same permutation-search + spherical-consensus
		// technique as TripleJunctionGeometry::resolve_rrr_endpoints (used by the RRR tool),
		// so both triple-junction tools pick endpoints the same, proven way rather than Pacific
		// plate birth alone relying on per-ridge seed proximity. The seed is still required
		// (below) to land inside the resulting void, as a sanity check on the user's click.
		double best_total_distance = std::numeric_limits<double>::max();
		for (unsigned int endpoint_mask = 0; endpoint_mask < 8; ++endpoint_mask)
		{
			std::vector<GPlatesMaths::PointOnSphere> candidate_endpoints;
			for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
			{
				candidate_endpoints.push_back((endpoint_mask & (1u << ridge_index))
						? firsts[ridge_index] : lasts[ridge_index]);
			}
			const GPlatesMaths::PointOnSphere candidate_consensus = consensus(candidate_endpoints);
			double total_distance = 0.0;
			for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
			{
				total_distance += angular_distance(candidate_consensus, candidate_endpoints[ridge_index]);
			}
			if (total_distance < best_total_distance)
			{
				best_total_distance = total_distance;
				result.selected_endpoints = candidate_endpoints;
			}
		}

		for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
		{
			result.maximum_seed_distance_degrees = std::max(
					result.maximum_seed_distance_degrees,
					angular_distance(seed, result.selected_endpoints[ridge_index]) * RADIANS_TO_DEGREES);
		}
		if (result.maximum_seed_distance_degrees > maximum_seed_distance_degrees + 1e-9)
		{
			throw std::runtime_error(QObject::tr(
					"The nearest selected MOR endpoint is %1 degrees from the seed; the local-void limit is %2 degrees.")
						.arg(result.maximum_seed_distance_degrees, 0, 'f', 3)
						.arg(maximum_seed_distance_degrees, 0, 'f', 3).toStdString());
		}
		for (unsigned int first = 0; first < 3; ++first)
		{
			for (unsigned int second = first + 1; second < 3; ++second)
			{
				if (NaturalizeCoastlineGeometry::points_are_close(
						result.selected_endpoints[first], result.selected_endpoints[second],
						naturalize_parameters.coincidence_tolerance_km,
						naturalize_parameters.planet_radius_km))
				{
					throw std::runtime_error("Two selected MOR endpoints coincide; no central void can be formed.");
				}
			}
		}

		std::vector<GPlatesMaths::PointOnSphere> naturalized_ring;
		for (unsigned int edge_index = 0; edge_index < 3; ++edge_index)
		{
			const NaturalizeCoastlineGeometry::SegmentResult segment =
					NaturalizeCoastlineGeometry::naturalize_segment(
							result.selected_endpoints[edge_index],
							result.selected_endpoints[(edge_index + 1) % 3],
							naturalize_parameters);
			if (segment.points.size() < 2)
			{
				throw std::runtime_error("A naturalized MOR boundary did not contain two endpoints.");
			}
			result.inserted_vertex_count += segment.inserted_point_count;
			result.actual_maximum_segment_length_km = std::max(
					result.actual_maximum_segment_length_km, segment.maximum_segment_length_km);
			result.bounding_ridges.push_back(GPlatesMaths::PolylineOnSphere::create(segment.points));
			naturalized_ring.insert(naturalized_ring.end(), segment.points.begin(), segment.points.end() - 1);
		}

		for (unsigned int edge_index = 0; edge_index < 3; ++edge_index)
		{
			const unsigned int next_edge = (edge_index + 1) % 3;
			GPlatesMaths::GeometryIntersect::Graph graph;
			if (GPlatesMaths::GeometryIntersect::intersect(
					graph, *result.bounding_ridges[edge_index], *result.bounding_ridges[next_edge]) &&
					!intersection_is_intended_endpoint(
							graph, result.selected_endpoints[next_edge], naturalize_parameters))
			{
				throw std::runtime_error("Naturalization made two proposed MOR boundaries cross away from their shared endpoint.");
			}
		}

		const OceanCrustBandBuilder::polygon_ptr_type candidate =
				GPlatesMaths::PolygonOnSphere::create(naturalized_ring);
		if (!candidate->is_point_in_polygon(seed))
		{
			throw std::runtime_error("The clicked seed is not inside the local void bounded by the selected MOR endpoints.");
		}

		result.void_polygon = candidate;
		result.success = true;
		return result;
	}
	catch (const std::exception &exception)
	{
		result.error = QString::fromLocal8Bit(exception.what());
		return result;
	}
}


GPlatesViewOperations::PacificPlateGeometry::Result
GPlatesViewOperations::PacificPlateGeometry::build_local_void(
		const polyline_seq_type &selected_ridges,
		const GPlatesMaths::PointOnSphere &seed,
		double maximum_seed_distance_degrees,
		const NaturalizeCoastlineGeometry::Parameters &naturalize_parameters,
		const OceanCrustBandBuilder::polygon_seq_type &existing_ocean_crust)
{
	Result result;
	const BoundaryResult boundary =
			compute_void_boundary(selected_ridges, seed, maximum_seed_distance_degrees, naturalize_parameters);
	result.selected_endpoints = boundary.selected_endpoints;
	result.bounding_ridges = boundary.bounding_ridges;
	result.inserted_vertex_count = boundary.inserted_vertex_count;
	result.maximum_seed_distance_degrees = boundary.maximum_seed_distance_degrees;
	result.actual_maximum_segment_length_km = boundary.actual_maximum_segment_length_km;
	if (!boundary.success || !boundary.void_polygon)
	{
		result.error = boundary.error;
		return result;
	}

	try
	{
		const OceanCrustBandBuilder::Result uncovered =
				OceanCrustBandBuilder::subtract_existing_crust(**boundary.void_polygon, existing_ocean_crust);
		if (!uncovered.success)
		{
			throw std::runtime_error(uncovered.error.toStdString());
		}
		OceanCrustBandBuilder::polygon_seq_type seeded_components;
		for (OceanCrustBandBuilder::polygon_seq_type::const_iterator component_iter =
			uncovered.polygons.begin(); component_iter != uncovered.polygons.end(); ++component_iter)
		{
			if ((*component_iter)->is_point_in_polygon(seed))
			{
				seeded_components.push_back(*component_iter);
			}
		}
		if (seeded_components.size() != 1)
		{
			throw std::runtime_error(seeded_components.empty()
					? "Existing OceanicCrust covers the clicked seed; no new central plate remains."
					: "The clicked seed identifies more than one uncovered component; narrow the local void.");
		}

		result.central_crust = seeded_components.front();
		result.existing_overlap_removed = uncovered.existing_overlap_removed;
		result.success = true;
		return result;
	}
	catch (const std::exception &exception)
	{
		result.error = QString::fromLocal8Bit(exception.what());
		return result;
	}
}
