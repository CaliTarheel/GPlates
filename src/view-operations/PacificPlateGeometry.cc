/* $Id$ */

#include "PacificPlateGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <QObject>

#include "maths/GeometryIntersect.h"
#include "maths/MathsUtils.h"
#include "maths/PolygonOnSphere.h"


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


GPlatesViewOperations::PacificPlateGeometry::Result
GPlatesViewOperations::PacificPlateGeometry::build_local_void(
		const polyline_seq_type &selected_ridges,
		const GPlatesMaths::PointOnSphere &seed,
		double maximum_seed_distance_degrees,
		const NaturalizeCoastlineGeometry::Parameters &naturalize_parameters,
		const OceanCrustBandBuilder::polygon_seq_type &existing_ocean_crust)
{
	Result result;
	if (selected_ridges.size() != 3 || maximum_seed_distance_degrees <= 0.0 ||
			naturalize_parameters.planet_radius_km <= 0.0)
	{
		result.error = QObject::tr("Pacific plate birth requires three ridges and positive geometry limits.");
		return result;
	}

	try
	{
		for (polyline_seq_type::const_iterator ridge_iter = selected_ridges.begin();
			ridge_iter != selected_ridges.end(); ++ridge_iter)
		{
			if ((*ridge_iter)->number_of_vertices() < 2)
			{
				throw std::runtime_error("Every selected MOR needs at least two vertices.");
			}
			const GPlatesMaths::PointOnSphere &first = *(*ridge_iter)->vertex_begin();
			GPlatesMaths::PolylineOnSphere::vertex_const_iterator last_iter = (*ridge_iter)->vertex_end();
			--last_iter;
			const GPlatesMaths::PointOnSphere &last = *last_iter;
			const GPlatesMaths::PointOnSphere endpoint =
					angular_distance(seed, first) <= angular_distance(seed, last) ? first : last;
			result.maximum_seed_distance_degrees = std::max(
					result.maximum_seed_distance_degrees,
					angular_distance(seed, endpoint) * RADIANS_TO_DEGREES);
			result.selected_endpoints.push_back(endpoint);
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
		const OceanCrustBandBuilder::Result uncovered =
				OceanCrustBandBuilder::subtract_existing_crust(*candidate, existing_ocean_crust);
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
