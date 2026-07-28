/* $Id$ */

/**
 * \file
 * Implements the Polygon Operations "Split Plate" tool.
 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QVBoxLayout>
#include <QUndoCommand>

#include "SplitPlateOperation.h"

#include "UndoRedo.h"
#include "VisibleGeometrySelection.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "feature-visitors/GeometrySetter.h"

#include "gui/FeatureFocus.h"

#include "maths/GeometryCrossing.h"
#include "maths/GeometryIntersect.h"
#include "maths/MathsUtils.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/NotificationGuard.h"
#include "model/TopLevelProperty.h"

#include "presentation/ViewState.h"


namespace
{
	typedef std::vector<GPlatesMaths::PointOnSphere> point_seq_type;
	typedef std::vector<point_seq_type> ring_seq_type;


	bool
	points_are_close(
			const GPlatesMaths::PointOnSphere &point1,
			const GPlatesMaths::PointOnSphere &point2)
	{
		return GPlatesMaths::dot(
				point1.position_vector(),
				point2.position_vector()).dval() >=
			GPlatesMaths::GeometryIntersect::Intersection::get_on_segment_start_threshold_cosine();
	}


	void
	append_point_if_distinct(
			point_seq_type &points,
			const GPlatesMaths::PointOnSphere &point)
	{
		if (points.empty() || !points_are_close(points.back(), point))
		{
			points.push_back(point);
		}
	}


	void
	remove_duplicate_closing_point(
			point_seq_type &points)
	{
		if (points.size() > 1 && points_are_close(points.front(), points.back()))
		{
			points.pop_back();
		}
	}


	point_seq_type
	create_polygon_boundary_path(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::GeometryIntersect::Intersection &start,
			const GPlatesMaths::GeometryIntersect::Intersection &end)
	{
		point_seq_type path;
		append_point_if_distinct(path, start.position);

		const unsigned int num_vertices = polygon.number_of_vertices_in_exterior_ring();

		// When both intersections are on the same segment and the end follows the
		// start, the forward boundary path contains no original polygon vertices.
		if (start.segment_index1 == end.segment_index1 &&
				start.angle_in_segment1 < end.angle_in_segment1)
		{
			append_point_if_distinct(path, end.position);
			return path;
		}

		unsigned int vertex_index = (start.segment_index1 + 1) % num_vertices;
		for (unsigned int visited = 0; visited < num_vertices; ++visited)
		{
			append_point_if_distinct(path, polygon.get_exterior_ring_vertex(vertex_index));
			if (vertex_index == end.segment_index1)
			{
				break;
			}
			vertex_index = (vertex_index + 1) % num_vertices;
		}

		append_point_if_distinct(path, end.position);
		return path;
	}


	point_seq_type
	create_cut_path(
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesMaths::GeometryIntersect::Intersection &start,
			const GPlatesMaths::GeometryIntersect::Intersection &end)
	{
		point_seq_type path;
		append_point_if_distinct(path, start.position);

		// A single great-circle segment can enter and leave a polygon. In that
		// common case there are no original polyline vertices between the two
		// intersections.
		if (start.segment_index2 == end.segment_index2 &&
				start.angle_in_segment2 < end.angle_in_segment2)
		{
			append_point_if_distinct(path, end.position);
			return path;
		}

		const unsigned int num_vertices = polyline.number_of_vertices();
		for (unsigned int vertex_index = start.segment_index2 + 1;
				vertex_index <= end.segment_index2 && vertex_index < num_vertices;
				++vertex_index)
		{
			append_point_if_distinct(path, polyline.get_vertex(vertex_index));
		}

		append_point_if_distinct(path, end.position);
		return path;
	}


	point_seq_type
	join_boundary_and_cut(
			const point_seq_type &boundary_path,
			const point_seq_type &cut_path,
			bool reverse_cut_path)
	{
		point_seq_type ring;
		for (point_seq_type::const_iterator point_iter = boundary_path.begin();
				point_iter != boundary_path.end();
				++point_iter)
		{
			append_point_if_distinct(ring, *point_iter);
		}

		if (reverse_cut_path)
		{
			for (point_seq_type::const_reverse_iterator point_iter = cut_path.rbegin();
					point_iter != cut_path.rend();
					++point_iter)
			{
				append_point_if_distinct(ring, *point_iter);
			}
		}
		else
		{
			for (point_seq_type::const_iterator point_iter = cut_path.begin();
					point_iter != cut_path.end();
					++point_iter)
			{
				append_point_if_distinct(ring, *point_iter);
			}
		}

		remove_duplicate_closing_point(ring);
		return ring;
	}


	GPlatesMaths::PointOnSphere
	reverse_reconstruct_point(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &reconstructed_feature_geometry)
	{
		const boost::optional<GPlatesModel::integer_plate_id_type> &plate_id =
				reconstructed_feature_geometry.reconstruction_plate_id();
		if (!plate_id)
		{
			return point;
		}

		return GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
				point,
				*plate_id,
				*reconstructed_feature_geometry.get_reconstruction_tree(),
				true /* reverse_reconstruct */);
	}


	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	reverse_reconstruct_polygon(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &reconstructed_feature_geometry)
	{
		point_seq_type exterior_ring;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
					polygon.exterior_ring_vertex_begin();
				point_iter != polygon.exterior_ring_vertex_end();
				++point_iter)
		{
			exterior_ring.push_back(reverse_reconstruct_point(*point_iter, reconstructed_feature_geometry));
		}

		ring_seq_type interior_rings;
		for (unsigned int ring_index = 0;
				ring_index < polygon.number_of_interior_rings();
				++ring_index)
		{
			point_seq_type interior_ring;
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
						polygon.interior_ring_vertex_begin(ring_index);
					point_iter != polygon.interior_ring_vertex_end(ring_index);
					++point_iter)
			{
				interior_ring.push_back(reverse_reconstruct_point(*point_iter, reconstructed_feature_geometry));
			}
			interior_rings.push_back(interior_ring);
		}

		return GPlatesMaths::PolygonOnSphere::create(exterior_ring, interior_rings, true);
	}


	struct SplitPolygonResult
	{
		SplitPolygonResult(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon1_,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon2_) :
			polygon1(polygon1_),
			polygon2(polygon2_)
		{  }

		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon1;
		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon2;
	};


	boost::optional<SplitPolygonResult>
	split_polygon(
			QString &error_message,
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &polygon_reconstruction)
	{
		GPlatesMaths::GeometryIntersect::Graph intersection_graph;
		if (!GPlatesMaths::GeometryIntersect::intersect(
				intersection_graph,
				polygon,
				polyline,
				true /* include polygon interior rings */))
		{
			error_message = QObject::tr(
					"The selected polyline does not cross the polygon boundary. "
					"The cutter must pass completely through the polygon.");
			return boost::none;
		}

		const unsigned int num_exterior_segments = polygon.number_of_segments_in_exterior_ring();
		for (GPlatesMaths::GeometryIntersect::intersection_seq_type::const_iterator intersection_iter =
					intersection_graph.unordered_intersections.begin();
				intersection_iter != intersection_graph.unordered_intersections.end();
				++intersection_iter)
		{
			if (intersection_iter->segment_index1 >= num_exterior_segments)
			{
				error_message = QObject::tr(
						"The cutter intersects an interior ring (a polygon hole). "
						"This first version can preserve holes, but cannot cut through one.");
				return boost::none;
			}
		}

		GPlatesMaths::GeometryIntersect::Graph crossing_graph =
				GPlatesMaths::GeometryCrossing::find_crossings(intersection_graph);

		if (crossing_graph.unordered_intersections.size() != 2 ||
				crossing_graph.geometry2_ordered_intersections.size() != 2)
		{
			error_message = QObject::tr(
					"The cutter must cross the polygon's exterior boundary exactly twice. "
					"This polyline produces %1 crossings.")
					.arg(crossing_graph.unordered_intersections.size());
			return boost::none;
		}

		// Reject boundary overlaps and tangential touches. They are ambiguous when
		// constructing two closed child rings.
		if (intersection_graph.unordered_intersections.size() !=
				crossing_graph.unordered_intersections.size())
		{
			error_message = QObject::tr(
					"The cutter touches or overlaps the polygon boundary. "
					"Move it so it crosses the boundary cleanly at two points.");
			return boost::none;
		}

		const GPlatesMaths::GeometryIntersect::Intersection &intersection1 =
				crossing_graph.unordered_intersections[
						crossing_graph.geometry2_ordered_intersections[0]];
		const GPlatesMaths::GeometryIntersect::Intersection &intersection2 =
				crossing_graph.unordered_intersections[
						crossing_graph.geometry2_ordered_intersections[1]];

		const point_seq_type cut_path = create_cut_path(polyline, intersection1, intersection2);
		const point_seq_type boundary_path1 =
				create_polygon_boundary_path(polygon, intersection1, intersection2);
		const point_seq_type boundary_path2 =
				create_polygon_boundary_path(polygon, intersection2, intersection1);

		const point_seq_type exterior_ring1 =
				join_boundary_and_cut(boundary_path1, cut_path, true);
		const point_seq_type exterior_ring2 =
				join_boundary_and_cut(boundary_path2, cut_path, false);

		if (GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(
					exterior_ring1, true) != GPlatesMaths::PolygonOnSphere::VALID ||
				GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(
					exterior_ring2, true) != GPlatesMaths::PolygonOnSphere::VALID)
		{
			error_message = QObject::tr(
					"The cut would create an invalid polygon. "
					"Try a cutter whose crossings are farther from polygon vertices.");
			return boost::none;
		}

		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type exterior_polygon1 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring1, true);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type exterior_polygon2 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring2, true);

		// Interior rings that are not cut remain intact and belong to whichever
		// new exterior contains their first vertex.
		ring_seq_type interior_rings1;
		ring_seq_type interior_rings2;
		for (unsigned int ring_index = 0;
				ring_index < polygon.number_of_interior_rings();
				++ring_index)
		{
			point_seq_type interior_ring(
					polygon.interior_ring_vertex_begin(ring_index),
					polygon.interior_ring_vertex_end(ring_index));
			if (exterior_polygon1->is_point_in_polygon(interior_ring.front()))
			{
				interior_rings1.push_back(interior_ring);
			}
			else if (exterior_polygon2->is_point_in_polygon(interior_ring.front()))
			{
				interior_rings2.push_back(interior_ring);
			}
			else
			{
				error_message = QObject::tr(
						"An interior ring could not be assigned to either output polygon.");
				return boost::none;
			}
		}

		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type reconstructed_polygon1 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring1, interior_rings1, true);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type reconstructed_polygon2 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring2, interior_rings2, true);

		return SplitPolygonResult(
				reverse_reconstruct_polygon(*reconstructed_polygon1, polygon_reconstruction),
				reverse_reconstruct_polygon(*reconstructed_polygon2, polygon_reconstruction));
	}


	class SplitPlateUndoCommand :
			public QUndoCommand
	{
	public:
		SplitPlateUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureHandle::weak_ref &polygon_feature,
				const GPlatesModel::FeatureHandle::iterator &polygon_property,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon1,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon2) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_polygon_feature(polygon_feature),
			d_polygon_property(polygon_property),
			d_original_geometry_property((*polygon_property)->clone()),
			d_polygon1_property(create_geometry_property(polygon_property, polygon1)),
			d_polygon2_property(create_geometry_property(polygon_property, polygon2)),
			d_first_redo(true)
		{
			setText(QObject::tr("split plate"));
		}

		virtual
		void
		redo()
		{
			if (!d_polygon_feature.is_valid() || !d_polygon_property.is_still_valid())
			{
				return;
			}

			GPlatesModel::FeatureCollectionHandle *feature_collection =
					d_polygon_feature->parent_ptr();
			if (!feature_collection)
			{
				return;
			}

			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			if (d_first_redo)
			{
				GPlatesModel::FeatureHandle::non_null_ptr_type new_feature =
						GPlatesModel::FeatureHandle::create(d_polygon_feature->feature_type());

				for (GPlatesModel::FeatureHandle::iterator property_iter = d_polygon_feature->begin();
						property_iter != d_polygon_feature->end();
						++property_iter)
				{
					GPlatesModel::FeatureHandle::iterator new_property_iter =
							new_feature->add((*property_iter)->clone());
					if (property_iter == d_polygon_property)
					{
						d_new_polygon_property = new_property_iter;
					}
				}

				new_feature->set(d_new_polygon_property, d_polygon2_property);
				d_new_feature = new_feature;
				d_feature_collection = feature_collection->reference();
				d_first_redo = false;
			}

			d_polygon_feature->set(d_polygon_property, d_polygon1_property);
			d_feature_collection->add(*d_new_feature);

			notification_guard.release_guard();
		}

		virtual
		void
		undo()
		{
			if (!d_polygon_feature.is_valid() || !d_polygon_property.is_still_valid() || !d_new_feature)
			{
				return;
			}

			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			d_polygon_feature->set(d_polygon_property, d_original_geometry_property);
			(*d_new_feature)->remove_from_parent();

			notification_guard.release_guard();
		}

	private:
		static
		GPlatesModel::TopLevelProperty::non_null_ptr_type
		create_geometry_property(
				const GPlatesModel::FeatureHandle::iterator &property,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon)
		{
			GPlatesModel::TopLevelProperty::non_null_ptr_type property_clone = (*property)->clone();
			GPlatesFeatureVisitors::GeometrySetter geometry_setter(polygon);
			geometry_setter.set_geometry(property_clone.get());
			return property_clone;
		}

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureHandle::weak_ref d_polygon_feature;
		GPlatesModel::FeatureHandle::iterator d_polygon_property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_original_geometry_property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_polygon1_property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_polygon2_property;
		boost::optional<GPlatesModel::FeatureHandle::non_null_ptr_type> d_new_feature;
		GPlatesModel::FeatureHandle::iterator d_new_polygon_property;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_feature_collection;
		bool d_first_redo;
	};
}


GPlatesViewOperations::SplitPlateOperation::SplitPlateOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface())
{  }


GPlatesViewOperations::SplitPlateOperation::Result
GPlatesViewOperations::SplitPlateOperation::trigger(
		QWidget *parent)
{
	using namespace VisibleGeometrySelection;

	const choice_seq_type polygon_choices = get_choices(d_view_state, POLYGONS);
	const choice_seq_type polyline_choices = get_choices(d_view_state, POLYLINES);
	if (polygon_choices.empty() || polyline_choices.empty())
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr(
						"Split Plate needs at least one visible reconstructed polygon and one visible "
						"reconstructed polyline. Show the required layers and try again."));
	}

	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Split Plate"));
	dialog.setModal(true);
	dialog.resize(720, dialog.sizeHint().height());

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *instructions = new QLabel(
			QObject::tr(
					"Choose a plate polygon and the polyline that cuts it. Inputs are the visible "
					"geometries at %1 Ma. The cutter must cross the exterior boundary exactly twice.")
					.arg(d_application_state.get_current_reconstruction().get_reconstruction_time(), 0, 'f', 2),
			&dialog);
	instructions->setWordWrap(true);
	layout->addWidget(instructions);

	QFormLayout *form_layout = new QFormLayout();
	QComboBox *polygon_combo = new QComboBox(&dialog);
	QComboBox *polyline_combo = new QComboBox(&dialog);
	for (choice_seq_type::const_iterator choice_iter = polygon_choices.begin();
			choice_iter != polygon_choices.end(); ++choice_iter)
	{
		polygon_combo->addItem(choice_iter->label);
	}
	for (choice_seq_type::const_iterator choice_iter = polyline_choices.begin();
			choice_iter != polyline_choices.end(); ++choice_iter)
	{
		polyline_combo->addItem(choice_iter->label);
	}
	form_layout->addRow(QObject::tr("Plate polygon:"), polygon_combo);
	form_layout->addRow(QObject::tr("Cutter polyline:"), polyline_combo);
	layout->addLayout(form_layout);

	// Make the current focus the initial selection when it is one of the choices.
	if (d_feature_focus.associated_reconstruction_geometry())
	{
		boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> focused_rfg =
				GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
						const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
							d_feature_focus.associated_reconstruction_geometry());
		if (focused_rfg && (*focused_rfg)->property().is_still_valid())
		{
			for (size_t choice_index = 0; choice_index < polygon_choices.size(); ++choice_index)
			{
				if (polygon_choices[choice_index].geometry->property() == (*focused_rfg)->property())
				{
					polygon_combo->setCurrentIndex(static_cast<int>(choice_index));
				}
			}
			for (size_t choice_index = 0; choice_index < polyline_choices.size(); ++choice_index)
			{
				if (polyline_choices[choice_index].geometry->property() == (*focused_rfg)->property())
				{
					polyline_combo->setCurrentIndex(static_cast<int>(choice_index));
				}
			}
		}
	}

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Split"));
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED, QObject::tr("Split Plate closed without changes."));
	}

	const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type polygon_rfg =
			polygon_choices[polygon_combo->currentIndex()].geometry;
	const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type polyline_rfg =
			polyline_choices[polyline_combo->currentIndex()].geometry;
	if (!polygon_rfg->is_valid() || !polygon_rfg->property().is_still_valid() ||
			!polyline_rfg->is_valid() || !polyline_rfg->property().is_still_valid())
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr("One of the selected geometries changed while the Split Plate window was open."));
	}

	const GPlatesMaths::PolygonOnSphere *polygon =
			dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(polygon_rfg->reconstructed_geometry().get());
	const GPlatesMaths::PolylineOnSphere *polyline =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(polyline_rfg->reconstructed_geometry().get());
	if (!polygon || !polyline)
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr("The selected inputs are no longer a polygon and a polyline."));
	}

	QString error_message;
	boost::optional<SplitPolygonResult> split_result;
	try
	{
		split_result = split_polygon(
				error_message,
				*polygon,
				*polyline,
				*polygon_rfg);
	}
	catch (const std::exception &exception)
	{
		error_message = QObject::tr("Could not construct the two output polygons: %1").arg(exception.what());
	}
	catch (...)
	{
		error_message = QObject::tr("Could not construct the two output polygons because the geometry is invalid.");
	}

	if (!split_result)
	{
		return Result(OPERATION_ERROR, error_message);
	}

	std::unique_ptr<QUndoCommand> undo_command(
			new SplitPlateUndoCommand(
					d_feature_focus,
					d_model_interface,
					polygon_rfg->get_feature_ref(),
					polygon_rfg->property(),
					split_result->polygon1,
					split_result->polygon2));

	UndoRedo::instance().get_active_undo_stack().push(undo_command.release());

	return Result(
			SPLIT_COMPLETED,
			QObject::tr(
					"Plate split completed. The original feature is one polygon and a cloned feature is the other. "
					"Use Edit > Undo to put them back together."));
}
