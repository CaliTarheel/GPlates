/* $Id$ */

/**
 * \file
 * Implements the Worldbuilding Pasta opposite-margin subduction step.
 */

#include "GenerateInitialSubductionOperation.h"

#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QMessageBox>
#include <QObject>
#include <QUndoCommand>

#include "InitialSubductionGeometry.h"
#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "UndoRedo.h"
#include "WorldbuildingFeatureCollectionUtils.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "feature-visitors/PropertyValueFinder.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "model/FeatureCollectionHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/EnumerationType.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	bool
	is_half_stage_mor(
			const GPlatesModel::FeatureHandle::weak_ref &feature)
	{
		static const GPlatesModel::FeatureType MID_OCEAN_RIDGE =
				GPlatesModel::FeatureType::create_gpml("MidOceanRidge");
		if (!feature.is_valid() || feature->feature_type() != MID_OCEAN_RIDGE)
		{
			return false;
		}

		const boost::optional<GPlatesPropertyValues::Enumeration::non_null_ptr_to_const_type>
				reconstruction_method =
					GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::Enumeration>(
							feature,
							GPlatesModel::PropertyName::create_gpml("reconstructionMethod"));
		return reconstruction_method &&
				(*reconstruction_method)->get_value() ==
						GPlatesPropertyValues::EnumerationContent("HalfStageRotationVersion3");
	}

	void
	set_required_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &property_name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::set_property(feature, property_name, value))
		{
			throw std::runtime_error(QString("Unable to set required property '%1'.")
					.arg(property_name.get_name().qstring()).toStdString());
		}
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type
	create_subduction_zone(
			const QString &name,
			double start_time,
			GPlatesModel::integer_plate_id_type overriding_plate,
			const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("SubductionZone"));
		const GPlatesModel::FeatureHandle::weak_ref feature_ref = feature->reference();
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(start_time),
						GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(overriding_plate));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
				GPlatesModel::ModelUtils::create_gml_time_instant(
						GPlatesPropertyValues::GeoTimeInstant(start_time)));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gpml("subductionPolarity"),
				GPlatesPropertyValues::Enumeration::create(
						GPlatesPropertyValues::EnumerationType::create_gpml("SubductionPolarityEnumeration"),
						"Left"));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gpml("leftPlate"),
				GPlatesPropertyValues::GpmlPlateId::create(overriding_plate));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gpml("centerLineOf"),
				GPlatesAppLogic::GeometryUtils::create_polyline_geometry_property_value(polyline));
		return feature;
	}

	class CreateSubductionUndoCommand :
			public QUndoCommand
	{
	public:
		CreateSubductionUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &feature) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_collection(collection),
			d_feature(feature)
		{
			setText(QObject::tr("create opposite-margin subduction zone"));
		}

		virtual void redo()
		{
			if (!d_collection.is_valid() || !d_feature)
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_iterator = d_collection->add(d_feature);
			guard.release_guard();
		}

		virtual void undo()
		{
			if (!d_collection.is_valid() || !d_iterator || !d_iterator->is_still_valid())
			{
				return;
			}
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_feature = d_collection->remove(*d_iterator);
			d_iterator = boost::none;
			guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_feature;
		boost::optional<GPlatesModel::FeatureCollectionHandle::iterator> d_iterator;
	};

	boost::optional<GPlatesAppLogic::Layer>
	find_reconstruct_layer_for_collection(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		GPlatesAppLogic::ReconstructGraph &graph = application_state.get_reconstruct_graph();
		for (GPlatesAppLogic::ReconstructGraph::iterator layer_iter = graph.begin();
			layer_iter != graph.end(); ++layer_iter)
		{
			GPlatesAppLogic::Layer layer = *layer_iter;
			if (layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
			{
				continue;
			}
			const std::vector<GPlatesAppLogic::Layer::InputConnection> inputs =
					layer.get_channel_inputs(layer.get_main_input_feature_collection_channel());
			for (std::vector<GPlatesAppLogic::Layer::InputConnection>::const_iterator input_iter =
					inputs.begin(); input_iter != inputs.end(); ++input_iter)
			{
				const boost::optional<GPlatesAppLogic::Layer::InputFile> file = input_iter->get_input_file();
				if (file && file->get_feature_collection() == collection)
				{
					return layer;
				}
			}
		}
		return boost::none;
	}

	void
	name_subduction_layer(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesPresentation::ViewState &view_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		const boost::optional<GPlatesAppLogic::Layer> layer =
				find_reconstruct_layer_for_collection(application_state, collection);
		if (!layer)
		{
			return;
		}
		boost::shared_ptr<GPlatesPresentation::VisualLayer> visual_layer =
				view_state.get_visual_layers().get_visual_layer(*layer).lock();
		if (visual_layer)
		{
			visual_layer->set_custom_name(QObject::tr("Active Subduction Zones"));
			visual_layer->set_visible(true);
		}
	}
}


GPlatesViewOperations::GenerateInitialSubductionOperation::GenerateInitialSubductionOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface()),
	d_selection_mode(NOT_SELECTING)
{  }


GPlatesViewOperations::GenerateInitialSubductionOperation::~GenerateInitialSubductionOperation()
{
	restore_mor_candidate_display();
}


std::size_t
GPlatesViewOperations::GenerateInitialSubductionOperation::show_half_stage_mor_candidates()
{
	restore_mor_candidate_display();

	RenderedGeometryCollection::UpdateGuard update_guard;
	d_mor_candidate_layer =
			d_view_state.get_rendered_geometry_collection().
					create_child_rendered_layer_and_transfer_ownership(
							RenderedGeometryCollection::RECONSTRUCTION_LAYER);
	d_mor_candidate_layer->set_active(true);

	std::size_t candidate_count = 0;
	std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
	std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> reconstruct_layers;
	d_application_state.get_current_reconstruction().get_active_layer_outputs<
			GPlatesAppLogic::ReconstructLayerProxy>(reconstruct_layers);
	for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
			layer_iter = reconstruct_layers.begin(); layer_iter != reconstruct_layers.end(); ++layer_iter)
	{
		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
		(*layer_iter)->get_reconstructed_feature_geometries(geometries);
		for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
				geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
		{
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
			if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
					!is_half_stage_mor(rfg.get_feature_ref()))
			{
				continue;
			}

			const GPlatesMaths::PolylineOnSphere *polyline =
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
							rfg.reconstructed_geometry().get());
			if (!polyline)
			{
				continue;
			}

			if (d_captured_continent)
			{
				const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type>
						left_plate =
							GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
									rfg.get_feature_ref(),
									GPlatesModel::PropertyName::create_gpml("leftPlate"));
				const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type>
						right_plate =
							GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
									rfg.get_feature_ref(),
									GPlatesModel::PropertyName::create_gpml("rightPlate"));
				if (!left_plate || !right_plate ||
						(d_captured_continent->plate_id != (*left_plate)->get_value() &&
						 d_captured_continent->plate_id != (*right_plate)->get_value()))
				{
					continue;
				}
			}

			const GPlatesModel::TopLevelProperty *property = (*rfg.property()).get();
			if (!seen_properties.insert(property).second)
			{
				continue;
			}

			d_mor_candidate_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_reconstruction_geometry(
							rfg.get_non_null_pointer_to_const(),
							RenderedGeometryFactory::create_rendered_polyline_on_sphere(
									polyline->get_non_null_pointer(),
									GPlatesGui::Colour::get_aqua(), 5.0f)));
			++candidate_count;
		}
	}

	return candidate_count;
}


void
GPlatesViewOperations::GenerateInitialSubductionOperation::restore_mor_candidate_display()
{
	if (!d_mor_candidate_layer)
	{
		return;
	}

	RenderedGeometryCollection::UpdateGuard update_guard;
	d_mor_candidate_layer.reset();
}


GPlatesViewOperations::GenerateInitialSubductionOperation::Result
GPlatesViewOperations::GenerateInitialSubductionOperation::arm_continent_selection()
{
	d_selection_mode = NOT_SELECTING;
	restore_mor_candidate_display();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_CONTINENT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select one of the rifted ContinentalCrust polygons in the globe or map view."));
}


GPlatesViewOperations::GenerateInitialSubductionOperation::Result
GPlatesViewOperations::GenerateInitialSubductionOperation::arm_mor_selection()
{
	d_selection_mode = NOT_SELECTING;
	restore_mor_candidate_display();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_MOR;
	const std::size_t candidate_count = show_half_stage_mor_candidates();
	return Result(SELECTION_ARMED,
			candidate_count == 0
					? QObject::tr("No selectable HalfStageRotationVersion3 MOR is available at this time. The existing map display has not been changed.")
					: QObject::tr("Highlighted %1 selectable HalfStageRotationVersion3 MOR candidate(s) in aqua. Other features remain visible for context but cannot satisfy this selection.")
							.arg(candidate_count));
}


GPlatesViewOperations::GenerateInitialSubductionOperation::Result
GPlatesViewOperations::GenerateInitialSubductionOperation::cancel_selection()
{
	const bool was_selecting = d_selection_mode != NOT_SELECTING;
	d_selection_mode = NOT_SELECTING;
	d_feature_focus.unset_focus();
	restore_mor_candidate_display();
	return Result(OPERATION_CANCELLED,
			was_selecting
					? QObject::tr("Selection cancelled; the candidate highlight has been cleared.")
					: QString());
}


GPlatesViewOperations::GenerateInitialSubductionOperation::Result
GPlatesViewOperations::GenerateInitialSubductionOperation::capture_armed_selection()
{
	if (d_selection_mode == NOT_SELECTING)
	{
		return Result(OPERATION_CANCELLED, QString());
	}
	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That item has no selectable reconstructed feature geometry; selection is still armed."));
	}
	const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> rfg =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!rfg)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("This operation requires editable reconstructed feature geometry; selection is still armed."));
	}

	const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry =
			(*rfg)->reconstructed_geometry();
	const double reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();

	if (d_selection_mode == SELECTING_CONTINENT)
	{
		static const GPlatesModel::FeatureType CONTINENTAL_CRUST =
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust");
		const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(geometry.get());
		if (!polygon || d_feature_focus.focused_feature()->feature_type() != CONTINENTAL_CRUST)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("That is not a ContinentalCrust polygon; continent selection is still armed."));
		}
		const boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
				(*rfg)->reconstruction_plate_id();
		if (!plate_id)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selected continent has no reconstruction plate ID."));
		}
		if (d_captured_mor && *plate_id != d_captured_mor->left_plate &&
				*plate_id != d_captured_mor->right_plate)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("That continent is not one of the captured MOR's left/right plates; continent selection is still armed."));
		}
		d_captured_continent = CapturedContinent(
				d_feature_focus.focused_feature(), (*rfg)->get_non_null_pointer_to_const(),
				polygon->get_non_null_pointer(), *plate_id, reconstruction_time);
		d_selection_mode = NOT_SELECTING;
		return Result(CONTINENT_CAPTURED,
				QObject::tr("Continent captured. Now select its half-stage MOR."));
	}

	const GPlatesMaths::PolylineOnSphere *polyline =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(geometry.get());
	if (!polyline || !is_half_stage_mor(d_feature_focus.focused_feature()))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That is not a selectable HalfStageRotationVersion3 MidOceanRidge polyline; MOR selection is still armed."));
	}
	const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> left_plate =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
					d_feature_focus.focused_feature(),
					GPlatesModel::PropertyName::create_gpml("leftPlate"));
	const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> right_plate =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
					d_feature_focus.focused_feature(),
					GPlatesModel::PropertyName::create_gpml("rightPlate"));
	if (!left_plate || !right_plate)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected MOR has no left/right plate pair, so it is not usable as a half-stage ridge."));
	}
	if (d_captured_continent &&
			d_captured_continent->plate_id != (*left_plate)->get_value() &&
			d_captured_continent->plate_id != (*right_plate)->get_value())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That MOR does not include the captured continent's plate ID; MOR selection is still armed."));
	}
	d_captured_mor = CapturedMor(
			d_feature_focus.focused_feature(), polyline->get_non_null_pointer(),
			(*left_plate)->get_value(), (*right_plate)->get_value(), reconstruction_time);
	d_selection_mode = NOT_SELECTING;
	restore_mor_candidate_display();
	return Result(MOR_CAPTURED,
			QObject::tr("Half-stage MOR captured. Generate the opposite-margin trench when both selections are ready."));
}


bool
GPlatesViewOperations::GenerateInitialSubductionOperation::can_generate() const
{
	return d_captured_continent && d_captured_mor &&
			d_captured_continent->feature.is_valid() &&
			d_captured_mor->feature.is_valid() &&
			(d_captured_continent->plate_id == d_captured_mor->left_plate ||
			 d_captured_continent->plate_id == d_captured_mor->right_plate) &&
			std::fabs(d_captured_continent->reconstruction_time -
					d_captured_mor->reconstruction_time) <= 1e-9;
}


QString
GPlatesViewOperations::GenerateInitialSubductionOperation::continent_status() const
{
	if (!d_captured_continent || !d_captured_continent->feature.is_valid())
	{
		return QObject::tr("Continent: not selected");
	}
	return QObject::tr("Continent: Plate %1 at %2 Ma")
			.arg(d_captured_continent->plate_id)
			.arg(d_captured_continent->reconstruction_time, 0, 'f', 1);
}


QString
GPlatesViewOperations::GenerateInitialSubductionOperation::mor_status() const
{
	if (!d_captured_mor || !d_captured_mor->feature.is_valid())
	{
		return QObject::tr("Half-stage MOR: not selected");
	}
	return QObject::tr("Half-stage MOR: Plates %1 / %2 at %3 Ma")
			.arg(d_captured_mor->left_plate)
			.arg(d_captured_mor->right_plate)
			.arg(d_captured_mor->reconstruction_time, 0, 'f', 1);
}


GPlatesViewOperations::GenerateInitialSubductionOperation::Result
GPlatesViewOperations::GenerateInitialSubductionOperation::generate(
		QWidget *parent_widget)
{
	if (!d_captured_continent || !d_captured_mor)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select both a rifted continent and its half-stage MOR before generating."));
	}
	if (!d_captured_continent->feature.is_valid() || !d_captured_mor->feature.is_valid())
	{
		reset();
		return Result(OPERATION_ERROR,
				QObject::tr("One of the captured features no longer exists; select both again."));
	}
	if (d_captured_continent->plate_id != d_captured_mor->left_plate &&
			d_captured_continent->plate_id != d_captured_mor->right_plate)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected continent is not one of the selected MOR's left/right plates."));
	}
	const double reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(reconstruction_time - d_captured_continent->reconstruction_time) > 1e-9 ||
			std::fabs(reconstruction_time - d_captured_mor->reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after selection. Re-select both features at the current time."));
	}

	try
	{
		const InitialSubductionGeometry::Result geometry =
				InitialSubductionGeometry::generate(
						d_captured_continent->polygon, d_captured_mor->polyline);
		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_view_state.get_rendered_geometry_collection().create_child_rendered_layer_and_transfer_ownership(
						RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_subduction_teeth_polyline(
						geometry.trench, true, GPlatesGui::Colour::get_yellow(), 4.0f));
		preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_arrowed_polyline(
						geometry.motion_arrow, GPlatesGui::Colour::get_green(), 12.0f, 4.0f));

		const QMessageBox::StandardButton confirmation = QMessageBox::question(
				parent_widget,
				QObject::tr("Confirm Opposite-Margin Subduction Zone"),
				QObject::tr(
						"The green arrow is the spreading direction inferred from the selected half-stage MOR. "
						"The yellow toothed line is a smoothed trench on the far continental margin.\n\n"
						"It covers approximately %1 km of the continent's %2 km motion-normal width, is %3 km long, "
						"uses segments no longer than %4 km, remains at least %5 km outside the continental crust, "
						"and extends %6 km beyond each end. The clearance check required %7 outward adjustment pass(es). "
						"Create it for Plate %8?")
						.arg(geometry.metrics.trench_motion_normal_span_km, 0, 'f', 0)
						.arg(geometry.metrics.continent_motion_normal_width_km, 0, 'f', 0)
						.arg(geometry.metrics.trench_length_km, 0, 'f', 0)
						.arg(geometry.metrics.maximum_segment_length_km, 0, 'f', 0)
						.arg(geometry.metrics.minimum_continental_clearance_km, 0, 'f', 0)
						.arg(geometry.metrics.endpoint_buffer_km, 0, 'f', 0)
						.arg(geometry.metrics.clearance_adjustment_iterations)
						.arg(d_captured_continent->plate_id),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes);
		preview_layer->clear_rendered_geometries();
		if (confirmation != QMessageBox::Yes)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("Subduction preview rejected; no data changed and both selections remain captured."));
		}

		const QString name = QObject::tr("Plate %1 Opposite-Margin Subduction Zone")
				.arg(d_captured_continent->plate_id);
		const GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				create_subduction_zone(
						name, reconstruction_time, d_captured_continent->plate_id, geometry.trench);
		GPlatesAppLogic::FeatureCollectionFileState::file_reference file =
				resolve_or_create_worldbuilding_feature_collection(
						d_application_state.get_feature_collection_file_io(),
						d_application_state.get_feature_collection_file_state(),
						QString::fromLatin1("trenches"),
						QObject::tr("Active Subduction Zones"));
		const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
				file.get_file().get_feature_collection();
		std::unique_ptr<QUndoCommand> command(new CreateSubductionUndoCommand(
				d_feature_focus, d_model_interface, collection, feature));
		reset();
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		name_subduction_layer(d_application_state, d_view_state, collection);
		return Result(SUBDUCTION_COMPLETED,
				QObject::tr("Created '%1' at %2 Ma from the selected MOR's inferred spreading direction.")
						.arg(name).arg(reconstruction_time, 0, 'f', 1));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not generate the opposite-margin subduction zone: %1")
						.arg(exception.what()));
	}
}


void
GPlatesViewOperations::GenerateInitialSubductionOperation::reset()
{
	d_selection_mode = NOT_SELECTING;
	restore_mor_candidate_display();
	d_captured_continent = boost::none;
	d_captured_mor = boost::none;
}
