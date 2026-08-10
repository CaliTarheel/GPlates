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

#include <QDebug>
#include <QObject>
#include <QStringList>
#include <QUndoCommand>

#include "RenderedGeometryCollection.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "feature-visitors/GeometrySetter.h"

#include "gui/FeatureFocus.h"

#include "maths/MathsUtils.h"
#include "maths/PointOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/NotificationGuard.h"
#include "model/TopLevelProperty.h"


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
				const QString &operation_name,
				const feature_seq_type &operands_to_remove = feature_seq_type()) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_collection(collection),
			d_collection_iterator(collection_iterator),
			d_primary_feature(*collection_iterator),
			d_geometry_property_iterator(geometry_property_iterator),
			d_original_geometry_property((*geometry_property_iterator)->clone()),
			d_remove_primary(output_polygons.empty()),
			d_operands_to_remove(operands_to_remove)
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
			qDebug() << "BooleanPolygonUndoCommand::redo -" << (d_remove_primary ? "removing primary feature (empty result)" : "replacing primary geometry, adding clones");
			if (d_remove_primary)
			{
				if (d_collection_iterator.is_still_valid())
				{
					d_primary_feature = d_collection->remove(d_collection_iterator);
					qDebug() << "  removed primary feature:" << d_primary_feature->feature_id().get().qstring();
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
				qDebug() << "  replaced geometry on primary feature:" << d_primary_feature->feature_id().get().qstring();
				for (feature_seq_type::const_iterator clone_iter = d_clones.begin();
						clone_iter != d_clones.end(); ++clone_iter)
				{
					if (!(*clone_iter)->parent_ptr())
					{
						d_collection->add(*clone_iter);
						qDebug() << "  added clone feature:" << (*clone_iter)->feature_id().get().qstring();
					}
				}
			}
			if (d_operands_to_remove.empty())
			{
				qDebug() << "  (no operand feature is referenced or touched by this command)";
			}
			for (feature_seq_type::const_iterator remove_iter = d_operands_to_remove.begin();
					remove_iter != d_operands_to_remove.end(); ++remove_iter)
			{
				if ((*remove_iter)->parent_ptr())
				{
					qDebug() << "  removed absorbed operand feature:" << (*remove_iter)->feature_id().get().qstring();
					(*remove_iter)->remove_from_parent();
				}
			}
			guard.release_guard();

			// Boolean operations explicitly drop focus above (to avoid touching the model
			// while a feature is focused) but never restored it, leaving GeometryBuilder-backed
			// tools (Move Vertex, etc) looking at whatever was focused before this ran - stale
			// or empty - instead of the fresh result. Re-focus the primary feature now that the
			// model has settled, so its geometry is what those tools actually see next.
			if (!d_remove_primary && d_geometry_property_iterator.is_still_valid())
			{
				d_feature_focus.set_focus(d_primary_feature->reference(), d_geometry_property_iterator);
			}
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
			qDebug() << "BooleanPolygonUndoCommand::undo -" << (d_remove_primary ? "restoring primary feature" : "restoring primary geometry, removing clones");
			if (d_remove_primary)
			{
				if (!d_primary_feature->parent_ptr())
				{
					d_collection_iterator = d_collection->add(d_primary_feature);
					qDebug() << "  restored primary feature:" << d_primary_feature->feature_id().get().qstring();
				}
			}
			else
			{
				for (feature_seq_type::const_iterator clone_iter = d_clones.begin();
						clone_iter != d_clones.end(); ++clone_iter)
				{
					if ((*clone_iter)->parent_ptr())
					{
						qDebug() << "  removed clone feature:" << (*clone_iter)->feature_id().get().qstring();
						(*clone_iter)->remove_from_parent();
					}
				}
				if (d_geometry_property_iterator.is_still_valid())
				{
					d_primary_feature->set(
							d_geometry_property_iterator, d_original_geometry_property->clone());
					qDebug() << "  restored original geometry on primary feature:" << d_primary_feature->feature_id().get().qstring();
				}
			}
			for (feature_seq_type::const_reverse_iterator remove_iter = d_operands_to_remove.rbegin();
					remove_iter != d_operands_to_remove.rend(); ++remove_iter)
			{
				if (!(*remove_iter)->parent_ptr())
				{
					d_collection->add(*remove_iter);
					qDebug() << "  restored absorbed operand feature:" << (*remove_iter)->feature_id().get().qstring();
				}
			}
			guard.release_guard();

			// Mirror the redo() refocus (see there for why) so undo leaves the primary feature
			// focused on its restored geometry too, instead of stuck unfocused either way.
			if (d_primary_feature->parent_ptr() && d_geometry_property_iterator.is_still_valid())
			{
				d_feature_focus.set_focus(d_primary_feature->reference(), d_geometry_property_iterator);
			}
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
		feature_seq_type d_operands_to_remove;
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
		GPlatesAppLogic::ApplicationState &application_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_model_interface(application_state.get_model_interface()),
	d_selection_mode(NOT_SELECTING)
{  }


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::arm_first_selection()
{
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
	d_selection_mode = NOT_SELECTING;
	return Result(OPERAND_CAPTURED,
			QObject::tr("Operand %1 captured. Add another operand or apply the operation.")
					.arg(d_operands.size()));
}


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::apply(
		SubductionCutterGeometry::BooleanOperation operation)
{
	if (!d_first || d_operands.empty())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select a first polygon and at least one operand before applying the operation."));
	}
	if (!d_first->feature.is_valid() || !d_first->geometry_property.is_still_valid() ||
			!d_first->feature->parent_ptr())
	{
		reset();
		return Result(OPERATION_ERROR,
				QObject::tr("The first polygon is no longer editable. The Boolean selection was reset."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_first->reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Return to %1 Ma before applying, or select the polygons again at the current time.")
						.arg(d_first->reconstruction_time, 0, 'f', 2));
	}

	polygon_seq_type operands;
	for (std::vector<CapturedPolygon>::const_iterator operand_iter = d_operands.begin();
			operand_iter != d_operands.end(); ++operand_iter)
	{
		if (!operand_iter->feature.is_valid() || !operand_iter->geometry_property.is_still_valid())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("An operand is no longer available. Clear the operands and select them again."));
		}
		operands.push_back(operand_iter->polygon);
	}

	SubductionCutterGeometry::BooleanResult boolean_result;
	try
	{
		boolean_result = SubductionCutterGeometry::apply_polygon_boolean(
				*d_first->polygon, operands, operation);
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The polygon Boolean operation failed: %1").arg(exception.what()));
	}
	catch (...)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The polygon Boolean operation failed on invalid geometry."));
	}
	if (!boolean_result.success)
	{
		return Result(OPERATION_ERROR, boolean_result.error);
	}

	std::sort(boolean_result.polygons.begin(), boolean_result.polygons.end(), LargerPolygon());
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

	// Union fully absorbs its operands into the result, so they are removed too - unlike
	// Subtract/Intersect/Symmetric-Difference, where every operand keeps a role (cutting tool,
	// or simply not fully consumed) and must stay untouched.
	const bool absorbs_operands = (operation == SubductionCutterGeometry::POLYGON_UNION);
	feature_seq_type operands_to_remove;
	if (absorbs_operands)
	{
		for (std::vector<CapturedPolygon>::const_iterator operand_iter = d_operands.begin();
				operand_iter != d_operands.end(); ++operand_iter)
		{
			operands_to_remove.push_back(
					GPlatesModel::FeatureHandle::non_null_ptr_type(operand_iter->feature.handle_ptr()));
		}
	}

	// Diagnostic logging for SR1b - to verify against reports of an operand disappearing after
	// Subtract. Nothing below this line modifies the model; it only records what is about to
	// happen so it can be checked against what was actually observed afterwards.
	qDebug() << "BooleanPolygonOperation::apply -" << operation_name(operation)
			<< "- first feature:" << d_first->feature->feature_id().get().qstring();
	for (std::vector<CapturedPolygon>::const_iterator operand_iter = d_operands.begin();
			operand_iter != d_operands.end(); ++operand_iter)
	{
		qDebug() << "  operand feature:" << operand_iter->feature->feature_id().get().qstring();
	}
	qDebug() << "  input_count:" << input_count << "output_count:" << output_count
			<< "primary_will_be_removed:" << (output_count == 0)
			<< "operands_will_be_removed:" << absorbs_operands;

	std::unique_ptr<QUndoCommand> command(new BooleanPolygonUndoCommand(
			d_feature_focus, d_model_interface, collection->reference(), collection_iterator,
			d_first->geometry_property, stored_polygons, operation_name(operation),
			operands_to_remove));
	reset();
	UndoRedo::instance().get_active_undo_stack().push(command.release());

	return Result(BOOLEAN_COMPLETED, absorbs_operands
			? QObject::tr("Polygon %1 complete: %2 input polygon(s) produced %3 output polygon(s). "
					"Outputs keep the first feature's properties; the %4 absorbed operand(s) were removed. Use Edit > Undo to revert.")
					.arg(operation_name(operation)).arg(input_count).arg(output_count).arg(static_cast<unsigned int>(d_operands.size()))
			: QObject::tr("Polygon %1 complete: %2 input polygon(s) produced %3 output polygon(s). "
					"Outputs keep the first feature's properties; operand features were not changed. Use Edit > Undo to revert.")
					.arg(operation_name(operation)).arg(input_count).arg(output_count));
}


GPlatesViewOperations::BooleanPolygonOperation::Result
GPlatesViewOperations::BooleanPolygonOperation::apply_unify()
{
	if (!d_first)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select a first polygon before unifying matching polygons."));
	}
	if (!d_first->feature.is_valid() || !d_first->geometry_property.is_still_valid() ||
			!d_first->feature->parent_ptr())
	{
		reset();
		return Result(OPERATION_ERROR,
				QObject::tr("The first polygon is no longer editable. The Boolean selection was reset."));
	}

	GPlatesAppLogic::ReconstructionFeatureProperties first_properties;
	first_properties.visit_feature(d_first->feature);
	if (!first_properties.get_recon_plate_id())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The first polygon has no Plate ID, so nothing can be matched against it."));
	}
	const GPlatesModel::integer_plate_id_type first_plate_id = *first_properties.get_recon_plate_id();
	const GPlatesAppLogic::ReconstructionFeatureProperties::TimePeriod &first_valid_time =
			first_properties.get_valid_time();

	const GPlatesModel::FeatureCollectionHandle *first_collection = d_first->feature->parent_ptr();

	// The Boolean clipping engine projects every operand using a Lambert azimuthal equal-area
	// projection centred on the FIRST polygon alone (see SubductionCutterGeometry.cc). That
	// projection is only well-behaved within a limited angular distance of its centre; far
	// enough away the projected shape becomes severely distorted (though project() does not
	// report failure until much further out, near the true antipode), which can make a real
	// operand's contribution to the union come out wrong or negligible - and since Union now
	// removes absorbed operands, a distant match could vanish without actually being added.
	// Plate ID and age range alone say nothing about geographic proximity (a plate can have
	// disjoint pieces, or the "match" could be coincidental), so cap it explicitly here.
	const double MAXIMUM_UNIFY_MATCH_DISTANCE_DEGREES = 60.0;
	const GPlatesMaths::PointOnSphere first_centroid(d_first->polygon->get_boundary_centroid());
	QStringList skipped_distant_matches;

	// Gather every currently reconstructed geometry across every active layer - the match has
	// to be found by feature collection, not by which layer happened to produce it.
	std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layer_proxies;
	d_application_state.get_current_reconstruction().get_active_layer_outputs<
			GPlatesAppLogic::ReconstructLayerProxy>(layer_proxies);

	std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
	for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
			proxy_iter = layer_proxies.begin(); proxy_iter != layer_proxies.end(); ++proxy_iter)
	{
		(*proxy_iter)->get_reconstructed_feature_geometries(geometries);
	}

	std::vector<CapturedPolygon> matches;
	for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
			geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
	{
		const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
		if (!rfg.is_valid() || !rfg.property().is_still_valid())
		{
			continue;
		}

		const GPlatesModel::FeatureHandle::weak_ref feature_ref = rfg.get_feature_ref();
		if (!feature_ref.is_valid() || feature_ref->parent_ptr() != first_collection)
		{
			continue;
		}

		// Skip the first polygon itself - it is not its own operand.
		if (feature_ref == d_first->feature &&
				(*rfg.property()).get() == (*d_first->geometry_property).get())
		{
			continue;
		}

		const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(rfg.reconstructed_geometry().get());
		if (!polygon)
		{
			continue;
		}

		GPlatesAppLogic::ReconstructionFeatureProperties candidate_properties;
		candidate_properties.visit_feature(feature_ref);
		if (!candidate_properties.get_recon_plate_id() ||
				*candidate_properties.get_recon_plate_id() != first_plate_id)
		{
			continue;
		}

		// "Matching age range" means the appearance and disappearance times agree exactly -
		// both absent (always valid) counts as a match; one absent and the other not does not.
		const GPlatesAppLogic::ReconstructionFeatureProperties::TimePeriod &candidate_valid_time =
				candidate_properties.get_valid_time();
		const bool appearance_matches =
				(!first_valid_time.time_of_appearance && !candidate_valid_time.time_of_appearance) ||
				(first_valid_time.time_of_appearance && candidate_valid_time.time_of_appearance &&
						first_valid_time.time_of_appearance->is_coincident_with(
								*candidate_valid_time.time_of_appearance));
		const bool disappearance_matches =
				(!first_valid_time.time_of_disappearance && !candidate_valid_time.time_of_disappearance) ||
				(first_valid_time.time_of_disappearance && candidate_valid_time.time_of_disappearance &&
						first_valid_time.time_of_disappearance->is_coincident_with(
								*candidate_valid_time.time_of_disappearance));
		if (!appearance_matches || !disappearance_matches)
		{
			continue;
		}

		const GPlatesMaths::PointOnSphere candidate_centroid(polygon->get_boundary_centroid());
		const double distance_degrees = GPlatesMaths::convert_rad_to_deg(
				GPlatesMaths::calculate_distance_on_surface_of_sphere(
						first_centroid, candidate_centroid, 1.0).dval());
		if (distance_degrees > MAXIMUM_UNIFY_MATCH_DISTANCE_DEGREES)
		{
			skipped_distant_matches << QObject::tr("%1 (%2 degrees away)")
					.arg(feature_ref->feature_id().get().qstring()).arg(distance_degrees, 0, 'f', 1);
			continue;
		}

		matches.push_back(CapturedPolygon(
				feature_ref,
				rfg.property(),
				rfg.get_non_null_pointer_to_const(),
				polygon->get_non_null_pointer(),
				d_first->reconstruction_time));
	}

	if (matches.empty())
	{
		return Result(OPERATION_ERROR, skipped_distant_matches.isEmpty()
				? QObject::tr("No other polygons in this layer share Plate ID %1 and this age range.")
						.arg(first_plate_id)
				: QObject::tr("%1 other polygon(s) share Plate ID %2 and this age range, but all are"
						" more than %3 degrees away and were not matched (a same-plate match that far"
						" from the first polygon is unlikely to union correctly): %4")
						.arg(skipped_distant_matches.size()).arg(first_plate_id)
						.arg(MAXIMUM_UNIFY_MATCH_DISTANCE_DEGREES, 0, 'f', 0)
						.arg(skipped_distant_matches.join("; ")));
	}

	d_operands = matches;
	const Result result = apply(SubductionCutterGeometry::POLYGON_UNION);
	if (result.outcome != BOOLEAN_COMPLETED || skipped_distant_matches.isEmpty())
	{
		return result;
	}
	return Result(result.outcome, result.message + QObject::tr(
			" (%1 other same-plate/age match(es) were more than %2 degrees away and were not"
			" included: %3)")
					.arg(skipped_distant_matches.size())
					.arg(MAXIMUM_UNIFY_MATCH_DISTANCE_DEGREES, 0, 'f', 0)
					.arg(skipped_distant_matches.join("; ")));
}


void
GPlatesViewOperations::BooleanPolygonOperation::clear_operands()
{
	d_operands.clear();
	d_selection_mode = NOT_SELECTING;
}


void
GPlatesViewOperations::BooleanPolygonOperation::reset()
{
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
	return d_operands.empty()
			? QObject::tr("No operand polygons selected.")
			: QObject::tr("%1 operand polygon(s) selected; operands remain unchanged.")
					.arg(d_operands.size());
}
