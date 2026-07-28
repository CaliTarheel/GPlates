/* $Id$ */

// CGAL 6.2's generic arrangement validity helper currently instantiates a
// planar ray-shooting method for spherical topology. The Boolean engine itself
// uses the no-validation policy below; suppressing assertion-only calls avoids
// instantiating that inapplicable helper.
#define CGAL_NDEBUG

#include <algorithm>
#include <cmath>
#include <list>
#include <stdexcept>
#include <variant>
#include <vector>

#include <CGAL/Arr_geodesic_arc_on_sphere_traits_2.h>
#include <CGAL/Arr_spherical_topology_traits_2.h>
#include <CGAL/Boolean_set_operations_2/Gps_default_dcel.h>
#include <CGAL/Boolean_set_operations_2/Gps_on_surface_base_2.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Gps_traits_2.h>
#include <CGAL/number_utils.h>

#include "PolygonBooleanGeometry.h"

#include "maths/PolygonOrientation.h"
#include "maths/Vector3D.h"


namespace
{
	using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
	using ArrangementTraits = CGAL::Arr_geodesic_arc_on_sphere_traits_2<Kernel>;
	using BooleanTraits = CGAL::Gps_traits_2<ArrangementTraits>;
	using Dcel = CGAL::Gps_default_dcel<BooleanTraits>;
	using TopologyTraits = CGAL::Arr_spherical_topology_traits_2<BooleanTraits, Dcel>;
	using PolygonSet = CGAL::Gps_on_surface_base_2<
			BooleanTraits,
			TopologyTraits,
			CGAL::Boolean_set_operation_2_internal::NoValidationPolicy>;
	using CgalPolygon = BooleanTraits::Polygon_2;
	using CgalPolygonWithHoles = BooleanTraits::Polygon_with_holes_2;
	using CgalPoint = ArrangementTraits::Point_2;
	using CgalCurve = ArrangementTraits::Curve_2;
	using CgalXMonotoneCurve = ArrangementTraits::X_monotone_curve_2;
	using MakeXMonotoneResult = std::variant<CgalPoint, CgalXMonotoneCurve>;
	using point_seq_type = std::vector<GPlatesMaths::PointOnSphere>;
	using ring_seq_type = std::vector<point_seq_type>;


	CgalPoint
	to_cgal_point(
			const GPlatesMaths::PointOnSphere &point,
			const BooleanTraits &traits)
	{
		const GPlatesMaths::UnitVector3D &vector = point.position_vector();
		return traits.construct_point_2_object()(
				Kernel::Direction_3(vector.x().dval(), vector.y().dval(), vector.z().dval()));
	}


	template <typename PointIterator>
	CgalPolygon
	to_cgal_ring(
			PointIterator begin,
			PointIterator end,
			bool reverse,
			const BooleanTraits &traits)
	{
		point_seq_type points(begin, end);
		if (reverse)
		{
			std::reverse(points.begin(), points.end());
		}

		std::vector<CgalXMonotoneCurve> curves;
		for (size_t point_index = 0; point_index < points.size(); ++point_index)
		{
			const CgalPoint source = to_cgal_point(points[point_index], traits);
			const CgalPoint target = to_cgal_point(points[(point_index + 1) % points.size()], traits);
			const CgalCurve curve = traits.construct_curve_2_object()(source, target);
			std::vector<MakeXMonotoneResult> monotone_parts;
			traits.make_x_monotone_2_object()(curve, std::back_inserter(monotone_parts));
			for (std::vector<MakeXMonotoneResult>::const_iterator part_iter = monotone_parts.begin();
					part_iter != monotone_parts.end(); ++part_iter)
			{
				const CgalXMonotoneCurve *monotone_curve = std::get_if<CgalXMonotoneCurve>(&*part_iter);
				if (monotone_curve)
				{
					curves.push_back(*monotone_curve);
				}
			}
		}

		CgalPolygon ring;
		traits.construct_polygon_2_object()(curves.begin(), curves.end(), ring);
		return ring;
	}


	CgalPolygonWithHoles
	to_cgal_polygon(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const BooleanTraits &traits)
	{
		const bool reverse_exterior =
				polygon.get_orientation() != GPlatesMaths::PolygonOrientation::COUNTERCLOCKWISE;
		const CgalPolygon exterior = to_cgal_ring(
				polygon.exterior_ring_vertex_begin(),
				polygon.exterior_ring_vertex_end(),
				reverse_exterior,
				traits);

		std::vector<CgalPolygon> holes;
		for (unsigned int ring_index = 0; ring_index < polygon.number_of_interior_rings(); ++ring_index)
		{
			point_seq_type ring_points(
					polygon.interior_ring_vertex_begin(ring_index),
					polygon.interior_ring_vertex_end(ring_index));
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type ring_polygon =
					GPlatesMaths::PolygonOnSphere::create(ring_points, true);
			const bool reverse_hole =
					ring_polygon->get_orientation() != GPlatesMaths::PolygonOrientation::CLOCKWISE;
			holes.push_back(to_cgal_ring(ring_points.begin(), ring_points.end(), reverse_hole, traits));
		}

		return traits.construct_polygon_with_holes_2_object()(
				exterior, holes.begin(), holes.end());
	}


	void
	join_polygons(
			PolygonSet &polygon_set,
			const GPlatesViewOperations::PolygonBooleanGeometry::polygon_seq_type &polygons,
			const BooleanTraits &traits)
	{
		for (GPlatesViewOperations::PolygonBooleanGeometry::polygon_seq_type::const_iterator
				polygon_iter = polygons.begin(); polygon_iter != polygons.end(); ++polygon_iter)
		{
			polygon_set.join(to_cgal_polygon(**polygon_iter, traits));
		}
	}


	GPlatesMaths::PointOnSphere
	from_cgal_point(
			const CgalPoint &point)
	{
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::Vector3D(
						CGAL::to_double(point.dx()),
						CGAL::to_double(point.dy()),
						CGAL::to_double(point.dz())).get_normalisation());
	}


	bool
	points_are_close(
			const GPlatesMaths::PointOnSphere &lhs,
			const GPlatesMaths::PointOnSphere &rhs)
	{
		return GPlatesMaths::dot(lhs.position_vector(), rhs.position_vector()).dval() > 1.0 - 1e-12;
	}


	point_seq_type
	from_cgal_ring(
			const CgalPolygon &ring)
	{
		point_seq_type points;
		for (CgalPolygon::Curve_const_iterator curve_iter = ring.curves_begin();
				curve_iter != ring.curves_end(); ++curve_iter)
		{
			const GPlatesMaths::PointOnSphere point = from_cgal_point(curve_iter->source());
			if (points.empty() || !points_are_close(points.back(), point))
			{
				points.push_back(point);
			}
		}
		if (points.size() > 1 && points_are_close(points.front(), points.back()))
		{
			points.pop_back();
		}
		if (points.size() < 3)
		{
			throw std::runtime_error("Boolean operation produced a degenerate polygon ring");
		}
		return points;
	}
}


GPlatesViewOperations::PolygonBooleanGeometry::polygon_seq_type
GPlatesViewOperations::PolygonBooleanGeometry::apply(
		Operation operation,
		const polygon_seq_type &subjects,
		const polygon_seq_type &modifiers)
{
	if (subjects.empty())
	{
		throw std::invalid_argument("A polygon Boolean operation needs at least one subject");
	}
	if (modifiers.empty())
	{
		throw std::invalid_argument("A polygon Boolean operation needs at least one modifier");
	}

	BooleanTraits traits;
	PolygonSet subject_set(traits);
	PolygonSet modifier_set(traits);
	join_polygons(subject_set, subjects, traits);
	join_polygons(modifier_set, modifiers, traits);

	switch (operation)
	{
	case UNION:
		subject_set.join(modifier_set);
		break;
	case DIFFERENCE:
		subject_set.difference(modifier_set);
		break;
	case INTERSECTION:
		subject_set.intersection(modifier_set);
		break;
	case SYMMETRIC_DIFFERENCE:
		subject_set.symmetric_difference(modifier_set);
		break;
	}

	std::list<CgalPolygonWithHoles> cgal_results;
	subject_set.polygons_with_holes(std::back_inserter(cgal_results));

	polygon_seq_type results;
	for (std::list<CgalPolygonWithHoles>::const_iterator result_iter = cgal_results.begin();
			result_iter != cgal_results.end(); ++result_iter)
	{
		if (result_iter->is_unbounded())
		{
			throw std::runtime_error("Boolean operation produced the complement of a bounded polygon");
		}

		point_seq_type exterior = from_cgal_ring(result_iter->outer_boundary());
		ring_seq_type holes;
		for (CgalPolygonWithHoles::Hole_const_iterator hole_iter = result_iter->holes_begin();
				hole_iter != result_iter->holes_end(); ++hole_iter)
		{
			holes.push_back(from_cgal_ring(*hole_iter));
		}
		results.push_back(GPlatesMaths::PolygonOnSphere::create(exterior, holes, true));
	}
	return results;
}
