/* $Id$ */

/**
 * \file
 * Implements the Worldbuilding Pasta ocean-crust age-band step.
 */

#include "CreateOceanCrustOperation.h"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "OceanCrustBandBuilder.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/PlanetaryParameters.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/ReconstructUtils.h"

#include "feature-visitors/PropertyValueFinder.h"

#include "gui/Colour.h"
#include "gui/AnimationController.h"
#include "gui/FeatureFocus.h"

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "qt-widgets/ChooseFeatureCollectionWidget.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	const double TIME_EPSILON = 1e-9;

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

	boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type>
	get_single_active_geometry(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			double reconstruction_time,
			QString &reason)
	{
		boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> geometry;
		for (GPlatesModel::FeatureHandle::iterator property_iter = feature->begin();
				property_iter != feature->end(); ++property_iter)
		{
			const boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> candidate =
					GPlatesAppLogic::GeometryUtils::get_geometry_from_property(
							property_iter, reconstruction_time);
			if (!candidate)
			{
				continue;
			}
			if (geometry)
			{
				reason = QObject::tr("The selected MOR has more than one active geometry property.");
				return boost::none;
			}
			geometry = *candidate;
		}
		if (!geometry)
		{
			reason = QObject::tr("The selected MOR has no active geometry at %1 Ma.")
					.arg(reconstruction_time, 0, 'f', 2);
		}
		return geometry;
	}

	GPlatesViewOperations::OceanCrustBandBuilder::polygon_seq_type
	get_existing_ocean_crust(
			GPlatesAppLogic::ApplicationState &application_state)
	{
		static const GPlatesModel::FeatureType OCEANIC_CRUST =
				GPlatesModel::FeatureType::create_gpml("OceanicCrust");
		GPlatesViewOperations::OceanCrustBandBuilder::polygon_seq_type polygons;
		std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
		application_state.get_current_reconstruction().get_active_layer_outputs<
				GPlatesAppLogic::ReconstructLayerProxy>(layers);
		for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
				layer_iter = layers.begin(); layer_iter != layers.end(); ++layer_iter)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			(*layer_iter)->get_reconstructed_feature_geometries(geometries);
			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
			{
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
				if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
						!rfg.get_feature_ref().is_valid() ||
						rfg.get_feature_ref()->feature_type() != OCEANIC_CRUST ||
						!seen_properties.insert((*rfg.property()).get()).second)
				{
					continue;
				}
				const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
							rfg.reconstructed_geometry().get());
				if (polygon)
				{
					polygons.push_back(
							GPlatesViewOperations::OceanCrustBandBuilder::polygon_ptr_type(polygon));
				}
			}
		}
		return polygons;
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
	create_oceanic_crust_feature(
			const QString &name,
			double appearance_time,
			double geometry_import_time,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &stored_polygon)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("OceanicCrust"));
		const GPlatesModel::FeatureHandle::weak_ref feature_ref = feature->reference();
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(appearance_time),
						GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
		set_required_property(feature_ref,
				GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));
		set_required_property(feature_ref,
				GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
				GPlatesModel::ModelUtils::create_gml_time_instant(
						GPlatesPropertyValues::GeoTimeInstant(geometry_import_time)));
		set_required_property(feature_ref, GPlatesModel::PropertyName::create_gpml("outlineOf"),
				GPlatesAppLogic::GeometryUtils::create_polygon_geometry_property_value(stored_polygon));
		return feature;
	}

	class CreateOceanCrustUndoCommand : public QUndoCommand
	{
	public:
		CreateOceanCrustUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> &features) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_collection(collection),
			d_features(features)
		{
			setText(QObject::tr("create ocean-crust age band"));
		}

		virtual void redo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_iterators.clear();
			for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::const_iterator feature_iter =
					d_features.begin(); feature_iter != d_features.end(); ++feature_iter)
			{
				d_iterators.push_back(d_collection->add(*feature_iter));
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
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::vector<GPlatesModel::FeatureCollectionHandle::iterator>::reverse_iterator iter =
					d_iterators.rbegin(); iter != d_iterators.rend(); ++iter)
			{
				if (iter->is_still_valid())
				{
					d_collection->remove(*iter);
				}
			}
			d_iterators.clear();
			guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> d_features;
		std::vector<GPlatesModel::FeatureCollectionHandle::iterator> d_iterators;
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
	name_ocean_crust_layer(
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
			visual_layer->set_custom_name(QObject::tr("Ocean Crust Age Bands"));
			visual_layer->set_visible(true);
		}
	}
}


GPlatesViewOperations::CreateOceanCrustOperation::CreateOceanCrustOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface())
{ }


bool
GPlatesViewOperations::CreateOceanCrustOperation::select_focused_mor(
		QString &message)
{
	const GPlatesModel::FeatureHandle::weak_ref focused_feature =
			d_feature_focus.focused_feature();
	if (!is_half_stage_mor(focused_feature))
	{
		return false;
	}

	if (d_selected_mor.is_valid() && d_selected_mor == focused_feature)
	{
		d_selected_mor = GPlatesModel::FeatureHandle::weak_ref();
		message = QObject::tr(
				"Removed the half-stage MOR from the ocean-crust selection. Shift-click it again to reselect it.");
		return true;
	}

	d_selected_mor = focused_feature;
	message = QObject::tr(
			"Selected half-stage MOR %1 for ocean-crust generation. The selection persists while you inspect other features; Shift-click it again to clear it.")
				.arg(focused_feature->feature_id().get().qstring());
	return true;
}


GPlatesViewOperations::CreateOceanCrustOperation::Result
GPlatesViewOperations::CreateOceanCrustOperation::trigger(
		QWidget *parent_widget)
{
	GPlatesModel::FeatureHandle::weak_ref selected_mor = d_selected_mor;
	if (!selected_mor.is_valid() && is_half_stage_mor(d_feature_focus.focused_feature()))
	{
		selected_mor = d_feature_focus.focused_feature();
		d_selected_mor = selected_mor;
	}
	if (!selected_mor.is_valid() || !is_half_stage_mor(selected_mor) || !selected_mor->parent_ptr())
	{
		return Result(SELECTION_REQUIRED,
				QObject::tr("Shift-click a HalfStageRotationVersion3 MidOceanRidge, then run Generate Ocean Crust from MOR again."));
	}

	const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> left_plate =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
					selected_mor,
					GPlatesModel::PropertyName::create_gpml("leftPlate"));
	const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> right_plate =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
					selected_mor,
					GPlatesModel::PropertyName::create_gpml("rightPlate"));
	if (!left_plate || !right_plate || (*left_plate)->get_value() == (*right_plate)->get_value())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected half-stage MOR needs two different left/right plate IDs."));
	}

	const double current_time = d_application_state.get_current_reconstruction_time();
	QDialog dialog(parent_widget);
	dialog.setWindowTitle(QObject::tr("Worldbuilding Pasta - Generate Ocean Crust from MOR"));
	dialog.setModal(true);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *intro = new QLabel(QObject::tr(
			"Fill the open space created on each side of the Shift-clicked half-stage MOR. "
			"Existing OceanicCrust is subtracted before the preview, so accepted polygons do not duplicate loaded crust at %1 Ma.")
			.arg(current_time, 0, 'f', 2), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	QFormLayout *form = new QFormLayout();
	QLabel *current_time_value = new QLabel(QObject::tr("%1 Ma").arg(current_time, 0, 'f', 2), &dialog);
	form->addRow(QObject::tr("Young ridge edge:"), current_time_value);
	QDoubleSpinBox *older_time_spin = new QDoubleSpinBox(&dialog);
	older_time_spin->setDecimals(2);
	older_time_spin->setSingleStep(1.0);
	older_time_spin->setRange(current_time + 0.1, std::max(current_time + 0.1, 10000.0));
	const boost::optional<double> project_older_bound =
			d_application_state.get_project_timestamp_schedule().default_older_bound(current_time);
	const double fallback_increment = d_view_state.get_animation_controller().time_increment();
	older_time_spin->setValue(project_older_bound
			? *project_older_bound : current_time + fallback_increment);
	older_time_spin->setSuffix(QObject::tr(" Ma"));
	older_time_spin->setToolTip(QObject::tr(
			"Defaults to the next older Project Timestamp. The fallback is the current animation increment."));
	form->addRow(QObject::tr("Older ridge edge:"), older_time_spin);
	form->addRow(QObject::tr("Default source:"), new QLabel(project_older_bound
			? QObject::tr("Next older Project Timestamp (%1 Ma)").arg(*project_older_bound, 0, 'f', 2)
			: QObject::tr("Animation increment fallback (%1 My)").arg(fallback_increment, 0, 'f', 2), &dialog));
	form->addRow(QObject::tr("Assigned plates:"),
			new QLabel(QObject::tr("Left %1 / Right %2")
					.arg((*left_plate)->get_value()).arg((*right_plate)->get_value()), &dialog));
	layout->addLayout(form);
	GPlatesQtWidgets::ChooseFeatureCollectionWidget *collection_widget =
			new GPlatesQtWidgets::ChooseFeatureCollectionWidget(
					d_application_state.get_reconstruct_method_registry(),
					d_application_state.get_feature_collection_file_state(),
					d_application_state.get_feature_collection_file_io(),
					&dialog);
	collection_widget->setTitle(QObject::tr("Destination ocean-crust collection"));
	collection_widget->set_help_text(QObject::tr(
			"Choose a loaded reconstruction collection or create a new collection. The last accepted destination is selected next time."));
	collection_widget->initialise();
	if (d_last_output_collection.is_valid())
	{
		collection_widget->select_feature_collection(d_last_output_collection);
	}
	layout->addWidget(collection_widget);
	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Preview Age Band"));
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED, QObject::tr("Ocean-crust generation cancelled; no data changed."));
	}

	const double older_time = older_time_spin->value();
	if (older_time <= current_time + TIME_EPSILON)
	{
		return Result(OPERATION_ERROR, QObject::tr("The older ridge edge must be older than the displayed time."));
	}

	try
	{
		GPlatesAppLogic::ReconstructionFeatureProperties reconstruction_properties;
		reconstruction_properties.visit_feature(selected_mor);
		if (!reconstruction_properties.is_feature_defined_at_recon_time(current_time) ||
				!reconstruction_properties.is_feature_defined_at_recon_time(older_time))
		{
			throw std::runtime_error(
					"The selected MOR is not valid at both requested interval bounds.");
		}
		QString geometry_reason;
		const boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type>
				current_source_geometry =
						get_single_active_geometry(selected_mor, current_time, geometry_reason);
		if (!current_source_geometry)
		{
			throw std::runtime_error(geometry_reason.toStdString());
		}
		const boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type>
				older_source_geometry =
						get_single_active_geometry(selected_mor, older_time, geometry_reason);
		if (!older_source_geometry)
		{
			throw std::runtime_error(geometry_reason.toStdString());
		}

		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction()
						.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type current_tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type older_tree =
				tree_creator.get_reconstruction_tree(older_time);
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type current_ridge_geometry =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						*current_source_geometry,
						d_application_state.get_reconstruct_method_registry(),
						selected_mor, current_time, tree_creator,
						GPlatesAppLogic::ReconstructParams(), false);
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type older_ridge_geometry =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						*older_source_geometry,
						d_application_state.get_reconstruct_method_registry(),
						selected_mor, older_time, tree_creator,
						GPlatesAppLogic::ReconstructParams(), false);
		const GPlatesMaths::PolylineOnSphere *current_ridge =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(current_ridge_geometry.get());
		const GPlatesMaths::PolylineOnSphere *older_ridge =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(older_ridge_geometry.get());
		if (!current_ridge || !older_ridge)
		{
			throw std::runtime_error(
					"The selected MOR could not be reconstructed as a polyline at both interval bounds.");
		}

		const GPlatesModel::integer_plate_id_type left_plate_id = (*left_plate)->get_value();
		const GPlatesModel::integer_plate_id_type right_plate_id = (*right_plate)->get_value();
		const OceanCrustBandBuilder::polygon_seq_type existing_ocean_crust =
				get_existing_ocean_crust(d_application_state);
		const OceanCrustBandBuilder::Result left_band =
				OceanCrustBandBuilder::build_side_band(
						*current_ridge, *older_ridge, left_plate_id,
						*older_tree, *current_tree, existing_ocean_crust);
		const OceanCrustBandBuilder::Result right_band =
				OceanCrustBandBuilder::build_side_band(
						*current_ridge, *older_ridge, right_plate_id,
						*older_tree, *current_tree, existing_ocean_crust);
		if (!left_band.success)
		{
			throw std::runtime_error(QObject::tr("Left-plate band failed: %1")
					.arg(left_band.error).toStdString());
		}
		if (!right_band.success)
		{
			throw std::runtime_error(QObject::tr("Right-plate band failed: %1")
					.arg(right_band.error).toStdString());
		}
		if (left_band.polygons.empty() && right_band.polygons.empty())
		{
			throw std::runtime_error(
					"Existing OceanicCrust already fills both sides of this interval.");
		}

		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_view_state.get_rendered_geometry_collection().
						create_child_rendered_layer_and_transfer_ownership(
								RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		for (OceanCrustBandBuilder::polygon_seq_type::const_iterator piece_iter =
				left_band.polygons.begin(); piece_iter != left_band.polygons.end(); ++piece_iter)
		{
			preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							*piece_iter, GPlatesGui::Colour::get_aqua(), 3.0f, true,
							GPlatesGui::Colour(0.0f, 0.30f, 0.38f)));
		}
		for (OceanCrustBandBuilder::polygon_seq_type::const_iterator piece_iter =
				right_band.polygons.begin(); piece_iter != right_band.polygons.end(); ++piece_iter)
		{
			preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							*piece_iter, GPlatesGui::Colour(1.0f, 0.55f, 0.0f), 3.0f, true,
							GPlatesGui::Colour(0.40f, 0.20f, 0.0f)));
		}

		double left_area_steradians = 0.0;
		for (OceanCrustBandBuilder::polygon_seq_type::const_iterator piece_iter =
				left_band.polygons.begin(); piece_iter != left_band.polygons.end(); ++piece_iter)
		{
			left_area_steradians += (*piece_iter)->get_area().dval();
		}
		double right_area_steradians = 0.0;
		for (OceanCrustBandBuilder::polygon_seq_type::const_iterator piece_iter =
				right_band.polygons.begin(); piece_iter != right_band.polygons.end(); ++piece_iter)
		{
			right_area_steradians += (*piece_iter)->get_area().dval();
		}
		const double radius_km = d_application_state.get_planetary_parameters().
				effective_radius_kilometres();
		const double square_radius_million_km2 = radius_km * radius_km / 1.0e6;
		QMessageBox confirmation(
				QMessageBox::Question,
				QObject::tr("Confirm Ocean-Crust Age Band"),
				QObject::tr(
						"Aqua follows left Plate %1: %2 piece(s), %3 million km².\n"
						"Orange follows right Plate %4: %5 piece(s), %6 million km².\n\n"
						"%7 existing OceanicCrust polygon(s) were checked and overlapping area was removed%8. "
						"The result records seafloor created from %9 to %10 Ma without changing the MOR or either plate's motion.\n\n"
						"Create these editable OceanicCrust polygons in the selected collection?")
						.arg(left_plate_id)
						.arg(static_cast<unsigned int>(left_band.polygons.size()))
						.arg(left_area_steradians * square_radius_million_km2, 0, 'f', 3)
						.arg(right_plate_id)
						.arg(static_cast<unsigned int>(right_band.polygons.size()))
						.arg(right_area_steradians * square_radius_million_km2, 0, 'f', 3)
						.arg(static_cast<unsigned int>(existing_ocean_crust.size()))
						.arg(left_band.existing_overlap_removed || right_band.existing_overlap_removed
								? QObject::tr(" where necessary") : QObject::tr("; no overlap was found"))
						.arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2),
				QMessageBox::Ok | QMessageBox::Cancel,
				parent_widget);
		confirmation.button(QMessageBox::Ok)->setText(QObject::tr("Create Ocean Crust"));
		confirmation.setDefaultButton(QMessageBox::Cancel);
		const int confirmation_result = confirmation.exec();
		preview_layer->clear_rendered_geometries();
		if (confirmation_result != QMessageBox::Ok)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("Ocean-crust preview rejected; no data changed."));
		}

		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> features;
		const OceanCrustBandBuilder::polygon_seq_type *side_bands[2] =
				{ &left_band.polygons, &right_band.polygons };
		const GPlatesModel::integer_plate_id_type side_plate_ids[2] =
				{ left_plate_id, right_plate_id };
		for (unsigned int side_index = 0; side_index < 2; ++side_index)
		{
			const OceanCrustBandBuilder::polygon_seq_type &pieces = *side_bands[side_index];
			for (unsigned int piece_index = 0; piece_index < pieces.size(); ++piece_index)
			{
				const OceanCrustBandBuilder::polygon_ptr_type stored_polygon =
						OceanCrustBandBuilder::reverse_reconstruct_polygon(
								*pieces[piece_index], side_plate_ids[side_index], *current_tree);
				QString name = QObject::tr("Oceanic crust %1-%2 Ma - plate %3")
						.arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2)
						.arg(side_plate_ids[side_index]);
				if (pieces.size() > 1)
				{
					name += QObject::tr(" (part %1)").arg(piece_index + 1);
				}
				features.push_back(create_oceanic_crust_feature(
						name, current_time, current_time,
						side_plate_ids[side_index], stored_polygon));
			}
		}

		boost::optional< std::pair<
				GPlatesAppLogic::FeatureCollectionFileState::file_reference, bool> > collection_selection;
		try
		{
			collection_selection = collection_widget->get_file_reference();
		}
		catch (const GPlatesQtWidgets::ChooseFeatureCollectionWidget::NoFeatureCollectionSelectedException &)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("No destination feature collection was selected; no crust was created."));
		}
		const GPlatesAppLogic::FeatureCollectionFileState::file_reference file =
				collection_selection->first;
		const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
				file.get_file().get_feature_collection();
		std::unique_ptr<QUndoCommand> command(new CreateOceanCrustUndoCommand(
				d_feature_focus, d_model_interface, collection, features));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		d_last_output_collection = collection;
		if (collection_selection->second)
		{
			name_ocean_crust_layer(d_application_state, d_view_state, collection);
		}
		return Result(OPERATION_COMPLETED,
				QObject::tr("Created %1 non-overlapping OceanicCrust polygon(s) for Plates %2 and %3 spanning %4-%5 Ma. The selected MOR and plate motion were not changed.")
						.arg(static_cast<unsigned int>(features.size()))
						.arg(left_plate_id).arg(right_plate_id)
						.arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not create the ocean-crust age band: %1").arg(exception.what()));
	}
}
