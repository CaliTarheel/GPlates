/* $Id$ */

/**
 * \file
 * Implements the reviewable Worldbuilding Pasta subduction-effects workflow.
 */

#include "GenerateSubductionEffectsOperation.h"

#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QObject>
#include <QUndoCommand>

#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "UndoRedo.h"
#include "WorldbuildingFeatureCollectionUtils.h"
#include "FeatureEventVersioner.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "feature-visitors/PropertyValueFinder.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/FiniteRotation.h"

#include "model/FeatureCollectionHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/EnumerationType.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsBoolean.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
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
			GPlatesModel::integer_plate_id_type plate_id)
	{
		add_property(feature, GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		add_property(feature, GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(start_time),
						GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
		add_property(feature, GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));
		add_property(feature, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
				GPlatesModel::ModelUtils::create_gml_time_instant(
						GPlatesPropertyValues::GeoTimeInstant(start_time)));
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_island_arc(
			const QString &name,
			double start_time,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &line)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("IslandArc"));
		const GPlatesModel::FeatureHandle::weak_ref ref = feature->reference();
		add_common_properties(ref, name, start_time, plate_id);
		add_property(ref, GPlatesModel::PropertyName::create_gpml("isActive"),
				GPlatesPropertyValues::XsBoolean::create(true));
		add_property(ref, GPlatesModel::PropertyName::create_gpml("outlineOf"),
				GPlatesAppLogic::GeometryUtils::create_polyline_geometry_property_value(line));
		return feature;
	}

	struct VisibleLand
	{
		VisibleLand(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
				GPlatesModel::integer_plate_id_type plate_id_) :
			polygon(polygon_), plate_id(plate_id_)
		{  }

		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
		GPlatesModel::integer_plate_id_type plate_id;
	};

	std::vector<VisibleLand> gather_visible_land(
			GPlatesAppLogic::ApplicationState &application_state)
	{
		static const GPlatesModel::FeatureType CONTINENTAL_CRUST =
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust");
		std::vector<VisibleLand> result;
		std::set<const GPlatesModel::TopLevelProperty *> seen;
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
						rfg.get_feature_ref()->feature_type() != CONTINENTAL_CRUST ||
						!rfg.reconstruction_plate_id() ||
						!seen.insert((*rfg.property()).get()).second)
				{
					continue;
				}
				const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
								rfg.reconstructed_geometry().get());
				if (polygon)
				{
					result.push_back(VisibleLand(
							polygon->get_non_null_pointer(), *rfg.reconstruction_plate_id()));
				}
			}
		}
		return result;
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_orogenic_belt(
			const QString &name,
			double start_time,
			GPlatesModel::integer_plate_id_type plate_id,
			bool overriding_is_left,
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("OrogenicBelt"));
		const GPlatesModel::FeatureHandle::weak_ref ref = feature->reference();
		add_common_properties(ref, name, start_time, plate_id);
		add_property(ref, GPlatesModel::PropertyName::create_gpml("subductionPolarity"),
				GPlatesPropertyValues::Enumeration::create(
						GPlatesPropertyValues::EnumerationType::create_gpml(
								"SubductionPolarityEnumeration"),
						overriding_is_left ? "Left" : "Right"));
		add_property(ref,
				overriding_is_left
						? GPlatesModel::PropertyName::create_gpml("leftPlate")
						: GPlatesModel::PropertyName::create_gpml("rightPlate"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));
		// Artifexia stores active orogenic polygons in the inherited centerLineOf
		// property, and GPGIM permits polygon geometry there.
		add_property(ref, GPlatesModel::PropertyName::create_gpml("centerLineOf"),
				GPlatesPropertyValues::GmlPolygon::create(polygon));
		return feature;
	}

	class CreateEffectsUndoCommand : public QUndoCommand
	{
	public:
		CreateEffectsUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> &features,
				QUndoCommand *parent = NULL) :
			QUndoCommand(parent),
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_collection(collection),
			d_features(features)
		{
			setText(QObject::tr("create reviewed subduction effects"));
		}

		virtual void redo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_iterators.clear();
			for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::iterator feature_iter =
					d_features.begin(); feature_iter != d_features.end(); ++feature_iter)
			{
				const GPlatesModel::FeatureCollectionHandle::iterator inserted =
						d_collection->add(*feature_iter);
				d_iterators.push_back(inserted);
				*feature_iter = *inserted;
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
			for (std::size_t reverse = d_iterators.size(); reverse > 0; --reverse)
			{
				const std::size_t index = reverse - 1;
				if (d_iterators[index].is_still_valid())
				{
					d_features[index] = d_collection->remove(d_iterators[index]);
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
				const boost::optional<GPlatesAppLogic::Layer::InputFile> file = input_iter->get_input_file();
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


GPlatesViewOperations::GenerateSubductionEffectsOperation::Options::Options() :
	effect_type(SubductionEffectsGeometry::ISLAND_ARC),
	flip_declared_polarity(false),
	allow_early_island_arc(false),
	island_arc_delay_ma(50.0),
	trim_start_percent(0),
	trim_end_percent(0),
	offset_km(220.0),
	island_irregularity(0.22),
	belt_width_km(100.0),
	lifecycle_event(SubductionLifecyclePlanner::CONTINUE_SUBDUCTION),
	subducting_plate(0),
	migration_offset_km(0),
	lifecycle_duration_ma(0),
	isolates_plate_fragment(false)
{  }


GPlatesViewOperations::GenerateSubductionEffectsOperation::GenerateSubductionEffectsOperation(
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


GPlatesViewOperations::GenerateSubductionEffectsOperation::Result
GPlatesViewOperations::GenerateSubductionEffectsOperation::arm_subduction_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_SUBDUCTION;
	return Result(SELECTION_ARMED,
			QObject::tr("Select a SubductionZone polyline in the globe, map, or feature table."));
}


GPlatesViewOperations::GenerateSubductionEffectsOperation::Result
GPlatesViewOperations::GenerateSubductionEffectsOperation::arm_continent_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_CONTINENT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the overriding ContinentalCrust polygon, or leave it unset for an island arc."));
}


GPlatesViewOperations::GenerateSubductionEffectsOperation::Result
GPlatesViewOperations::GenerateSubductionEffectsOperation::capture_armed_selection()
{
	if (d_selection_mode == NOT_SELECTING)
	{
		return Result(OPERATION_CANCELLED, QString());
	}
	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That item has no editable reconstructed geometry; selection remains armed."));
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

	if (d_selection_mode == SELECTING_SUBDUCTION)
	{
		static const GPlatesModel::FeatureType SUBDUCTION =
				GPlatesModel::FeatureType::create_gpml("SubductionZone");
		const GPlatesMaths::PolylineOnSphere *polyline =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
						(*rfg)->reconstructed_geometry().get());
		if (!polyline || d_feature_focus.focused_feature()->feature_type() != SUBDUCTION)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("That is not a SubductionZone polyline; selection remains armed."));
		}
		const boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
				(*rfg)->reconstruction_plate_id();
		if (!plate_id)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selected subduction zone has no overriding reconstruction plate ID."));
		}
		bool declared_left = true;
		const boost::optional<GPlatesPropertyValues::Enumeration::non_null_ptr_to_const_type> polarity =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::Enumeration>(
						d_feature_focus.focused_feature(),
						GPlatesModel::PropertyName::create_gpml("subductionPolarity"));
		if (polarity)
		{
			declared_left = (*polarity)->get_value() ==
					GPlatesPropertyValues::EnumerationContent("Left");
		}
		double start_time = current_time;
		const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type> valid_time =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GmlTimePeriod>(
						d_feature_focus.focused_feature(),
						GPlatesModel::PropertyName::create_gml("validTime"));
		if (valid_time && (*valid_time)->begin()->get_time_position().is_real())
		{
			start_time = (*valid_time)->begin()->get_time_position().value();
		}
		GPlatesModel::integer_plate_id_type subducting_plate = 0;
		const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> left_plate =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
						d_feature_focus.focused_feature(), GPlatesModel::PropertyName::create_gpml("leftPlate"));
		const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> right_plate =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
						d_feature_focus.focused_feature(), GPlatesModel::PropertyName::create_gpml("rightPlate"));
		if (left_plate && (*left_plate)->get_value() != *plate_id)
			subducting_plate = (*left_plate)->get_value();
		else if (right_plate && (*right_plate)->get_value() != *plate_id)
			subducting_plate = (*right_plate)->get_value();
		d_captured_subduction = CapturedSubduction(
				d_feature_focus.focused_feature(), polyline->get_non_null_pointer(),
				*plate_id, subducting_plate, declared_left, start_time, current_time);
		if (d_captured_continent && d_captured_continent->plate_id != *plate_id)
		{
			d_captured_continent = boost::none;
		}
		d_selection_mode = NOT_SELECTING;
		return Result(SUBDUCTION_CAPTURED,
				QObject::tr("Subduction zone captured. Select overriding continental crust for mountains, or preview an island arc."));
	}

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
	if (d_captured_subduction && *plate_id != d_captured_subduction->overriding_plate)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That continent is Plate %1, but the selected subduction zone belongs to overriding Plate %2.")
						.arg(*plate_id).arg(d_captured_subduction->overriding_plate));
	}
	d_captured_continent = CapturedContinent(
			d_feature_focus.focused_feature(), polygon->get_non_null_pointer(),
			*plate_id, current_time);
	d_selection_mode = NOT_SELECTING;
	return Result(CONTINENT_CAPTURED,
			QObject::tr("Overriding continental crust captured. Auto mode will propose an Andean belt."));
}


bool
GPlatesViewOperations::GenerateSubductionEffectsOperation::has_subduction() const
{
	return d_captured_subduction && d_captured_subduction->feature.is_valid();
}


bool
GPlatesViewOperations::GenerateSubductionEffectsOperation::has_continent() const
{
	return d_captured_continent && d_captured_continent->feature.is_valid();
}


bool
GPlatesViewOperations::GenerateSubductionEffectsOperation::declared_overriding_side_is_left() const
{
	return !d_captured_subduction || d_captured_subduction->declared_left;
}


bool
GPlatesViewOperations::GenerateSubductionEffectsOperation::recommended_polarity_flip() const
{
	if (!has_subduction() || !has_continent())
	{
		return false;
	}
	try
	{
		const double declared_fraction =
				SubductionEffectsGeometry::calculate_overriding_containment_fraction(
						d_captured_subduction->polyline, d_captured_continent->polygon,
						d_captured_subduction->declared_left);
		const double flipped_fraction =
				SubductionEffectsGeometry::calculate_overriding_containment_fraction(
						d_captured_subduction->polyline, d_captured_continent->polygon,
						!d_captured_subduction->declared_left);
		return flipped_fraction > 0.45 && flipped_fraction > declared_fraction + 0.20;
	}
	catch (const std::exception &)
	{
		return false;
	}
}


double
GPlatesViewOperations::GenerateSubductionEffectsOperation::subduction_age_ma() const
{
	if (!d_captured_subduction)
	{
		return 0;
	}
	return std::max(0.0,
			d_captured_subduction->start_time -
			d_application_state.get_current_reconstruction().get_reconstruction_time());
}


GPlatesModel::integer_plate_id_type
GPlatesViewOperations::GenerateSubductionEffectsOperation::suggested_subducting_plate() const
{
	return d_captured_subduction ? d_captured_subduction->subducting_plate : 0;
}


QString
GPlatesViewOperations::GenerateSubductionEffectsOperation::subduction_status() const
{
	if (!has_subduction())
	{
		return QObject::tr("Subduction zone: not selected");
	}
	return QObject::tr("Subduction zone: overriding Plate %1, subducting Plate %2, %3 polarity, began %4 Ma (%5 Ma old)")
			.arg(d_captured_subduction->overriding_plate)
			.arg(d_captured_subduction->subducting_plate == 0
					? QObject::tr("not recorded") : QString::number(d_captured_subduction->subducting_plate))
			.arg(d_captured_subduction->declared_left ? QObject::tr("Left") : QObject::tr("Right"))
			.arg(d_captured_subduction->start_time, 0, 'f', 1)
			.arg(subduction_age_ma(), 0, 'f', 1);
}


QString
GPlatesViewOperations::GenerateSubductionEffectsOperation::continent_status() const
{
	if (!has_continent())
	{
		return QObject::tr("Overriding crust: none (island-arc context)");
	}
	return QObject::tr("Overriding crust: ContinentalCrust on Plate %1")
			.arg(d_captured_continent->plate_id);
}


GPlatesViewOperations::GenerateSubductionEffectsOperation::Result
GPlatesViewOperations::GenerateSubductionEffectsOperation::preview(const Options &options)
{
	clear_preview();
	if (!has_subduction())
	{
		return Result(OPERATION_ERROR, QObject::tr("Select a subduction zone first."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_captured_subduction->reconstruction_time) > 1e-9 ||
			(d_captured_continent &&
			 std::fabs(current_time - d_captured_continent->reconstruction_time) > 1e-9))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after selection. Re-select the source features."));
	}
	if (options.effect_type != SubductionEffectsGeometry::ISLAND_ARC && !has_continent())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select overriding ContinentalCrust before proposing a mountain belt."));
	}
	if (options.effect_type == SubductionEffectsGeometry::ISLAND_ARC &&
			!options.allow_early_island_arc &&
			subduction_age_ma() + 1e-9 < options.island_arc_delay_ma)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Worldbuilding Pasta delays island-arc emergence by %1 Ma. This trench is only %2 Ma old; advance to %3 Ma or enable the explicit early-arc override.")
						.arg(options.island_arc_delay_ma, 0, 'f', 0)
						.arg(subduction_age_ma(), 0, 'f', 1)
						.arg(d_captured_subduction->start_time - options.island_arc_delay_ma, 0, 'f', 1));
	}
	SubductionLifecyclePlanner::Request lifecycle_request;
	lifecycle_request.event_type = options.lifecycle_event;
	lifecycle_request.event_time = current_time;
	lifecycle_request.trench_start_time = d_captured_subduction->start_time;
	lifecycle_request.overriding_plate = d_captured_subduction->overriding_plate;
	lifecycle_request.subducting_plate = options.subducting_plate;
	lifecycle_request.polarity_left = d_captured_subduction->declared_left;
	lifecycle_request.successor_polarity_left =
			d_captured_subduction->declared_left != options.flip_declared_polarity;
	lifecycle_request.migration_offset_km = options.migration_offset_km;
	lifecycle_request.duration_ma = options.lifecycle_duration_ma;
	lifecycle_request.isolates_plate_fragment = options.isolates_plate_fragment;
	const SubductionLifecyclePlanner::Plan lifecycle_plan =
			SubductionLifecyclePlanner::plan(lifecycle_request);
	if (!lifecycle_plan.valid)
		return Result(OPERATION_ERROR, QObject::tr("Lifecycle plan is not valid:\n%1")
				.arg(lifecycle_plan.errors.join("\n")));
	QString lifecycle_review = QObject::tr("\nLifecycle transaction review:");
	for (QStringList::const_iterator warning = lifecycle_plan.warnings.begin();
			warning != lifecycle_plan.warnings.end(); ++warning)
		lifecycle_review += QObject::tr("\nWARNING: %1").arg(*warning);
	for (QStringList::const_iterator step = lifecycle_plan.atomic_steps.begin();
			step != lifecycle_plan.atomic_steps.end(); ++step)
		lifecycle_review += QObject::tr("\n- %1").arg(*step);
	if (options.lifecycle_event != SubductionLifecyclePlanner::CONTINUE_SUBDUCTION)
		lifecycle_review += QObject::tr(
				"\nPLAN ONLY: this PR audits the transition but does not yet compose trench versioning, crust retirement, topology repair, or plate birth. Commit is disabled for this event.");

	try
	{
		SubductionEffectsGeometry::Parameters parameters;
		parameters.effect_type = options.effect_type;
		parameters.overriding_side_is_left =
				d_captured_subduction->declared_left != options.flip_declared_polarity;
		parameters.trim_start_percent = options.trim_start_percent;
		parameters.trim_end_percent = options.trim_end_percent;
		parameters.trench_to_effect_offset_km = options.offset_km;
		parameters.island_irregularity = options.island_irregularity;
		parameters.belt_width_km = options.belt_width_km;
		const boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> continent =
				has_continent()
						? boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>(
								d_captured_continent->polygon)
						: boost::none;
		if (continent)
		{
			const double polarity_probe_km = std::max(100.0, std::min(250.0, options.offset_km));
			const double selected_fraction =
					SubductionEffectsGeometry::calculate_overriding_containment_fraction(
							d_captured_subduction->polyline, *continent,
							parameters.overriding_side_is_left, polarity_probe_km);
			const double opposite_fraction =
					SubductionEffectsGeometry::calculate_overriding_containment_fraction(
							d_captured_subduction->polyline, *continent,
							!parameters.overriding_side_is_left, polarity_probe_km);
			if (opposite_fraction > 0.45 && opposite_fraction > selected_fraction + 0.20)
			{
				return Result(OPERATION_ERROR,
						QObject::tr("The chosen polarity places only %1% of the proposal probes in the overriding crust, while the opposite side places %2% there. Toggle Flip declared trench polarity, then preview again.")
								.arg(100.0 * selected_fraction, 0, 'f', 0)
								.arg(100.0 * opposite_fraction, 0, 'f', 0));
			}
		}
		const SubductionEffectsGeometry::Result geometry =
				SubductionEffectsGeometry::generate(
						d_captured_subduction->polyline, parameters, continent);
		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_subduction_teeth_polyline(
						d_captured_subduction->polyline,
						parameters.overriding_side_is_left,
						GPlatesGui::Colour::get_yellow(), 3.0f));
		std::vector<Preview::LandBelt> land_belts;
		if (options.effect_type == SubductionEffectsGeometry::ISLAND_ARC && geometry.guide)
		{
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							*geometry.guide, GPlatesGui::Colour::get_aqua(), 4.0f));
			const std::vector<VisibleLand> visible_land = gather_visible_land(d_application_state);
			for (std::vector<VisibleLand>::const_iterator land_iter = visible_land.begin();
					land_iter != visible_land.end(); ++land_iter)
			{
				const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> belts =
						SubductionEffectsGeometry::generate_land_intersection_belts(
								*geometry.guide, land_iter->polygon, options.belt_width_km);
				for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
						belt_iter = belts.begin(); belt_iter != belts.end(); ++belt_iter)
				{
					land_belts.push_back(Preview::LandBelt(*belt_iter, land_iter->plate_id));
					d_preview_layer->add_rendered_geometry(
							RenderedGeometryFactory::create_rendered_polygon_on_sphere(
									*belt_iter, GPlatesGui::Colour(1.0f, 0.5f, 0.0f),
									3.0f, true, GPlatesGui::Colour(1.0f, 0.5f, 0.0f)));
				}
			}
		}
		else if (geometry.guide)
		{
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							*geometry.guide, GPlatesGui::Colour::get_white(), 2.0f));
		}
		const GPlatesGui::Colour proposal_colour(1.0f, 0.5f, 0.0f);
		for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
				polygon_iter = geometry.polygons.begin();
			polygon_iter != geometry.polygons.end(); ++polygon_iter)
		{
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							*polygon_iter, proposal_colour, 3.0f, true, proposal_colour));
		}
		d_preview = Preview(options, geometry, lifecycle_plan, land_belts);
		const QString side = parameters.overriding_side_is_left
				? QObject::tr("left") : QObject::tr("right");
		if (options.effect_type == SubductionEffectsGeometry::ISLAND_ARC)
		{
			return Result(PREVIEW_READY,
					QObject::tr("Previewed one editable island-arc line over %1 km on the %2/overriding side, plus %3 land-intersection mountain belt(s). Lifecycle: %4 (%5 atomic step(s)). Aqua is arc notation; orange is mountain-building confined to visible ContinentalCrust/terranes.%6")
							.arg(geometry.metrics.selected_length_km, 0, 'f', 0)
							.arg(side)
							.arg(land_belts.size())
							.arg(lifecycle_plan.event_name)
							.arg(lifecycle_plan.atomic_steps.size())
							.arg(lifecycle_review));
		}
		return Result(PREVIEW_READY,
				QObject::tr("Previewed a contained %1 km-wide %2 belt over %3 km on the %4/overriding side. Crust-side agreement is %5%. Lifecycle: %6 (%7 atomic step(s)).%8")
						.arg(options.belt_width_km, 0, 'f', 0)
						.arg(options.effect_type == SubductionEffectsGeometry::LARAMIDE_OROGENY
								? QObject::tr("Laramide") : QObject::tr("Andean"))
						.arg(geometry.metrics.selected_length_km, 0, 'f', 0)
						.arg(side)
						.arg(100.0 * geometry.metrics.overriding_containment_fraction, 0, 'f', 0)
						.arg(lifecycle_plan.event_name)
						.arg(lifecycle_plan.atomic_steps.size())
						.arg(lifecycle_review));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not generate the subduction-effects preview: %1")
						.arg(exception.what()));
	}
}


GPlatesViewOperations::GenerateSubductionEffectsOperation::Result
GPlatesViewOperations::GenerateSubductionEffectsOperation::commit()
{
	if (!d_preview || !has_subduction())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Create and review a preview before committing subduction effects."));
	}
	if (d_preview->options.lifecycle_event != SubductionLifecyclePlanner::CONTINUE_SUBDUCTION)
	{
		return Result(OPERATION_ERROR, QObject::tr(
				"This lifecycle transition is a reviewed plan only. No features were changed. Return to Continued subduction to commit ordinary arc/orogeny effects, or use the delegated crust-retirement, topology, collision, and plate-birth tools named in the plan."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_captured_subduction->reconstruction_time) > 1e-9)
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
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> island_arc_features;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> active_orogeny_features;
		if (d_preview->options.effect_type == SubductionEffectsGeometry::ISLAND_ARC)
		{
			if (!d_preview->geometry.guide)
			{
				throw std::runtime_error("The reviewed island-arc line is missing.");
			}
			const GPlatesMaths::FiniteRotation overriding_rotation =
					tree->get_composed_absolute_rotation(
							d_captured_subduction->overriding_plate);
			const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type present_arc =
					GPlatesMaths::get_reverse(overriding_rotation) *
							*d_preview->geometry.guide;
			island_arc_features.push_back(create_island_arc(
					QObject::tr("Plate %1 Island Arc")
							.arg(d_captured_subduction->overriding_plate),
					current_time, d_captured_subduction->overriding_plate,
					present_arc));
			const bool overriding_is_left = d_captured_subduction->declared_left !=
					d_preview->options.flip_declared_polarity;
			for (std::size_t index = 0; index < d_preview->land_belts.size(); ++index)
			{
				const GPlatesMaths::FiniteRotation land_rotation =
						tree->get_composed_absolute_rotation(
								d_preview->land_belts[index].plate_id);
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type present_belt =
						GPlatesMaths::get_reverse(land_rotation) *
								d_preview->land_belts[index].polygon;
				active_orogeny_features.push_back(create_orogenic_belt(
						QObject::tr("Plate %1 Arc-Intersection Active Orogeny %2")
								.arg(d_preview->land_belts[index].plate_id).arg(index + 1),
						current_time, d_preview->land_belts[index].plate_id,
						overriding_is_left, present_belt));
			}
		}
		else
		{
			const bool laramide =
					d_preview->options.effect_type == SubductionEffectsGeometry::LARAMIDE_OROGENY;
			const GPlatesMaths::FiniteRotation overriding_rotation =
					tree->get_composed_absolute_rotation(
							d_captured_subduction->overriding_plate);
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type present_belt =
					GPlatesMaths::get_reverse(overriding_rotation) *
							d_preview->geometry.polygons.front();
			active_orogeny_features.push_back(create_orogenic_belt(
					QObject::tr("Plate %1 %2 Active Orogeny")
							.arg(d_captured_subduction->overriding_plate)
							.arg(laramide ? QObject::tr("Laramide") : QObject::tr("Andean")),
					current_time, d_captured_subduction->overriding_plate,
					d_captured_subduction->declared_left !=
							d_preview->options.flip_declared_polarity,
					present_belt));
		}

		QStringList output_ids;
		for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::const_iterator feature =
				island_arc_features.begin(); feature != island_arc_features.end(); ++feature)
		{
			output_ids.append((*feature)->feature_id().get().qstring());
		}
		for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::const_iterator feature =
				active_orogeny_features.begin(); feature != active_orogeny_features.end(); ++feature)
		{
			output_ids.append((*feature)->feature_id().get().qstring());
		}
		const FeatureEventVersioner::EventRecord lifecycle_event = FeatureEventVersioner::make_event(
				QString("subduction.%1").arg(d_preview->lifecycle_plan.event_name).replace(' ', '-'),
				current_time, "2.0",
				QStringList() << d_captured_subduction->feature->feature_id().get().qstring(),
				output_ids, "subduction-effects");
		for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::const_iterator feature =
				island_arc_features.begin(); feature != island_arc_features.end(); ++feature)
		{
			FeatureEventVersioner::set_properties((*feature)->reference(),
					FeatureEventVersioner::properties_for_event(
							(*feature)->reference(), FeatureEventVersioner::KEEP_VALID_TIME, lifecycle_event));
		}
		for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::const_iterator feature =
				active_orogeny_features.begin(); feature != active_orogeny_features.end(); ++feature)
		{
			FeatureEventVersioner::set_properties((*feature)->reference(),
					FeatureEventVersioner::properties_for_event(
							(*feature)->reference(), FeatureEventVersioner::KEEP_VALID_TIME, lifecycle_event));
		}

		GPlatesAppLogic::FeatureCollectionFileIO &file_io =
				d_application_state.get_feature_collection_file_io();
		std::unique_ptr<QUndoCommand> command(new QUndoCommand());
		command->setText(QObject::tr("create reviewed subduction effects"));
		if (!island_arc_features.empty())
		{
			const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
					resolve_or_create_worldbuilding_feature_collection(
							file_io, d_application_state.get_feature_collection_file_state(),
							QString::fromLatin1("island-arcs-terranes"),
							QObject::tr("Island Arcs and Terranes")).get_file().get_feature_collection();
			new CreateEffectsUndoCommand(
					d_feature_focus, d_model_interface, collection, island_arc_features, command.get());
			name_layer(d_application_state, d_view_state, collection,
					QObject::tr("Island Arcs and Terranes"));
		}
		if (!active_orogeny_features.empty())
		{
			const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
					resolve_or_create_worldbuilding_feature_collection(
							file_io, d_application_state.get_feature_collection_file_state(),
							QString::fromLatin1("active-orogenies"),
							QObject::tr("Active Orogenies")).get_file().get_feature_collection();
			new CreateEffectsUndoCommand(
					d_feature_focus, d_model_interface, collection, active_orogeny_features, command.get());
			name_layer(d_application_state, d_view_state, collection, QObject::tr("Active Orogenies"));
		}
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		const unsigned int feature_count = island_arc_features.size() + active_orogeny_features.size();
		const QString output_summary = !island_arc_features.empty() && !active_orogeny_features.empty()
				? QObject::tr("Island Arc and Active Orogeny")
				: (!island_arc_features.empty() ? QObject::tr("Island Arc") : QObject::tr("Active Orogeny"));
		clear_preview();
		return Result(EFFECTS_COMMITTED,
				QObject::tr("Created %1 native, editable %2 feature(s) at %3 Ma; the island-arc line follows overriding Plate %4, while intersection mountains follow their land plates.")
						.arg(feature_count).arg(output_summary)
						.arg(current_time, 0, 'f', 1)
						.arg(d_captured_subduction->overriding_plate));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not commit subduction effects: %1").arg(exception.what()));
	}
}


void
GPlatesViewOperations::GenerateSubductionEffectsOperation::clear_continent()
{
	clear_preview();
	d_captured_continent = boost::none;
}


void
GPlatesViewOperations::GenerateSubductionEffectsOperation::clear_preview()
{
	d_preview_layer->clear_rendered_geometries();
	d_preview = boost::none;
}


void
GPlatesViewOperations::GenerateSubductionEffectsOperation::reset()
{
	clear_preview();
	d_selection_mode = NOT_SELECTING;
	d_captured_subduction = boost::none;
	d_captured_continent = boost::none;
}
