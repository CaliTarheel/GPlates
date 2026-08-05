/* $Id$ */

/**
 * \file
 * Persistent polygon Boolean workflow for World Building.
 */

#include "BooleanPolygonOperation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include <QObject>
#include <QStringList>
#include <QUndoCommand>

#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "feature-visitors/GeometrySetter.h"

#include "gui/FeatureFocus.h"
#include "gui/Colour.h"

#include "maths/MathsUtils.h"
#include "maths/PointOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/NotificationGuard.h"
#include "model/TopLevelProperty.h"

#include "presentation/ViewState.h"


namespace
{
	typedef GPlatesViewOperations::SubductionCutterGeometry::polygon_ptr_type polygon_ptr_type;
	typedef GPlatesViewOperations::SubductionCutterGeometry::polygon_seq_type polygon_seq_type;
	typedef std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> feature_seq_type;
	typedef std::vector<GPlatesModel::TopLevelProperty::non_null_ptr_type> property_seq_type;

	GPlatesMaths::PointOnSphere
	reverse_reconstruct_point(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &reconstruction)
	{
		const boost::optional<GPlatesModel::integer_plate_id_type> &plate_id =
				reconstruction.reconstruction_plate_id();
		if (!plate_id)
		{
			return point;
		}
		return GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
				point, *plate_id, *reconstruction.get_reconstruction_tree(), true);
	}

	polygon_ptr_type
	reverse_reconstruct_polygon(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &reconstruction)
	{
		std::vector<GPlatesMaths::PointOnSphere> exterior;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
				polygon.exterior_ring_vertex_begin();
				point_iter != polygon.exterior_ring_vertex_end(); ++point_iter)
		{
			exterior.push_back(reverse_reconstruct_point(*point_iter, reconstruction));
		}

		std::vector< std::vector<GPlatesMaths::PointOnSphere> > interiors;
		for (unsigned int ring_index = 0;
				ring_index < polygon.number_of_interior_rings(); ++ring_index)
		{
			interiors.push_back(std::vector<GPlatesMaths::PointOnSphere>());
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
					polygon.interior_ring_vertex_begin(ring_index);
					point_iter != polygon.interior_ring_vertex_end(ring_index); ++point_iter)
			{
				interiors.back().push_back(
						reverse_reconstruct_point(*point_iter, reconstruction));
			}
		}
		return GPlatesMaths::PolygonOnSphere::create(
				exterior.begin(), exterior.end(), interiors.begin(), interiors.end(), true);
	}

	GPlatesModel::TopLevelProperty::non_null_ptr_type
	geometry_property(
			const GPlatesModel::FeatureHandle::iterator &source_property,
			const polygon_ptr_type &polygon)
	{
		GPlatesModel::TopLevelProperty::non_null_ptr_type property =
				(*source_property)->clone();
		GPlatesFeatureVisitors::GeometrySetter setter(polygon);
		setter.set_geometry(property.get());
		return property;
	}

	class LargerPolygon
	{
	public:
		bool operator()(const polygon_ptr_type &lhs, const polygon_ptr_type &rhs) const
		{
			return lhs->get_area().dval() > rhs->get_area().dval();
		}
	};

	class BooleanPolygonUndoCommand : public QUndoCommand
	{
	public:
		BooleanPolygonUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const GPlatesModel::FeatureCollectionHandle::iterator &collection_iterator,
				const GPlatesModel::FeatureHandle::iterator &geometry_property_iterator,
				const polygon_seq_type &output_polygons,
				const QString &operation_name) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_collection(collection),
			d_collection_iterator(collection_iterator),
			d_primary_feature(*collection_iterator),
			d_geometry_property_iterator(geometry_property_iterator),
			d_original_geometry_property((*geometry_property_iterator)->clone()),
			d_remove_primary(output_polygons.empty())
		{
			setText(QObject::tr("%1 polygons").arg(operation_name));
			if (d_remove_primary)
			{
				return;
			}

			d_primary_output_property = geometry_property(
					d_geometry_property_iterator, output_polygons.front());
			for (unsigned int output_index = 1;
					output_index < output_polygons.size(); ++output_index)
			{
				GPlatesModel::FeatureHandle::non_null_ptr_type clone =
						GPlatesModel::FeatureHandle::create(d_primary_feature->feature_type());
				GPlatesModel::FeatureHandle::iterator clone_geometry_property;
				for (GPlatesModel::FeatureHandle::const_iterator property_iter =
						d_primary_feature->begin();
						property_iter != d_primary_feature->end(); ++property_iter)
				{
					const GPlatesModel::FeatureHandle::iterator clone_property =
							clone->add((*property_iter)->clone());
					if ((*property_iter).get() == (*d_geometry_property_iterator).get())
					{
						clone_geometry_property = clone_property;
					}
				}
				if (!clone_geometry_property.is_still_valid())
				{
					throw std::runtime_error("Could not find the cloned Boolean geometry property.");
				}
				clone->set(clone_geometry_property,
						geometry_property(clone_geometry_property, output_polygons[output_index]));
				d_clones.push_back(clone);
			}
		}

		virtual void redo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			d_feature_focus.unset_focus();
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_remove_primary)
			{
				if (d_collection_iterator.is_still_valid())
				{
					d_primary_feature = d_collection->remove(d_collection_iterator);
				}
			}
			else if (d_geometry_property_iterator.is_still_valid())
			{
				if (!d_primary_output_property)
				{
					return;
				}
				d_primary_feature->set(
						d_geometry_property_iterator, (*d_primary_output_property)->clone());
				for (feature_seq_type::const_iterator clone_iter = d_clones.begin();
						clone_iter != d_clones.end(); ++clone_iter)
				{
					if (!(*clone_iter)->parent_ptr())
					{
						d_collection->add(*clone_iter);
					}
				}
			}
			guard.release_guard();
		}

		virtual void undo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			d_feature_focus.unset_focus();
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_remove_primary)
			{
				if (!d_primary_feature->parent_ptr())
				{
					d_collection_iterator = d_collection->add(d_primary_feature);
				}
			}
			else
			{
				for (feature_seq_type::const_iterator clone_iter = d_clones.begin();
						clone_iter != d_clones.end(); ++clone_iter)
				{
					if ((*clone_iter)->parent_ptr())
					{
						(*clone_iter)->remove_from_parent();
					}
				}
				if (d_geometry_property_iterator.is_still_valid())
				{
					d_primary_feature->set(
							d_geometry_property_iterator, d_original_geometry_property->clone());
				}
			}
			guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		GPlatesModel::FeatureCollectionHandle::iterator d_collection_iterator;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_primary_feature;
		GPlatesModel::FeatureHandle::iterator d_geometry_property_iterator;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_original_geometry_property;
		boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> d_primary_output_property;
		feature_seq_type d_clones;
		bool d_remove_primary;
	};

	QString operation_name(
			GPlatesViewOperations::SubductionCutterGeometry::BooleanOperation operation)
	{
		switch (operation)
		{
		case GPlatesViewOperations::SubductionCutterGeometry::POLYGON_UNION:
			return QObject::tr("union");
		case GPlatesViewOperations::SubductionCutterGeometry::POLYGON_DIFFERENCE:
			return QObject::tr("subtract");
		case GPlatesViewOperations::SubductionCutterGeometry::POLYGON_INTERSECTION:
			return QObject::tr("intersect");
		case GPlatesViewOperations::SubductionCutterGeometry::POLYGON_SYMMETRIC_DIFFERENCE:
			return QObject::tr("symmetric-difference");
		}
		return QObject::tr("Boolean-edit");
	}
}


GPlatesViewOperations::BooleanPolygonOperation::BooleanPolygonOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_model_interface(application_state.get_model_interface()),
	d_preview_layer(
			view_state.get_rendered_geometry_collection().
					create_child_rendered_layer_and_transfer_ownership(
							RenderedGeometryCollection::RECONSTRUCTION_LAYER)),
	d_selection_mode(NOT_SELECTING),
	d_preview_ready(false),
	d_preview_operation(SubductionCutterGeometry::POLYGON_UNION)
{
	d_preview_layer->set_active(true);
}


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::arm_first_selection()
{
	clear_preview();
	d_selection_mode = SELECTING_FIRST;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the first polygon on the globe or map. Its feature properties will be kept."));
}


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::arm_operand_selection()
{
	if (!d_first)
	{
		return Result(OPERATION_ERROR, QObject::tr("Select the first polygon before adding operands."));
	}
	clear_preview();
	d_selection_mode = SELECTING_OPERAND;
	return Result(SELECTION_ARMED,
			QObject::tr("Select an operand polygon. Repeat this button to add more polygons."));
}


boost::optional<GPlatesViewOperations::BooleanPolygonOperation::CapturedPolygon>
GPlatesViewOperations::BooleanPolygonOperation::focused_polygon(QString &error) const
{
	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		error = QObject::tr("The selection has no editable reconstructed polygon geometry.");
		return boost::none;
	}

	const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> rfg =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!rfg)
	{
		error = QObject::tr("Boolean Polygons supports rigid reconstructed feature polygons only.");
		return boost::none;
	}

	const GPlatesMaths::PolygonOnSphere *polygon =
			dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
					(*rfg)->reconstructed_geometry().get());
	if (!polygon || !(*rfg)->property().is_still_valid())
	{
		error = QObject::tr("Select a polygon backed by an editable geometry property.");
		return boost::none;
	}

	return CapturedPolygon(
			d_feature_focus.focused_feature(),
			(*rfg)->property(),
			(*rfg)->get_non_null_pointer_to_const(),
			polygon->get_non_null_pointer(),
			d_application_state.get_current_reconstruction().get_reconstruction_time());
}


bool
GPlatesViewOperations::BooleanPolygonOperation::already_captured(
		const CapturedPolygon &candidate) const
{
	if (d_first && d_first->feature == candidate.feature &&
			(*d_first->geometry_property).get() == (*candidate.geometry_property).get())
	{
		return true;
	}
	for (std::vector<CapturedPolygon>::const_iterator operand_iter = d_operands.begin();
			operand_iter != d_operands.end(); ++operand_iter)
	{
		if (operand_iter->feature == candidate.feature &&
				(*operand_iter->geometry_property).get() == (*candidate.geometry_property).get())
		{
			return true;
		}
	}
	return false;
}


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::capture_armed_selection()
{
	if (d_selection_mode == NOT_SELECTING)
	{
		return Result(OPERATION_CANCELLED, QString());
	}

	QString error;
	const boost::optional<CapturedPolygon> selected = focused_polygon(error);
	if (!selected)
	{
		return Result(OPERATION_ERROR, error + QObject::tr(" Selection remains armed."));
	}

	if (d_selection_mode == SELECTING_FIRST)
	{
		clear_preview();
		d_first = *selected;
		d_operands.clear();
		d_selection_mode = NOT_SELECTING;
		return Result(FIRST_CAPTURED,
				QObject::tr("First polygon captured. It supplies all output feature properties. Add one or more operands."));
	}

	if (std::fabs(selected->reconstruction_time - d_first->reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The View time changed after the first selection. Return to %1 Ma or select the first polygon again.")
						.arg(d_first->reconstruction_time, 0, 'f', 2));
	}
	if (already_captured(*selected))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That polygon is already part of this Boolean operation. Selection remains armed."));
	}

	d_operands.push_back(*selected);
	clear_preview();
	d_selection_mode = NOT_SELECTING;
	return Result(OPERAND_CAPTURED,
			QObject::tr("Operand %1 captured. Add another operand or apply the operation.")
					.arg(d_operands.size()));
}


bool
GPlatesViewOperations::BooleanPolygonOperation::calculate_boolean(
		SubductionCutterGeometry::BooleanOperation operation,
		SubductionCutterGeometry::BooleanResult &boolean_result,
		QString &error) const
{
	if (!d_first || d_operands.empty())
	{
		error = QObject::tr("Select a first polygon and at least one operand before previewing the operation.");
		return false;
	}
	if (!d_first->feature.is_valid() || !d_first->geometry_property.is_still_valid() ||
			!d_first->feature->parent_ptr())
	{
		error = QObject::tr("The first polygon is no longer editable. Select it again.");
		return false;
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_first->reconstruction_time) > 1e-9)
	{
		error = QObject::tr("Return to %1 Ma before previewing, or select the polygons again at the current time.")
				.arg(d_first->reconstruction_time, 0, 'f', 2);
		return false;
	}

	polygon_seq_type operands;
	for (std::vector<CapturedPolygon>::const_iterator operand_iter = d_operands.begin();
			operand_iter != d_operands.end(); ++operand_iter)
	{
		if (!operand_iter->feature.is_valid() || !operand_iter->geometry_property.is_still_valid())
		{
			error = QObject::tr("An operand is no longer available. Remove or reselect the operands.");
			return false;
		}
		operands.push_back(operand_iter->polygon);
	}

	try
	{
		boolean_result = SubductionCutterGeometry::apply_polygon_boolean(
				*d_first->polygon, operands, operation);
	}
	catch (const std::exception &exception)
	{
		error = QObject::tr("The polygon Boolean operation failed: %1").arg(exception.what());
		return false;
	}
	catch (...)
	{
		error = QObject::tr("The polygon Boolean operation failed on invalid geometry.");
		return false;
	}
	if (!boolean_result.success)
	{
		error = boolean_result.error;
		return false;
	}
	std::sort(boolean_result.polygons.begin(), boolean_result.polygons.end(), LargerPolygon());
	return true;
}


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::preview(
		SubductionCutterGeometry::BooleanOperation operation)
{
	clear_preview();
	SubductionCutterGeometry::BooleanResult boolean_result;
	QString error;
	if (!calculate_boolean(operation, boolean_result, error))
	{
		return Result(OPERATION_ERROR, error);
	}

	double total_area_steradians = 0.0;
	for (polygon_seq_type::const_iterator polygon_iter = boolean_result.polygons.begin();
			polygon_iter != boolean_result.polygons.end(); ++polygon_iter)
	{
		total_area_steradians += (*polygon_iter)->get_area().dval();
		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						*polygon_iter,
						GPlatesGui::Colour::get_green(),
						3.0f,
						true,
						GPlatesGui::Colour(0.0f, 0.35f, 0.10f)));
	}
	d_preview_ready = true;
	d_preview_operation = operation;
	const double sphere_percent =
			100.0 * total_area_steradians / (4.0 * GPlatesMaths::PI);
	return Result(PREVIEW_READY,
			boolean_result.polygons.empty()
					? QObject::tr("Preview is empty: applying will remove the first feature geometry. Operand features will remain unchanged.")
					: QObject::tr("Green preview: %1 output component(s), covering %2% of the sphere. The first feature supplies output properties; operands remain unchanged.")
							.arg(boolean_result.polygons.size())
							.arg(sphere_percent, 0, 'f', 3));
}


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::apply(
		SubductionCutterGeometry::BooleanOperation operation)
{
	if (!d_preview_ready || d_preview_operation != operation)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Preview the currently selected operation before applying it."));
	}

	SubductionCutterGeometry::BooleanResult boolean_result;
	QString error;
	if (!calculate_boolean(operation, boolean_result, error))
	{
		clear_preview();
		return Result(OPERATION_ERROR,
				error + QObject::tr(" Preview invalidated; review the inputs and preview again."));
	}

	polygon_seq_type stored_polygons;
	for (polygon_seq_type::const_iterator polygon_iter = boolean_result.polygons.begin();
			polygon_iter != boolean_result.polygons.end(); ++polygon_iter)
	{
		stored_polygons.push_back(reverse_reconstruct_polygon(
				**polygon_iter, *d_first->reconstructed_feature_geometry));
	}

	GPlatesModel::FeatureCollectionHandle *collection = d_first->feature->parent_ptr();
	GPlatesModel::FeatureCollectionHandle::iterator collection_iterator = collection->end();
	for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
			feature_iter != collection->end(); ++feature_iter)
	{
		if ((*feature_iter)->reference() == d_first->feature)
		{
			collection_iterator = feature_iter;
			break;
		}
	}
	if (collection_iterator == collection->end())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The first polygon could not be found in its feature collection."));
	}

	const unsigned int output_count = static_cast<unsigned int>(stored_polygons.size());
	const unsigned int input_count = static_cast<unsigned int>(d_operands.size() + 1);
	std::unique_ptr<QUndoCommand> command(new BooleanPolygonUndoCommand(
			d_feature_focus, d_model_interface, collection->reference(), collection_iterator,
			d_first->geometry_property, stored_polygons, operation_name(operation)));
	reset();
	UndoRedo::instance().get_active_undo_stack().push(command.release());

	return Result(BOOLEAN_COMPLETED,
			QObject::tr("Polygon %1 complete: %2 input polygon(s) produced %3 output polygon(s). "
					"Outputs keep the first feature's properties; operand features were not changed. Use Edit > Undo to revert.")
					.arg(operation_name(operation)).arg(input_count).arg(output_count));
}


void
GPlatesViewOperations::BooleanPolygonOperation::remove_last_operand()
{
	if (!d_operands.empty())
	{
		d_operands.pop_back();
	}
	d_selection_mode = NOT_SELECTING;
	clear_preview();
}


void
GPlatesViewOperations::BooleanPolygonOperation::clear_operands()
{
	d_operands.clear();
	d_selection_mode = NOT_SELECTING;
	clear_preview();
}


void
GPlatesViewOperations::BooleanPolygonOperation::clear_preview()
{
	d_preview_layer->clear_rendered_geometries();
	d_preview_ready = false;
}


void
GPlatesViewOperations::BooleanPolygonOperation::reset()
{
	clear_preview();
	d_selection_mode = NOT_SELECTING;
	d_first = boost::none;
	d_operands.clear();
}


QString
GPlatesViewOperations::BooleanPolygonOperation::first_status() const
{
	if (!d_first)
	{
		return QObject::tr("No first polygon selected.");
	}
	return QObject::tr("First: %1 at %2 Ma (properties retained)")
			.arg(d_first->feature->feature_id().get().qstring())
			.arg(d_first->reconstruction_time, 0, 'f', 2);
}


QString
GPlatesViewOperations::BooleanPolygonOperation::operands_status() const
{
	if (d_operands.empty())
	{
		return QObject::tr("No operand polygons selected.");
	}
	QStringList lines;
	for (std::vector<CapturedPolygon>::const_iterator operand_iter = d_operands.begin();
			operand_iter != d_operands.end(); ++operand_iter)
	{
		lines.append(QObject::tr("%1. %2")
				.arg(lines.size() + 1)
				.arg(operand_iter->feature->feature_id().get().qstring()));
	}
	return QObject::tr("Operands remain unchanged:\n%1").arg(lines.join("\n"));
}
