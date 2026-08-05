/* $Id$ */

/**
 * \file
 * Spherical polygon clipping support for the World Building subduction cutter.
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QPainterPath>
#include <QPolygonF>
#include <QObject>
#include <QTransform>

#include "SubductionCutterGeometry.h"

#include "maths/AngularDistance.h"
#include "maths/GeometryDistance.h"
#include "maths/PointOnSphere.h"
#include "maths/UnitVector3D.h"


namespace
{
	const double PROJECTION_SCALE = 100000.0;
	const double MAX_TESSELLATION_RADIANS = 0.25 * 3.14159265358979323846 / 180.0;
	const double MIN_PROJECTED_RING_AREA = 0.01;
	const double MIN_VECTOR_LENGTH = 1e-12;
	const double MIN_PROJECTION_DENOMINATOR = 1e-10;

	struct Vec3
	{
		Vec3() : x(0.0), y(0.0), z(0.0) {  }
		Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {  }

		double x;
		double y;
		double z;
	};

	Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs)
	{
		return Vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
	}

	Vec3 operator*(double scale, const Vec3 &vector)
	{
		return Vec3(scale * vector.x, scale * vector.y, scale * vector.z);
	}

	double dot(const Vec3 &lhs, const Vec3 &rhs)
	{
		return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
	}

	Vec3 cross(const Vec3 &lhs, const Vec3 &rhs)
	{
		return Vec3(
				lhs.y * rhs.z - lhs.z * rhs.y,
				lhs.z * rhs.x - lhs.x * rhs.z,
				lhs.x * rhs.y - lhs.y * rhs.x);
	}

	double length(const Vec3 &vector)
	{
		return std::sqrt(dot(vector, vector));
	}

	bool normalise(Vec3 &vector)
	{
		const double magnitude = length(vector);
		if (magnitude < MIN_VECTOR_LENGTH)
		{
			return false;
		}
		vector = (1.0 / magnitude) * vector;
		return true;
	}

	Vec3 to_vec3(const GPlatesMaths::PointOnSphere &point)
	{
		const GPlatesMaths::UnitVector3D &vector = point.position_vector();
		return Vec3(vector.x().dval(), vector.y().dval(), vector.z().dval());
	}

	struct Projection
	{
		Vec3 centre;
		Vec3 east;
		Vec3 north;

		bool project(const GPlatesMaths::PointOnSphere &point, QPointF &result) const
		{
			const Vec3 vector = to_vec3(point);
			const double denominator = 1.0 + dot(centre, vector);
			if (denominator < MIN_PROJECTION_DENOMINATOR)
			{
				return false;
			}
			const double scale = std::sqrt(2.0 / denominator) * PROJECTION_SCALE;
			result = QPointF(scale * dot(vector, east), scale * dot(vector, north));
			return true;
		}

		GPlatesMaths::PointOnSphere unproject(const QPointF &point) const
		{
			const double x = point.x() / PROJECTION_SCALE;
			const double y = point.y() / PROJECTION_SCALE;
			const double rho = std::sqrt(x * x + y * y);
			if (rho < MIN_VECTOR_LENGTH)
			{
				return GPlatesMaths::PointOnSphere(GPlatesMaths::UnitVector3D(
						centre.x, centre.y, centre.z));
			}
			const double limited_rho = std::min(2.0, rho);
			const double angular_distance = 2.0 * std::asin(0.5 * limited_rho);
			Vec3 vector = std::cos(angular_distance) * centre +
					(std::sin(angular_distance) / rho) * (x * east + y * north);
			normalise(vector);
			return GPlatesMaths::PointOnSphere(GPlatesMaths::UnitVector3D(
					vector.x, vector.y, vector.z));
		}
	};

	Projection create_projection(const GPlatesMaths::PolygonOnSphere &target)
	{
		Vec3 centre;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				target.exterior_ring_vertex_begin();
				vertex_iter != target.exterior_ring_vertex_end(); ++vertex_iter)
		{
			centre = centre + to_vec3(*vertex_iter);
		}
		if (!normalise(centre))
		{
			centre = to_vec3(*target.exterior_ring_vertex_begin());
		}

		const Vec3 reference = std::fabs(centre.z) < 0.9
				? Vec3(0.0, 0.0, 1.0) : Vec3(1.0, 0.0, 0.0);
		Vec3 east = cross(reference, centre);
		normalise(east);
		Vec3 north = cross(centre, east);
		normalise(north);

		Projection projection = { centre, east, north };
		return projection;
	}

	template <typename VertexIterator>
	bool add_projected_ring(
			QPainterPath &path,
			VertexIterator begin,
			VertexIterator end,
			const Projection &projection)
	{
		QPolygonF ring;
		for (VertexIterator vertex_iter = begin; vertex_iter != end; ++vertex_iter)
		{
			QPointF projected_point;
			if (!projection.project(*vertex_iter, projected_point))
			{
				return false;
			}
			ring.push_back(projected_point);
		}
		if (ring.size() < 3)
		{
			return false;
		}
		path.addPolygon(ring);
		path.closeSubpath();
		return true;
	}

	bool create_projected_path(
			QPainterPath &path,
			const GPlatesMaths::PolygonOnSphere &polygon,
			const Projection &projection)
	{
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type tessellated_polygon =
				GPlatesMaths::tessellate(polygon, MAX_TESSELLATION_RADIANS);
		path.setFillRule(Qt::OddEvenFill);
		if (!add_projected_ring(
				path,
				tessellated_polygon->exterior_ring_vertex_begin(),
				tessellated_polygon->exterior_ring_vertex_end(),
				projection))
		{
			return false;
		}
		for (unsigned int ring_index = 0;
				ring_index < tessellated_polygon->number_of_interior_rings(); ++ring_index)
		{
			if (!add_projected_ring(
					path,
					tessellated_polygon->interior_ring_vertex_begin(ring_index),
					tessellated_polygon->interior_ring_vertex_end(ring_index),
					projection))
			{
				return false;
			}
		}
		return true;
	}

	double signed_area(const QPolygonF &ring)
	{
		double area = 0.0;
		for (int point_index = 0; point_index < ring.size(); ++point_index)
		{
			const QPointF &point = ring[point_index];
			const QPointF &next = ring[(point_index + 1) % ring.size()];
			area += point.x() * next.y() - next.x() * point.y();
		}
		return 0.5 * area;
	}

	QPolygonF clean_ring(const QPolygonF &input)
	{
		QPolygonF ring;
		for (int point_index = 0; point_index < input.size(); ++point_index)
		{
			const QPointF &point = input[point_index];
			if (ring.empty() ||
					std::fabs(ring.back().x() - point.x()) > 1e-8 ||
					std::fabs(ring.back().y() - point.y()) > 1e-8)
			{
				ring.push_back(point);
			}
		}
		if (ring.size() > 1 &&
				std::fabs(ring.front().x() - ring.back().x()) < 1e-8 &&
				std::fabs(ring.front().y() - ring.back().y()) < 1e-8)
		{
			ring.removeLast();
		}
		return ring;
	}

	struct RingInfo
	{
		QPolygonF ring;
		double absolute_area;
		int parent;
		int depth;
	};

	bool polygon_contains_point(const QPolygonF &polygon, const QPointF &point)
	{
		return polygon.containsPoint(point, Qt::OddEvenFill);
	}

	GPlatesViewOperations::SubductionCutterGeometry::polygon_seq_type
	create_spherical_polygons(
			const QPainterPath &path,
			const Projection &projection)
	{
		typedef GPlatesViewOperations::SubductionCutterGeometry::polygon_seq_type polygon_seq_type;
		polygon_seq_type result;

		std::vector<RingInfo> rings;
		const QList<QPolygonF> subpaths = path.toSubpathPolygons(QTransform());
		for (QList<QPolygonF>::const_iterator subpath_iter = subpaths.begin();
				subpath_iter != subpaths.end(); ++subpath_iter)
		{
			const QPolygonF ring = clean_ring(*subpath_iter);
			if (ring.size() < 3)
			{
				continue;
			}
			const double area = std::fabs(signed_area(ring));
			if (area <= MIN_PROJECTED_RING_AREA)
			{
				continue;
			}
			RingInfo info = { ring, area, -1, 0 };
			rings.push_back(info);
		}

		for (unsigned int ring_index = 0; ring_index < rings.size(); ++ring_index)
		{
			double parent_area = std::numeric_limits<double>::max();
			for (unsigned int candidate_index = 0; candidate_index < rings.size(); ++candidate_index)
			{
				if (candidate_index == ring_index ||
						rings[candidate_index].absolute_area <= rings[ring_index].absolute_area)
				{
					continue;
				}
				if (polygon_contains_point(rings[candidate_index].ring, rings[ring_index].ring.front()) &&
						rings[candidate_index].absolute_area < parent_area)
				{
					rings[ring_index].parent = static_cast<int>(candidate_index);
					parent_area = rings[candidate_index].absolute_area;
				}
			}
		}

		for (unsigned int ring_index = 0; ring_index < rings.size(); ++ring_index)
		{
			int parent = rings[ring_index].parent;
			while (parent >= 0)
			{
				++rings[ring_index].depth;
				parent = rings[parent].parent;
			}
		}

		for (unsigned int exterior_index = 0; exterior_index < rings.size(); ++exterior_index)
		{
			if ((rings[exterior_index].depth % 2) != 0)
			{
				continue;
			}

			std::vector<GPlatesMaths::PointOnSphere> exterior_points;
			for (QPolygonF::const_iterator point_iter = rings[exterior_index].ring.begin();
					point_iter != rings[exterior_index].ring.end(); ++point_iter)
			{
				exterior_points.push_back(projection.unproject(*point_iter));
			}

			std::vector< std::vector<GPlatesMaths::PointOnSphere> > interior_rings;
			for (unsigned int hole_index = 0; hole_index < rings.size(); ++hole_index)
			{
				if (rings[hole_index].parent != static_cast<int>(exterior_index) ||
						(rings[hole_index].depth % 2) == 0)
				{
					continue;
				}
				interior_rings.push_back(std::vector<GPlatesMaths::PointOnSphere>());
				for (QPolygonF::const_iterator point_iter = rings[hole_index].ring.begin();
						point_iter != rings[hole_index].ring.end(); ++point_iter)
				{
					interior_rings.back().push_back(projection.unproject(*point_iter));
				}
			}

			if (GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(
					exterior_points.begin(), exterior_points.end(),
					interior_rings.begin(), interior_rings.end(), true) !=
				GPlatesMaths::PolygonOnSphere::VALID)
			{
				continue;
			}
			result.push_back(GPlatesMaths::PolygonOnSphere::create(
					exterior_points.begin(), exterior_points.end(),
					interior_rings.begin(), interior_rings.end(), true));
		}

		return result;
	}
}


GPlatesViewOperations::SubductionCutterGeometry::CutResult
GPlatesViewOperations::SubductionCutterGeometry::cut_polygon(
		const GPlatesMaths::PolygonOnSphere &target,
		const polygon_seq_type &cutters)
{
	CutResult result;
	polygon_seq_type relevant_cutters;
	for (polygon_seq_type::const_iterator cutter_iter = cutters.begin();
			cutter_iter != cutters.end(); ++cutter_iter)
	{
		if (GPlatesMaths::minimum_distance(
					target, **cutter_iter, true, true) == GPlatesMaths::AngularDistance::ZERO)
		{
			relevant_cutters.push_back(*cutter_iter);
		}
	}

	if (relevant_cutters.empty())
	{
		result.outside.push_back(polygon_ptr_type(&target));
		return result;
	}

	const Projection projection = create_projection(target);
	QPainterPath target_path;
	if (!create_projected_path(target_path, target, projection))
	{
		result.success = false;
		result.error = QObject::tr(
				"A target polygon reaches the antipode of its clipping projection.");
		return result;
	}

	QPainterPath cutter_path;
	cutter_path.setFillRule(Qt::OddEvenFill);
	for (polygon_seq_type::const_iterator cutter_iter = relevant_cutters.begin();
			cutter_iter != relevant_cutters.end(); ++cutter_iter)
	{
		QPainterPath projected_cutter;
		if (!create_projected_path(projected_cutter, **cutter_iter, projection))
		{
			result.success = false;
			result.error = QObject::tr(
					"An overriding polygon reaches the antipode of a required clipping projection.");
			return result;
		}
		cutter_path = cutter_path.united(projected_cutter);
		cutter_path.setFillRule(Qt::OddEvenFill);
	}

	const QPainterPath inside_path = target_path.intersected(cutter_path).simplified();
	if (inside_path.isEmpty())
	{
		result.outside.push_back(polygon_ptr_type(&target));
		return result;
	}
	const QPainterPath outside_path = target_path.subtracted(cutter_path).simplified();

	result.inside = create_spherical_polygons(inside_path, projection);
	result.outside = create_spherical_polygons(outside_path, projection);
	if (result.inside.empty())
	{
		result.success = false;
		result.error = QObject::tr(
				"The overlap was too small or degenerate to form a valid spherical polygon.");
		return result;
	}
	result.overlap = true;
	return result;
}


GPlatesViewOperations::SubductionCutterGeometry::BooleanResult
GPlatesViewOperations::SubductionCutterGeometry::apply_polygon_boolean(
		const GPlatesMaths::PolygonOnSphere &first,
		const polygon_seq_type &operands,
		BooleanOperation operation)
{
	BooleanResult result;
	if (operands.empty())
	{
		result.success = false;
		result.error = QObject::tr("Select at least one operand polygon.");
		return result;
	}

	const Projection projection = create_projection(first);
	QPainterPath first_path;
	if (!create_projected_path(first_path, first, projection))
	{
		result.success = false;
		result.error = QObject::tr(
				"The first polygon reaches the antipode of its clipping projection.");
		return result;
	}

	QPainterPath operand_path;
	operand_path.setFillRule(Qt::OddEvenFill);
	for (polygon_seq_type::const_iterator operand_iter = operands.begin();
			operand_iter != operands.end(); ++operand_iter)
	{
		QPainterPath projected_operand;
		if (!create_projected_path(projected_operand, **operand_iter, projection))
		{
			result.success = false;
			result.error = QObject::tr(
					"An operand polygon reaches the antipode of the first polygon's clipping projection.");
			return result;
		}
		operand_path = operand_path.united(projected_operand);
		operand_path.setFillRule(Qt::OddEvenFill);
	}

	QPainterPath output_path;
	switch (operation)
	{
	case POLYGON_UNION:
		output_path = first_path.united(operand_path);
		break;
	case POLYGON_DIFFERENCE:
		output_path = first_path.subtracted(operand_path);
		break;
	case POLYGON_INTERSECTION:
		output_path = first_path.intersected(operand_path);
		break;
	case POLYGON_SYMMETRIC_DIFFERENCE:
		output_path = first_path.united(operand_path).subtracted(
				first_path.intersected(operand_path));
		break;
	}

	output_path = output_path.simplified();
	if (!output_path.isEmpty())
	{
		result.polygons = create_spherical_polygons(output_path, projection);
		if (result.polygons.empty())
		{
			result.success = false;
			result.error = QObject::tr(
					"The Boolean result was too small or degenerate to form a valid spherical polygon.");
		}
	}
	return result;
}
