/* $Id$ */

/**
 * \file
 * Implements the Worldbuilding Pasta ocean-crust age-band step.
 */

#include "CreateOceanCrustOperation.h"

#include <algorithm>
#include <memory>
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
#include "UndoRedo.h"
#include "WorldbuildingFeatureCollectionUtils.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
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

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	create_crust_strip_at_current_time(
			const GPlatesMaths::PolylineOnSphere &current_ridge,
			const GPlatesMaths::PolylineOnSphere &older_ridge,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesAppLogic::ReconstructionTree &older_tree,
			const GPlatesAppLogic::ReconstructionTree &current_tree)
	{
		const std::size_t current_vertex_count =
				static_cast<std::size_t>(std::distance(current_ridge.vertex_begin(), current_ridge.vertex_end()));
		const std::size_t older_vertex_count =
				static_cast<std::size_t>(std::distance(older_ridge.vertex_begin(), older_ridge.vertex_end()));
		if (current_vertex_count < 2 || current_vertex_count != older_vertex_count)
		{
			throw std::runtime_error("The ridge geometry changed vertex topology between the two ages.");
		}

		std::vector<GPlatesMaths::PointOnSphere> advected_older_ridge;
		advected_older_ridge.reserve(older_vertex_count);
		for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
				older_ridge.vertex_begin(); vertex_iter != older_ridge.vertex_end(); ++vertex_iter)
		{
			const GPlatesMaths::PointOnSphere plate_frame_point =
					GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
							*vertex_iter, plate_id, older_tree, true);
			advected_older_ridge.push_back(
					GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
							plate_frame_point, plate_id, current_tree, false));
		}

		bool has_spreading_width = false;
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator current_iter = current_ridge.vertex_begin();
		for (std::size_t vertex_index = 0; vertex_index < older_vertex_count;
				++vertex_index, ++current_iter)
		{
			if (!GPlatesMaths::points_are_coincident(*current_iter, advected_older_ridge[vertex_index]))
			{
				has_spreading_width = true;
				break;
			}
		}
		if (!has_spreading_width)
		{
			throw std::runtime_error("This plate has no recorded motion across the requested age band.");
		}

		std::vector<GPlatesMaths::PointOnSphere> ring(
				current_ridge.vertex_begin(), current_ridge.vertex_end());
		for (std::vector<GPlatesMaths::PointOnSphere>::const_reverse_iterator vertex_iter =
				advected_older_ridge.rbegin(); vertex_iter != advected_older_ridge.rend(); ++vertex_iter)
		{
			ring.push_back(*vertex_iter);
		}
		return GPlatesMaths::PolygonOnSphere::create(ring);
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	reverse_reconstruct_polygon(
			const GPlatesMaths::PolygonOnSphere &polygon,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesAppLogic::ReconstructionTree &current_tree)
	{
		std::vector<GPlatesMaths::PointOnSphere> stored_ring;
		stored_ring.reserve(static_cast<std::size_t>(std::distance(
				polygon.exterior_ring_vertex_begin(), polygon.exterior_ring_vertex_end())));
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				polygon.exterior_ring_vertex_begin();
				vertex_iter != polygon.exterior_ring_vertex_end(); ++vertex_iter)
		{
			stored_ring.push_back(GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
					*vertex_iter, plate_id, current_tree, true));
		}
		return GPlatesMaths::PolygonOnSphere::create(stored_ring);
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


GPlatesViewOperations::CreateOceanCrustOperation::Result
GPlatesViewOperations::CreateOceanCrustOperation::trigger(
		QWidget *parent_widget)
{
	if (!d_feature_focus.focused_feature().is_valid() ||
			!is_half_stage_mor(d_feature_focus.focused_feature()) ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(SELECTION_REQUIRED,
				QObject::tr("Choose a HalfStageRotationVersion3 MidOceanRidge, then press Create Ocean Crust again."));
	}

	const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> rfg =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!rfg)
	{
		return Result(SELECTION_REQUIRED,
				QObject::tr("Choose the reconstructed polyline of a HalfStageRotationVersion3 MidOceanRidge."));
	}
	const GPlatesMaths::PolylineOnSphere *current_ridge =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>((*rfg)->reconstructed_geometry().get());
	if (!current_ridge)
	{
		return Result(SELECTION_REQUIRED,
				QObject::tr("The selected half-stage MOR does not have a polyline geometry."));
	}

	const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> left_plate =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
					d_feature_focus.focused_feature(),
					GPlatesModel::PropertyName::create_gpml("leftPlate"));
	const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> right_plate =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
					d_feature_focus.focused_feature(),
					GPlatesModel::PropertyName::create_gpml("rightPlate"));
	if (!left_plate || !right_plate || (*left_plate)->get_value() == (*right_plate)->get_value())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected half-stage MOR needs two different left/right plate IDs."));
	}

	const double current_time = d_application_state.get_current_reconstruction_time();
	QDialog dialog(parent_widget);
	dialog.setWindowTitle(QObject::tr("Worldbuilding Pasta - Create Ocean Crust"));
	dialog.setModal(true);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *intro = new QLabel(QObject::tr(
			"Create one editable ocean-crust age band on each side of the selected half-stage MOR. "
			"The older ridge edge is carried to %1 Ma by each plate's recorded .rot motion; the selected MOR forms the young edge.")
			.arg(current_time, 0, 'f', 1), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	QFormLayout *form = new QFormLayout();
	QLabel *current_time_value = new QLabel(QObject::tr("%1 Ma").arg(current_time, 0, 'f', 1), &dialog);
	form->addRow(QObject::tr("Young ridge edge:"), current_time_value);
	QDoubleSpinBox *older_time_spin = new QDoubleSpinBox(&dialog);
	older_time_spin->setDecimals(1);
	older_time_spin->setSingleStep(5.0);
	older_time_spin->setRange(current_time + 0.1, std::max(current_time + 0.1, 10000.0));
	older_time_spin->setValue(current_time + 50.0);
	older_time_spin->setSuffix(QObject::tr(" Ma"));
	older_time_spin->setToolTip(QObject::tr(
			"Normally this is the preceding turn (for example 1000 Ma when the display is at 950 Ma)."));
	form->addRow(QObject::tr("Older ridge edge:"), older_time_spin);
	form->addRow(QObject::tr("Assigned plates:"),
			new QLabel(QObject::tr("Left %1 / Right %2")
					.arg((*left_plate)->get_value()).arg((*right_plate)->get_value()), &dialog));
	layout->addLayout(form);
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
		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction()
						.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type current_tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type older_tree =
				tree_creator.get_reconstruction_tree(older_time);
		const GPlatesAppLogic::ReconstructMethodRegistry reconstruct_method_registry;
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type stored_ridge =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						current_ridge->get_non_null_pointer(), reconstruct_method_registry,
						d_feature_focus.focused_feature(), current_time, tree_creator,
						GPlatesAppLogic::ReconstructParams(), true);
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type older_ridge_geometry =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						stored_ridge, reconstruct_method_registry,
						d_feature_focus.focused_feature(), older_time, tree_creator,
						GPlatesAppLogic::ReconstructParams(), false);
		const GPlatesMaths::PolylineOnSphere *older_ridge =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(older_ridge_geometry.get());
		if (!older_ridge)
		{
			throw std::runtime_error("The selected MOR could not be reconstructed as a polyline at the older age.");
		}

		const GPlatesModel::integer_plate_id_type left_plate_id = (*left_plate)->get_value();
		const GPlatesModel::integer_plate_id_type right_plate_id = (*right_plate)->get_value();
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type left_current_polygon =
				create_crust_strip_at_current_time(
						*current_ridge, *older_ridge, left_plate_id, *older_tree, *current_tree);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type right_current_polygon =
				create_crust_strip_at_current_time(
						*current_ridge, *older_ridge, right_plate_id, *older_tree, *current_tree);

		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_view_state.get_rendered_geometry_collection().
						create_child_rendered_layer_and_transfer_ownership(
								RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						left_current_polygon, GPlatesGui::Colour::get_aqua(), 3.0f, true,
						GPlatesGui::Colour(0.0f, 0.30f, 0.38f)));
		preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						right_current_polygon, GPlatesGui::Colour(1.0f, 0.55f, 0.0f), 3.0f, true,
						GPlatesGui::Colour(0.40f, 0.20f, 0.0f)));

		const QMessageBox::StandardButton confirmation = QMessageBox::question(
				parent_widget,
				QObject::tr("Confirm Ocean-Crust Age Band"),
				QObject::tr(
						"Aqua follows left Plate %1 and orange follows right Plate %2. "
						"Together they record seafloor created from %3 to %4 Ma without changing either plate's motion.\n\n"
						"Create these two editable OceanicCrust polygons?")
						.arg(left_plate_id).arg(right_plate_id)
						.arg(older_time, 0, 'f', 1).arg(current_time, 0, 'f', 1),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes);
		preview_layer->clear_rendered_geometries();
		if (confirmation != QMessageBox::Yes)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("Ocean-crust preview rejected; no data changed."));
		}

		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type left_stored_polygon =
				reverse_reconstruct_polygon(*left_current_polygon, left_plate_id, *current_tree);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type right_stored_polygon =
				reverse_reconstruct_polygon(*right_current_polygon, right_plate_id, *current_tree);
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> features;
		features.push_back(create_oceanic_crust_feature(
				QObject::tr("Plate %1 Ocean Crust %2-%3 Ma")
						.arg(left_plate_id).arg(older_time, 0, 'f', 1).arg(current_time, 0, 'f', 1),
				current_time, current_time, left_plate_id, left_stored_polygon));
		features.push_back(create_oceanic_crust_feature(
				QObject::tr("Plate %1 Ocean Crust %2-%3 Ma")
						.arg(right_plate_id).arg(older_time, 0, 'f', 1).arg(current_time, 0, 'f', 1),
				current_time, current_time, right_plate_id, right_stored_polygon));

		GPlatesAppLogic::FeatureCollectionFileState::file_reference file =
				create_named_empty_feature_collection(
						d_application_state.get_feature_collection_file_io(),
						QObject::tr("Ocean Crust Age Bands"));
		const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
				file.get_file().get_feature_collection();
		std::unique_ptr<QUndoCommand> command(new CreateOceanCrustUndoCommand(
				d_feature_focus, d_model_interface, collection, features));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		name_ocean_crust_layer(d_application_state, d_view_state, collection);
		return Result(OPERATION_COMPLETED,
				QObject::tr("Created two OceanicCrust polygons for Plates %1 and %2 spanning %3-%4 Ma. Plate motion was not changed.")
						.arg(left_plate_id).arg(right_plate_id)
						.arg(older_time, 0, 'f', 1).arg(current_time, 0, 'f', 1));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not create the ocean-crust age band: %1").arg(exception.what()));
	}
}
