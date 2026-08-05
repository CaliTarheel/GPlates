/* $Id$ */

/**
 * \file
 * Implements the reviewable Worldbuilding Pasta LIP and hotspot workflow.
 */

#include "GenerateMantleEventsOperation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include <QObject>
#include <QUndoCommand>

#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "UndoRedo.h"
#include "WorldbuildingFeatureCollectionUtils.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/FiniteRotation.h"

#include "model/FeatureCollectionHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlPoint.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GmlTimeInstant.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlArray.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/StructuralType.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	struct FeatureGroup
	{
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> features;
	};

	void add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &property_name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, property_name, value))
		{
			throw std::runtime_error(QString("Unable to add required property '%1'.")
					.arg(property_name.get_name().qstring()).toStdString());
		}
	}

	void add_common_properties(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const QString &name,
			double start_time,
			const boost::optional<double> &end_time,
			GPlatesModel::integer_plate_id_type plate_id,
			double geometry_import_time)
	{
		add_property(feature, GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		add_property(feature, GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(start_time),
						end_time
								? GPlatesPropertyValues::GeoTimeInstant(*end_time)
								: GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
		add_property(feature, GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));
		add_property(feature, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
				GPlatesModel::ModelUtils::create_gml_time_instant(
						GPlatesPropertyValues::GeoTimeInstant(geometry_import_time)));
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_lip(
			const QString &name,
			double start_time,
			const boost::optional<double> &end_time,
			double geometry_import_time,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &outline)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("LargeIgneousProvince"));
		const GPlatesModel::FeatureHandle::weak_ref ref = feature->reference();
		add_common_properties(ref, name, start_time, end_time, plate_id, geometry_import_time);
		add_property(ref, GPlatesModel::PropertyName::create_gpml("outlineOf"),
				GPlatesPropertyValues::GmlPolygon::create(outline));
		return feature;
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_hotspot(
			const QString &name,
			double start_time,
			double end_time,
			GPlatesModel::integer_plate_id_type mantle_plate_id,
			const GPlatesMaths::PointOnSphere &present_day_position)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("HotSpot"));
		const GPlatesModel::FeatureHandle::weak_ref ref = feature->reference();
		add_common_properties(ref, name, start_time, end_time,
				mantle_plate_id, start_time);
		add_property(ref, GPlatesModel::PropertyName::create_gpml("position"),
				GPlatesPropertyValues::GmlPoint::create(present_day_position));
		return feature;
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_motion_path(
			const QString &name,
			double start_time,
			double end_time,
			double step_ma,
			GPlatesModel::integer_plate_id_type mantle_plate_id,
			GPlatesModel::integer_plate_id_type surface_plate_id,
			const GPlatesMaths::PointOnSphere &present_day_seed)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("MotionPath"));
		const GPlatesModel::FeatureHandle::weak_ref ref = feature->reference();
		// Trails remain visible after the hotspot ceases, matching Artifexia's
		// persistent MotionPath records; the times array bounds the active track.
		add_common_properties(ref, name, start_time, boost::none,
				mantle_plate_id, start_time);
		add_property(ref, GPlatesModel::PropertyName::create_gpml("relativePlate"),
				GPlatesPropertyValues::GpmlPlateId::create(surface_plate_id));

		std::vector<GPlatesModel::PropertyValue::non_null_ptr_type> periods;
		for (double younger = end_time; younger < start_time - 1e-9; )
		{
			const double older = std::min(start_time, younger + step_ma);
			periods.push_back(GPlatesPropertyValues::GmlTimePeriod::create(
					GPlatesPropertyValues::GmlTimeInstant::create(
							GPlatesPropertyValues::GeoTimeInstant(older)),
					GPlatesPropertyValues::GmlTimeInstant::create(
							GPlatesPropertyValues::GeoTimeInstant(younger))));
			younger = older;
		}
		if (periods.empty())
		{
			throw std::runtime_error("A hotspot trail requires at least one output-time period.");
		}
		add_property(ref, GPlatesModel::PropertyName::create_gpml("times"),
				GPlatesPropertyValues::GpmlArray::create(
						periods,
						GPlatesPropertyValues::StructuralType::create_gml("TimePeriod")));
		add_property(ref, GPlatesModel::PropertyName::create_gpml("seedPoints"),
				GPlatesPropertyValues::GmlPoint::create(present_day_seed));
		return feature;
	}

	class CreateMantleEventsUndoCommand : public QUndoCommand
	{
	public:
		CreateMantleEventsUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const std::vector<FeatureGroup> &groups) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_groups(groups)
		{
			setText(QObject::tr("create reviewed LIP and hotspot event"));
		}

		virtual void redo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::vector<FeatureGroup>::iterator group_iter = d_groups.begin();
					group_iter != d_groups.end(); ++group_iter)
			{
				if (!group_iter->collection.is_valid())
				{
					continue;
				}
				for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::iterator
						feature_iter = group_iter->features.begin();
						feature_iter != group_iter->features.end(); ++feature_iter)
				{
					if (!(*feature_iter)->parent_ptr())
					{
						const GPlatesModel::FeatureCollectionHandle::iterator inserted =
								group_iter->collection->add(*feature_iter);
						*feature_iter = *inserted;
					}
				}
			}
			guard.release_guard();
		}

		virtual void undo()
		{
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::vector<FeatureGroup>::reverse_iterator group_iter = d_groups.rbegin();
					group_iter != d_groups.rend(); ++group_iter)
			{
				for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::reverse_iterator
						feature_iter = group_iter->features.rbegin();
						feature_iter != group_iter->features.rend(); ++feature_iter)
				{
					if ((*feature_iter)->parent_ptr())
					{
						(*feature_iter)->remove_from_parent();
					}
				}
			}
			guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<FeatureGroup> d_groups;
	};

	boost::optional<GPlatesAppLogic::Layer> find_reconstruct_layer(
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
				const boost::optional<GPlatesAppLogic::Layer::InputFile> file =
						input_iter->get_input_file();
				if (file && file->get_feature_collection() == collection)
				{
					return layer;
				}
			}
		}
		return boost::none;
	}

	void name_layer(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesPresentation::ViewState &view_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
			const QString &name)
	{
		const boost::optional<GPlatesAppLogic::Layer> layer =
				find_reconstruct_layer(application_state, collection);
		if (!layer)
		{
			return;
		}
		boost::shared_ptr<GPlatesPresentation::VisualLayer> visual_layer =
				view_state.get_visual_layers().get_visual_layer(*layer).lock();
		if (visual_layer)
		{
			visual_layer->set_custom_name(name);
			visual_layer->set_visible(true);
		}
	}
}


GPlatesViewOperations::GenerateMantleEventsOperation::Options::Options() :
	rift_triggered(true),
	lip_diameter_km(700.0),
	lip_irregularity(0.28),
	maximum_segment_length_km(75.0),
	random_seed(1),
	active_lip_duration_ma(10.0),
	create_hotspot(true),
	hotspot_lifetime_ma(150.0),
	trail_step_ma(10.0),
	mantle_plate_id(1)
{ }


GPlatesViewOperations::GenerateMantleEventsOperation::GenerateMantleEventsOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface()),
	d_selection_mode(NOT_SELECTING),
	d_preview_layer(view_state.get_rendered_geometry_collection().
			create_child_rendered_layer_and_transfer_ownership(
					RenderedGeometryCollection::RECONSTRUCTION_LAYER))
{
	d_preview_layer->set_active(true);
}


GPlatesViewOperations::GenerateMantleEventsOperation::Result
GPlatesViewOperations::GenerateMantleEventsOperation::arm_continent_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_CONTINENT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the ContinentalCrust polygon that will host the mantle event."));
}


GPlatesViewOperations::GenerateMantleEventsOperation::Result
GPlatesViewOperations::GenerateMantleEventsOperation::arm_rift_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_RIFT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select an optional rift polyline. Any source polyline is accepted; its in-continent section anchors a rift-triggered LIP."));
}


GPlatesViewOperations::GenerateMantleEventsOperation::Result
GPlatesViewOperations::GenerateMantleEventsOperation::capture_armed_selection()
{
	if (d_selection_mode == NOT_SELECTING)
	{
		return Result(OPERATION_CANCELLED, QString());
	}
	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That item has no editable reconstructed source geometry; selection remains armed."));
	}
	const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> rfg =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!rfg)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select a source feature rather than topology-resolved geometry."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();

	if (d_selection_mode == SELECTING_CONTINENT)
	{
		static const GPlatesModel::FeatureType CONTINENTAL_CRUST =
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust");
		const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
						(*rfg)->reconstructed_geometry().get());
		if (!polygon || d_feature_focus.focused_feature()->feature_type() != CONTINENTAL_CRUST)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("That is not a ContinentalCrust polygon; selection remains armed."));
		}
		const boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
				(*rfg)->reconstruction_plate_id();
		if (!plate_id)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selected continental crust has no reconstruction plate ID."));
		}
		d_continent = CapturedContinent(
				d_feature_focus.focused_feature(), polygon->get_non_null_pointer(),
				*plate_id, current_time);
		d_selection_mode = NOT_SELECTING;
		return Result(CONTINENT_CAPTURED,
				QObject::tr("Host continental crust captured on Plate %1.").arg(*plate_id));
	}

	const GPlatesMaths::PolylineOnSphere *polyline =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
					(*rfg)->reconstructed_geometry().get());
	if (!polyline)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That source feature is not a polyline; selection remains armed."));
	}
	d_rift = CapturedRift(
			d_feature_focus.focused_feature(), polyline->get_non_null_pointer(), current_time);
	d_selection_mode = NOT_SELECTING;
	return Result(RIFT_CAPTURED,
			QObject::tr("Rift trigger captured from %1.")
					.arg(d_feature_focus.focused_feature()->feature_type().get_name().qstring()));
}


bool GPlatesViewOperations::GenerateMantleEventsOperation::has_continent() const
{
	return d_continent && d_continent->feature.is_valid();
}


bool GPlatesViewOperations::GenerateMantleEventsOperation::has_rift() const
{
	return d_rift && d_rift->feature.is_valid();
}


QString GPlatesViewOperations::GenerateMantleEventsOperation::continent_status() const
{
	return has_continent()
			? QObject::tr("Host: ContinentalCrust on Plate %1").arg(d_continent->plate_id)
			: QObject::tr("Host: not selected");
}


QString GPlatesViewOperations::GenerateMantleEventsOperation::rift_status() const
{
	return has_rift()
			? QObject::tr("Rift trigger: %1 polyline")
					.arg(d_rift->feature->feature_type().get_name().qstring())
			: QObject::tr("Rift trigger: none (random placement available)");
}


GPlatesViewOperations::GenerateMantleEventsOperation::Result
GPlatesViewOperations::GenerateMantleEventsOperation::preview(const Options &options)
{
	clear_preview();
	if (!has_continent())
	{
		return Result(OPERATION_ERROR, QObject::tr("Select host continental crust first."));
	}
	if (options.rift_triggered && !has_rift())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select a rift polyline or switch Placement to Random."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_continent->reconstruction_time) > 1e-9 ||
			(d_rift && std::fabs(current_time - d_rift->reconstruction_time) > 1e-9))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after selection. Re-select the source features."));
	}
	if (options.active_lip_duration_ma <= 0 || options.hotspot_lifetime_ma <= 0 ||
			options.trail_step_ma <= 0 || options.trail_step_ma > options.hotspot_lifetime_ma ||
			options.mantle_plate_id < 0)
	{
		return Result(OPERATION_ERROR, QObject::tr("The event timing or mantle Plate ID is invalid."));
	}

	try
	{
		MantleEventGeometry::Parameters parameters;
		parameters.diameter_km = options.lip_diameter_km;
		parameters.irregularity = options.lip_irregularity;
		parameters.maximum_segment_length_km = options.maximum_segment_length_km;
		parameters.rift_triggered = options.rift_triggered;
		parameters.random_seed = options.random_seed;
		const boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> rift =
				has_rift()
						? boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>(
								d_rift->polyline)
						: boost::none;
		const MantleEventGeometry::Result geometry =
				MantleEventGeometry::generate(d_continent->polygon, rift, parameters);

		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						d_continent->polygon, GPlatesGui::Colour::get_white(), 1.5f));
		if (has_rift())
		{
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							d_rift->polyline, GPlatesGui::Colour(1.0f, 0.5f, 0.0f), 2.0f));
		}
		const GPlatesGui::Colour lip_colour(0.85f, 0.1f, 0.75f);
		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						geometry.lip_outline, lip_colour, 3.0f, true, lip_colour));
		if (options.create_hotspot)
		{
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_point_on_sphere(
							geometry.hotspot_position, GPlatesGui::Colour::get_aqua(), 8.0f));
		}
		d_preview = Preview(options, geometry);
		const QString reduction = geometry.metrics.containment_reductions
				? QObject::tr(" It was reduced %1 time(s) to remain wholly inside the selected crust; effective diameter is %2 km.")
						.arg(geometry.metrics.containment_reductions)
						.arg(geometry.metrics.actual_diameter_km, 0, 'f', 0)
				: QString();
		return Result(PREVIEW_READY,
				QObject::tr("Previewed a %1 km LIP with %2 boundary vertices on Plate %3 using %4 placement.%5 Review the footprint and seed before committing.")
						.arg(geometry.metrics.actual_diameter_km, 0, 'f', 0)
						.arg(geometry.metrics.boundary_vertex_count)
						.arg(d_continent->plate_id)
						.arg(geometry.metrics.used_rift ? QObject::tr("rift-triggered") : QObject::tr("random"))
						.arg(reduction));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not generate the mantle-event preview: %1").arg(exception.what()));
	}
}


GPlatesViewOperations::GenerateMantleEventsOperation::Result
GPlatesViewOperations::GenerateMantleEventsOperation::commit()
{
	if (!d_preview || !has_continent())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Create and review a mantle-event preview before committing."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_continent->reconstruction_time) > 1e-9)
	{
		clear_preview();
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after preview. Re-select and preview again."));
	}

	try
	{
		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction().
						get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesMaths::FiniteRotation host_rotation =
				tree->get_composed_absolute_rotation(d_continent->plate_id);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type present_lip =
				GPlatesMaths::get_reverse(host_rotation) * d_preview->geometry.lip_outline;

		const double active_end = std::max(0.0,
				current_time - d_preview->options.active_lip_duration_ma);
		const QString event_name = QObject::tr("LIP %1 Plate %2")
				.arg(current_time, 0, 'f', 0).arg(d_continent->plate_id);
		GPlatesAppLogic::FeatureCollectionFileIO &file_io =
				d_application_state.get_feature_collection_file_io();
		std::vector<FeatureGroup> groups;
		std::vector<QString> names;

		FeatureGroup active_lip;
		active_lip.collection = create_named_empty_feature_collection(
				file_io, QObject::tr("Active Large Igneous Provinces")).get_file().get_feature_collection();
		active_lip.features.push_back(create_lip(
				event_name, current_time, active_end, current_time,
				d_continent->plate_id, present_lip));
		groups.push_back(active_lip);
		names.push_back(QObject::tr("active LIP"));

		FeatureGroup former_lip;
		former_lip.collection = create_named_empty_feature_collection(
				file_io, QObject::tr("Former Large Igneous Provinces")).get_file().get_feature_collection();
		former_lip.features.push_back(create_lip(
				QObject::tr("Former %1").arg(event_name), active_end, boost::none,
				current_time, d_continent->plate_id, present_lip));
		groups.push_back(former_lip);
		names.push_back(QObject::tr("former LIP"));

		if (d_preview->options.create_hotspot)
		{
			const double hotspot_end = std::max(0.0,
					current_time - d_preview->options.hotspot_lifetime_ma);
			const GPlatesMaths::FiniteRotation mantle_rotation =
					tree->get_composed_absolute_rotation(d_preview->options.mantle_plate_id);
			const GPlatesMaths::PointOnSphere present_hotspot =
					GPlatesMaths::get_reverse(mantle_rotation) *
							d_preview->geometry.hotspot_position;
			const QString hotspot_name = QObject::tr("Hotspot %1 Plate %2")
					.arg(current_time, 0, 'f', 0).arg(d_continent->plate_id);

			FeatureGroup hotspots;
			hotspots.collection = create_named_empty_feature_collection(
					file_io, QObject::tr("Hotspots")).get_file().get_feature_collection();
			hotspots.features.push_back(create_hotspot(
					hotspot_name, current_time, hotspot_end,
					d_preview->options.mantle_plate_id, present_hotspot));
			groups.push_back(hotspots);
			names.push_back(QObject::tr("Hotspots"));

			FeatureGroup trails;
			trails.collection = create_named_empty_feature_collection(
					file_io, QObject::tr("Hotspot Trails")).get_file().get_feature_collection();
			trails.features.push_back(create_motion_path(
					hotspot_name, current_time, hotspot_end,
					d_preview->options.trail_step_ma,
					d_preview->options.mantle_plate_id,
					d_continent->plate_id, present_hotspot));
			groups.push_back(trails);
			names.push_back(QObject::tr("Hotspot trails"));
		}

		std::unique_ptr<QUndoCommand> command(new CreateMantleEventsUndoCommand(
				d_feature_focus, d_model_interface, groups));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		for (unsigned int index = 0; index < groups.size(); ++index)
		{
			name_layer(d_application_state, d_view_state, groups[index].collection, names[index]);
		}
		const QString hotspot_text = d_preview->options.create_hotspot
				? QObject::tr(", a mantle-fixed HotSpot, and a native MotionPath trail relative to surface Plate %1")
						.arg(d_continent->plate_id)
				: QString();
		clear_preview();
		return Result(EVENTS_COMMITTED,
				QObject::tr("Committed an Artifexia-aligned active/former LIP lifecycle%1 at %2 Ma. All generated features remain editable and the transaction is one undo step.")
						.arg(hotspot_text).arg(current_time, 0, 'f', 1));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not commit mantle events: %1").arg(exception.what()));
	}
}


void GPlatesViewOperations::GenerateMantleEventsOperation::clear_rift()
{
	clear_preview();
	d_rift = boost::none;
}


void GPlatesViewOperations::GenerateMantleEventsOperation::clear_preview()
{
	d_preview_layer->clear_rendered_geometries();
	d_preview = boost::none;
}


void GPlatesViewOperations::GenerateMantleEventsOperation::reset()
{
	clear_preview();
	d_selection_mode = NOT_SELECTING;
	d_continent = boost::none;
	d_rift = boost::none;
}
