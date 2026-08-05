/* $Id$ */

/**
 * \file
 * Implements the reviewable Worldbuilding Pasta collision/orogeny workflow.
 */

#include "CollisionOrogenyOperation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QObject>
#include <QUndoCommand>

#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "FeatureEventVersioner.h"
#include "CollisionAccretionGuardrails.h"
#include "PlateEventTransaction.h"
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
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/ProjectTimestampSchedule.h"

#include "feature-visitors/PropertyValueFinder.h"
#include "feature-visitors/GeometrySetter.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/FiniteRotation.h"
#include "maths/GeometryDistance.h"

#include "model/FeatureCollectionHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	typedef std::vector<GPlatesModel::TopLevelProperty::non_null_ptr_type> property_seq_type;
	typedef std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> feature_seq_type;

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
			double begin_time,
			const boost::optional<double> &end_time,
			double geometry_import_time,
			GPlatesModel::integer_plate_id_type plate_id)
	{
		add_property(feature, GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		add_property(feature, GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(begin_time),
						end_time
								? GPlatesPropertyValues::GeoTimeInstant(*end_time)
								: GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
		add_property(feature, GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));
		add_property(feature, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
				GPlatesModel::ModelUtils::create_gml_time_instant(
						GPlatesPropertyValues::GeoTimeInstant(geometry_import_time)));
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_suture(
			const QString &name,
			double collision_time,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("Suture"));
		add_common_properties(feature->reference(), name, collision_time, boost::none,
				collision_time, plate_id);
		add_property(feature->reference(), GPlatesModel::PropertyName::create_gpml("centerLineOf"),
				GPlatesAppLogic::GeometryUtils::create_polyline_geometry_property_value(polyline));
		return feature;
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_orogeny(
			const QString &name,
			double begin_time,
			const boost::optional<double> &end_time,
			double collision_time,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("OrogenicBelt"));
		add_common_properties(feature->reference(), name, begin_time, end_time,
				collision_time, plate_id);
		add_property(feature->reference(), GPlatesModel::PropertyName::create_gpml("centerLineOf"),
				GPlatesPropertyValues::GmlPolygon::create(polygon));
		return feature;
	}

	struct FeatureGroup
	{
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		feature_seq_type features;
	};

	struct TimeSliceChange
	{
		TimeSliceChange(
				const GPlatesModel::FeatureHandle::weak_ref &source_,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const property_seq_type &source_before_,
				const property_seq_type &source_after_,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &successor_) :
			source(source_), collection(collection_),
			source_before(source_before_), source_after(source_after_),
			successor(successor_)
		{ }

		GPlatesModel::FeatureHandle::weak_ref source;
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		property_seq_type source_before;
		property_seq_type source_after;
		GPlatesModel::FeatureHandle::non_null_ptr_type successor;
	};

	GPlatesModel::FeatureHandle::non_null_ptr_type create_time_slice_successor(
			const GPlatesModel::FeatureHandle::weak_ref &source,
			const GPlatesModel::FeatureHandle::iterator &source_geometry_property,
			const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type &reconstructed_geometry,
			GPlatesModel::integer_plate_id_type target_plate_id,
			double collision_time,
			const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type successor =
				GPlatesModel::FeatureHandle::create(source->feature_type());
		GPlatesModel::FeatureHandle::iterator successor_geometry_property;
		for (GPlatesModel::FeatureHandle::iterator property_iter = source->begin();
			property_iter != source->end(); ++property_iter)
		{
			const GPlatesModel::FeatureHandle::iterator successor_property =
					successor->add((*property_iter)->clone());
			if (property_iter == source_geometry_property)
			{
				successor_geometry_property = successor_property;
			}
		}
		if (!successor_geometry_property.is_still_valid() ||
				!GPlatesModel::ModelUtils::set_property(
						successor->reference(),
						GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
						GPlatesPropertyValues::GpmlPlateId::create(target_plate_id)))
		{
			throw std::runtime_error("Could not prepare a collision successor Plate ID and geometry.");
		}

		const GPlatesAppLogic::ReconstructMethodRegistry reconstruct_method_registry;
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type stored_geometry =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						reconstructed_geometry,
						reconstruct_method_registry,
						successor->reference(),
						collision_time,
						tree_creator,
						GPlatesAppLogic::ReconstructParams(),
						true);
		GPlatesModel::TopLevelProperty::non_null_ptr_type geometry_property =
				(*successor_geometry_property)->clone();
		GPlatesFeatureVisitors::GeometrySetter geometry_setter(stored_geometry);
		geometry_setter.set_geometry(geometry_property.get());
		successor->set(successor_geometry_property, geometry_property);
		const QString source_id = source->feature_id().get().qstring();
		const QString successor_id = successor->feature_id().get().qstring();
		const GPlatesViewOperations::FeatureEventVersioner::EventRecord event =
				GPlatesViewOperations::FeatureEventVersioner::make_event(
						QString::fromLatin1("collision-time-slice"), collision_time,
						QString::fromLatin1("1"), QStringList() << source_id,
						QStringList() << successor_id, QString::fromLatin1("successor"));
		GPlatesViewOperations::FeatureEventVersioner::set_properties(
				successor->reference(),
				GPlatesViewOperations::FeatureEventVersioner::properties_for_event(
						successor->reference(),
						GPlatesViewOperations::FeatureEventVersioner::START_AT_EVENT, event));
		return successor;
	}

	bool should_retire_with_continent(const GPlatesModel::FeatureType &feature_type)
	{
		return feature_type == GPlatesModel::FeatureType::create_gpml("ContinentalCrust") ||
				feature_type == GPlatesModel::FeatureType::create_gpml("Craton") ||
				feature_type == GPlatesModel::FeatureType::create_gpml("ContinentalRift") ||
				feature_type == GPlatesModel::FeatureType::create_gpml("IslandArc") ||
				feature_type == GPlatesModel::FeatureType::create_gpml("LargeIgneousProvince") ||
				feature_type == GPlatesModel::FeatureType::create_gpml("OrogenicBelt") ||
				feature_type == GPlatesModel::FeatureType::create_gpml("Suture") ||
				feature_type == GPlatesModel::FeatureType::create_gpml("PassiveContinentalBoundary");
	}

	class CollisionCommitUndoCommand : public QUndoCommand
	{
	public:
		CollisionCommitUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const std::vector<FeatureGroup> &groups,
				const std::vector<TimeSliceChange> &time_slices,
				const boost::optional<GPlatesModel::FeatureHandle::weak_ref> &trench,
				double collision_time) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_groups(groups),
			d_time_slices(time_slices),
			d_trench(trench)
		{
			if (d_trench && d_trench->is_valid())
			{
				d_trench_before = GPlatesViewOperations::FeatureEventVersioner::clone_properties(**d_trench);
				const QString trench_id = (*d_trench)->feature_id().get().qstring();
				const GPlatesViewOperations::FeatureEventVersioner::EventRecord event =
						GPlatesViewOperations::FeatureEventVersioner::make_event(
								QString::fromLatin1("collision-trench-retirement"), collision_time,
								QString::fromLatin1("1"), QStringList() << trench_id,
								QStringList(), QString::fromLatin1("retired-source"));
				d_trench_after = GPlatesViewOperations::FeatureEventVersioner::properties_for_event(
						*d_trench, GPlatesViewOperations::FeatureEventVersioner::END_AT_EVENT, event);
			}
			d_iterators.resize(d_groups.size());
			d_successor_iterators.resize(d_time_slices.size());
			setText(QObject::tr("commit reviewed collision and orogeny"));
		}

		virtual void redo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_trench && d_trench->is_valid() && !d_trench_after.empty())
			{
				GPlatesViewOperations::FeatureEventVersioner::set_properties(*d_trench, d_trench_after);
			}
			for (unsigned int slice_index = 0; slice_index < d_time_slices.size(); ++slice_index)
			{
				TimeSliceChange &slice = d_time_slices[slice_index];
				if (!slice.source.is_valid() || !slice.collection.is_valid())
				{
					continue;
				}
				GPlatesViewOperations::FeatureEventVersioner::set_properties(slice.source, slice.source_after);
				d_successor_iterators[slice_index] = slice.collection->add(slice.successor);
				slice.successor = *d_successor_iterators[slice_index];
			}
			for (unsigned int group_index = 0; group_index < d_groups.size(); ++group_index)
			{
				d_iterators[group_index].clear();
				if (!d_groups[group_index].collection.is_valid())
				{
					continue;
				}
				for (feature_seq_type::iterator feature_iter =
						d_groups[group_index].features.begin();
					feature_iter != d_groups[group_index].features.end(); ++feature_iter)
				{
					const GPlatesModel::FeatureCollectionHandle::iterator inserted =
							d_groups[group_index].collection->add(*feature_iter);
					d_iterators[group_index].push_back(inserted);
					*feature_iter = *inserted;
				}
			}
			guard.release_guard();
		}

		virtual void undo()
		{
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (unsigned int group_index = 0; group_index < d_groups.size(); ++group_index)
			{
				for (std::size_t reverse = d_iterators[group_index].size(); reverse > 0; --reverse)
				{
					const std::size_t index = reverse - 1;
					if (d_iterators[group_index][index].is_still_valid())
					{
						d_groups[group_index].features[index] =
								d_groups[group_index].collection->remove(
										d_iterators[group_index][index]);
					}
				}
				d_iterators[group_index].clear();
			}
			for (std::size_t reverse = d_time_slices.size(); reverse > 0; --reverse)
			{
				const std::size_t slice_index = reverse - 1;
				TimeSliceChange &slice = d_time_slices[slice_index];
				if (slice.collection.is_valid() &&
						d_successor_iterators[slice_index].is_still_valid())
				{
					slice.successor = slice.collection->remove(
							d_successor_iterators[slice_index]);
				}
				if (slice.source.is_valid())
				{
					GPlatesViewOperations::FeatureEventVersioner::set_properties(slice.source, slice.source_before);
				}
			}
			if (d_trench && d_trench->is_valid() && !d_trench_before.empty())
			{
				GPlatesViewOperations::FeatureEventVersioner::set_properties(*d_trench, d_trench_before);
			}
			guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<FeatureGroup> d_groups;
		std::vector< std::vector<GPlatesModel::FeatureCollectionHandle::iterator> > d_iterators;
		std::vector<TimeSliceChange> d_time_slices;
		std::vector<GPlatesModel::FeatureCollectionHandle::iterator> d_successor_iterators;
		boost::optional<GPlatesModel::FeatureHandle::weak_ref> d_trench;
		property_seq_type d_trench_before;
		property_seq_type d_trench_after;
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

	QString collision_type_name(GPlatesViewOperations::CollisionGeometry::CollisionType type)
	{
		switch (type)
		{
		case GPlatesViewOperations::CollisionGeometry::ARC_OR_TERRANE_ACCRETION:
			return QObject::tr("arc/terrane-accretion");
		case GPlatesViewOperations::CollisionGeometry::HIMALAYAN_OROGENY:
			return QObject::tr("Himalayan");
		case GPlatesViewOperations::CollisionGeometry::URAL_OROGENY:
			return QObject::tr("Ural");
		}
		return QObject::tr("collision");
	}
}


GPlatesViewOperations::CollisionOrogenyOperation::Options::Options() :
	auto_classify(true),
	collision_type(CollisionGeometry::URAL_OROGENY),
	precursor_collision_count(0),
	contact_threshold_km(250.0),
	automatic_belt_width(true),
	belt_width_km(180.0),
	smoothing_iterations(2),
	belt_irregularity(0.12),
	active_duration_ma(50.0),
	old_orogen_age_ma(450.0),
	terminate_consumed_trench(true),
	allow_non_convergent(false),
	deform_contact_margins(true),
	deformation_reach_km(350.0),
	retire_incoming_plate(true)
{ }


GPlatesViewOperations::CollisionOrogenyOperation::CollisionOrogenyOperation(
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


GPlatesViewOperations::CollisionOrogenyOperation::Result
GPlatesViewOperations::CollisionOrogenyOperation::arm_incoming_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_INCOMING_CONTINENT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the incoming ContinentalCrust polygon."));
}


GPlatesViewOperations::CollisionOrogenyOperation::Result
GPlatesViewOperations::CollisionOrogenyOperation::arm_receiving_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_RECEIVING_CONTINENT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the receiving ContinentalCrust polygon."));
}


GPlatesViewOperations::CollisionOrogenyOperation::Result
GPlatesViewOperations::CollisionOrogenyOperation::arm_trench_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_CONSUMED_TRENCH;
	return Result(SELECTION_ARMED,
			QObject::tr("Select only the SubductionZone segment consumed by this collision."));
}


GPlatesViewOperations::CollisionOrogenyOperation::Result
GPlatesViewOperations::CollisionOrogenyOperation::capture_armed_selection()
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
	const boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
			(*rfg)->reconstruction_plate_id();
	if (!plate_id)
	{
		return Result(OPERATION_ERROR, QObject::tr("The selected feature has no reconstruction plate ID."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();

	if (d_selection_mode == SELECTING_CONSUMED_TRENCH)
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
		double start_time = current_time;
		const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type>
				valid_time = GPlatesFeatureVisitors::get_property_value<
						GPlatesPropertyValues::GmlTimePeriod>(
							d_feature_focus.focused_feature(),
							GPlatesModel::PropertyName::create_gml("validTime"));
		if (valid_time && (*valid_time)->begin()->get_time_position().is_real())
		{
			start_time = (*valid_time)->begin()->get_time_position().value();
		}
		d_trench = CapturedTrench(
				d_feature_focus.focused_feature(), polyline->get_non_null_pointer(),
				start_time, current_time);
		d_selection_mode = NOT_SELECTING;
		return Result(TRENCH_CAPTURED,
				QObject::tr("Consumed trench captured. Commit can end only this selected feature at collision time."));
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
	if ((d_selection_mode == SELECTING_INCOMING_CONTINENT && d_receiving &&
			d_receiving->feature == d_feature_focus.focused_feature()) ||
			(d_selection_mode == SELECTING_RECEIVING_CONTINENT && d_incoming &&
			d_incoming->feature == d_feature_focus.focused_feature()))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Incoming and receiving continents must be different source features."));
	}
	const CapturedContinent captured(
			d_feature_focus.focused_feature(), (*rfg)->property(), polygon->get_non_null_pointer(),
			*plate_id, current_time);
	if (d_selection_mode == SELECTING_INCOMING_CONTINENT)
	{
		d_incoming = captured;
		d_selection_mode = NOT_SELECTING;
		return Result(INCOMING_CAPTURED,
				QObject::tr("Incoming continent captured on Plate %1.").arg(*plate_id));
	}
	d_receiving = captured;
	d_selection_mode = NOT_SELECTING;
	return Result(RECEIVING_CAPTURED,
			QObject::tr("Receiving continent captured on Plate %1.").arg(*plate_id));
}


bool GPlatesViewOperations::CollisionOrogenyOperation::has_incoming() const
{
	return d_incoming && d_incoming->feature.is_valid();
}


bool GPlatesViewOperations::CollisionOrogenyOperation::has_receiving() const
{
	return d_receiving && d_receiving->feature.is_valid();
}


bool GPlatesViewOperations::CollisionOrogenyOperation::has_trench() const
{
	return d_trench && d_trench->feature.is_valid();
}


QString GPlatesViewOperations::CollisionOrogenyOperation::incoming_status() const
{
	return has_incoming()
			? QObject::tr("Incoming continent: ContinentalCrust on Plate %1").arg(d_incoming->plate_id)
			: QObject::tr("Incoming continent: not selected");
}


QString GPlatesViewOperations::CollisionOrogenyOperation::receiving_status() const
{
	return has_receiving()
			? QObject::tr("Receiving continent: ContinentalCrust on Plate %1").arg(d_receiving->plate_id)
			: QObject::tr("Receiving continent: not selected");
}


QString GPlatesViewOperations::CollisionOrogenyOperation::trench_status() const
{
	return has_trench()
			? QObject::tr("Consumed trench: selected (began %1 Ma)").arg(d_trench->start_time, 0, 'f', 1)
			: QObject::tr("Consumed trench: none selected (optional)");
}


boost::optional<double>
GPlatesViewOperations::CollisionOrogenyOperation::estimate_closing_speed(
		const CollisionGeometry::Result &geometry) const
{
	try
	{
		const double current_time =
				d_application_state.get_current_reconstruction().get_reconstruction_time();
		const double lookback_ma = 5.0;
		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction()
						.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type current_tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type older_tree =
				tree_creator.get_reconstruction_tree(current_time + lookback_ma);
		const boost::optional<GPlatesMaths::FiniteRotation> incoming_current =
				current_tree->get_composed_absolute_rotation_or_none(d_incoming->plate_id);
		const boost::optional<GPlatesMaths::FiniteRotation> receiving_current =
				current_tree->get_composed_absolute_rotation_or_none(d_receiving->plate_id);
		const boost::optional<GPlatesMaths::FiniteRotation> incoming_older =
				older_tree->get_composed_absolute_rotation_or_none(d_incoming->plate_id);
		const boost::optional<GPlatesMaths::FiniteRotation> receiving_older =
				older_tree->get_composed_absolute_rotation_or_none(d_receiving->plate_id);
		if (!incoming_current || !receiving_current || !incoming_older || !receiving_older)
		{
			return boost::none;
		}
		const GPlatesMaths::PointOnSphere incoming_previous =
				GPlatesMaths::compose(*incoming_older, GPlatesMaths::get_reverse(*incoming_current)) *
						geometry.incoming_contact;
		const GPlatesMaths::PointOnSphere receiving_previous =
				GPlatesMaths::compose(*receiving_older, GPlatesMaths::get_reverse(*receiving_current)) *
						geometry.receiving_contact;
		const double current_gap_km = GPlatesMaths::minimum_distance(
				geometry.incoming_contact, geometry.receiving_contact).calculate_angle().dval() * 6371.0088;
		const double previous_gap_km = GPlatesMaths::minimum_distance(
				incoming_previous, receiving_previous).calculate_angle().dval() * 6371.0088;
		return 0.1 * (previous_gap_km - current_gap_km) / lookback_ma;
	}
	catch (const std::exception &)
	{
		return boost::none;
	}
}


GPlatesViewOperations::CollisionOrogenyOperation::Result
GPlatesViewOperations::CollisionOrogenyOperation::preview(const Options &options)
{
	clear_preview();
	if (options.active_duration_ma <= 0 ||
			options.old_orogen_age_ma <= options.active_duration_ma)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Old-orogen age must be greater than the active-orogen duration."));
	}
	if (!has_incoming() || !has_receiving())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select both incoming and receiving continental crust first."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (options.retire_incoming_plate && d_incoming->plate_id == d_receiving->plate_id)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The incoming and receiving continents already use Plate %1; disable plate retirement or select different plates.")
						.arg(d_incoming->plate_id));
	}
	if (options.retire_incoming_plate && current_time <= 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("A plate cannot be retired toward the present from 0 Ma. Preview the collision at an older time."));
	}
	if (std::fabs(current_time - d_incoming->reconstruction_time) > 1e-9 ||
			std::fabs(current_time - d_receiving->reconstruction_time) > 1e-9 ||
			(d_trench && std::fabs(current_time - d_trench->reconstruction_time) > 1e-9))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after selection. Re-select the source features."));
	}
	if (options.terminate_consumed_trench && has_trench() &&
			d_trench->start_time + 1e-9 < current_time)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected trench begins younger than the collision time and cannot be ended here."));
	}

	try
	{
		CollisionGeometry::Parameters parameters;
		parameters.collision_type = options.collision_type;
		parameters.contact_threshold_km = options.contact_threshold_km;
		parameters.belt_width_km = options.belt_width_km;
		parameters.smoothing_iterations = options.smoothing_iterations;
		parameters.belt_irregularity = options.belt_irregularity;
		parameters.deformation_reach_km = options.deformation_reach_km;
		CollisionGeometry::Result geometry = CollisionGeometry::generate(
				d_incoming->polygon, d_receiving->polygon, parameters);
		const boost::optional<double> closing_speed = estimate_closing_speed(geometry);
		if (closing_speed && *closing_speed < -0.25 && !options.allow_non_convergent)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The rotation model estimates %1 cm/yr divergence across this contact. Enable the explicit non-convergent override only if this is intentional.")
							.arg(-*closing_speed, 0, 'f', 2));
		}
		const CollisionGeometry::CollisionType collision_type = options.auto_classify
				? CollisionGeometry::recommend_collision_type(
						d_incoming->polygon, d_receiving->polygon, closing_speed,
						options.precursor_collision_count)
				: options.collision_type;
		const double belt_width_km = options.automatic_belt_width
				? CollisionGeometry::default_belt_width_km(collision_type)
				: options.belt_width_km;
		if (collision_type != parameters.collision_type ||
				std::fabs(belt_width_km - parameters.belt_width_km) > 1e-9)
		{
			parameters.collision_type = collision_type;
			parameters.belt_width_km = belt_width_km;
			geometry = CollisionGeometry::generate(
					d_incoming->polygon, d_receiving->polygon, parameters);
		}
		const std::vector<double> &project_timestamps = d_application_state
				.get_project_timestamp_schedule().timestamps_older_to_younger();
		CollisionAccretionGuardrails::Request guard_request;
		guard_request.mode = CollisionAccretionGuardrails::COLLISION;
		guard_request.event_time = current_time;
		guard_request.project_schedule_available = !project_timestamps.empty();
		guard_request.event_is_project_timestamp = CollisionAccretionGuardrails::is_project_timestamp(
				current_time, project_timestamps);
		guard_request.incoming_plate = d_incoming->plate_id;
		guard_request.receiving_plate = d_receiving->plate_id;
		guard_request.survivor = options.retire_incoming_plate
				? CollisionAccretionGuardrails::RECEIVING_SURVIVES
				: CollisionAccretionGuardrails::KEEP_BOTH;
		guard_request.boolean_preview_reviewed = true;
		guard_request.boolean_output_count = options.deform_contact_margins ? 2 : 1;
		guard_request.craton_geometry_protected = true;
		guard_request.lineage_will_be_recorded = true;
		guard_request.no_rotation_jump = true;
		const CollisionAccretionGuardrails::Report guard_report =
				CollisionAccretionGuardrails::validate(guard_request);
		if (!guard_report.valid)
			return Result(OPERATION_ERROR, QObject::tr("Collision guardrail failed:\n%1")
					.arg(guard_report.errors.join("\n")));

		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						d_incoming->polygon, GPlatesGui::Colour::get_fuchsia(),
						2.0f, false, GPlatesGui::Colour::get_fuchsia()));
		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						d_receiving->polygon, GPlatesGui::Colour::get_blue(),
						2.0f, false, GPlatesGui::Colour::get_blue()));
		if (options.deform_contact_margins)
		{
			const GPlatesGui::Colour welded_colour(0.35f, 1.0f, 0.25f, 0.88f);
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							geometry.deformed_incoming, welded_colour, 3.0f,
							false, welded_colour));
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							geometry.deformed_receiving, welded_colour, 3.0f,
							false, welded_colour));
		}
		if (has_trench())
		{
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							d_trench->polyline, GPlatesGui::Colour::get_yellow(), 3.0f));
		}
		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polyline_on_sphere(
						geometry.suture, GPlatesGui::Colour::get_aqua(), 4.0f));
		const GPlatesGui::Colour orogen_colour(1.0f, 0.45f, 0.05f, 0.72f);
		d_preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						geometry.orogenic_belt, orogen_colour, 3.0f, true, orogen_colour));
		d_preview = Preview(options, collision_type, belt_width_km, geometry, closing_speed);

		const QString speed_text = closing_speed
				? QObject::tr("%1 cm/yr").arg(*closing_speed, 0, 'f', 2)
				: QObject::tr("unavailable");
		const QString deformation_text = options.deform_contact_margins
				? QObject::tr(" Green previews the time-sliced welded margins (%1 km post-deformation gap).")
						.arg(geometry.metrics.post_deformation_gap_km, 0, 'f', 1)
				: QString();
		const QString retirement_text = options.retire_incoming_plate
				? QObject::tr(" Incoming Plate %1 will retire into Plate %2 without a reconstruction jump; its older .rot history is preserved.")
						.arg(d_incoming->plate_id).arg(d_receiving->plate_id)
				: QString();
		return Result(PREVIEW_READY,
				QObject::tr("Previewed a %1 collision: %2 km suture, %3 km belt, %4 km present gap, relative closing speed %5. Incoming area is %6% of the receiver. %7 collision/accretion guardrails confirmed. Aqua is the suture; orange is the active orogeny.%8%9")
						.arg(collision_type_name(collision_type))
						.arg(geometry.metrics.contact_length_km, 0, 'f', 0)
						.arg(belt_width_km, 0, 'f', 0)
						.arg(geometry.metrics.minimum_gap_km, 0, 'f', 0)
						.arg(speed_text)
						.arg(100.0 * geometry.metrics.incoming_to_receiving_area_ratio, 0, 'f', 0)
						.arg(guard_report.confirmations.size())
						.arg(deformation_text).arg(retirement_text));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not generate the collision preview: %1").arg(exception.what()));
	}
}


GPlatesViewOperations::CollisionOrogenyOperation::Result
GPlatesViewOperations::CollisionOrogenyOperation::commit()
{
	if (!d_preview || !has_incoming() || !has_receiving())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Create and review a collision preview before committing."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_incoming->reconstruction_time) > 1e-9 ||
			std::fabs(current_time - d_receiving->reconstruction_time) > 1e-9)
	{
		clear_preview();
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after preview. Re-select and preview again."));
	}

	try
	{
		const QString type_name = collision_type_name(d_preview->collision_type);
		std::vector<FeatureGroup> groups;
		std::vector<QString> layer_names;
		GPlatesAppLogic::FeatureCollectionFileIO &file_io =
				d_application_state.get_feature_collection_file_io();

		FeatureGroup sutures;
		sutures.collection = resolve_or_create_worldbuilding_feature_collection(
				file_io, d_application_state.get_feature_collection_file_state(),
				QString::fromLatin1("collision-sutures"),
				QObject::tr("Collision Sutures")).get_file().get_feature_collection();
		sutures.features.push_back(create_suture(
				QObject::tr("Plate %1-%2 Collision Suture %3 Ma")
						.arg(d_incoming->plate_id).arg(d_receiving->plate_id)
						.arg(current_time, 0, 'f', 0),
				current_time, d_receiving->plate_id, d_preview->geometry.suture));
		groups.push_back(sutures);
		layer_names.push_back(QObject::tr("Collision Sutures"));

		const double active_end = std::max(0.0,
				current_time - d_preview->options.active_duration_ma);
		const double old_start = std::max(0.0,
				current_time - d_preview->options.old_orogen_age_ma);
		FeatureGroup active;
		active.collection = resolve_or_create_worldbuilding_feature_collection(
				file_io, d_application_state.get_feature_collection_file_state(),
				QString::fromLatin1("active-orogenies"),
				QObject::tr("Active Orogenies")).get_file().get_feature_collection();
		active.features.push_back(create_orogeny(
				QObject::tr("%1 Collision Orogeny %2 Ma").arg(type_name).arg(current_time, 0, 'f', 0),
				current_time, active_end, current_time, d_receiving->plate_id,
				d_preview->geometry.orogenic_belt));
		groups.push_back(active);
		layer_names.push_back(QObject::tr("Active Orogenies"));

		if (active_end > 1e-9)
		{
			FeatureGroup former;
			former.collection = resolve_or_create_worldbuilding_feature_collection(
					file_io, d_application_state.get_feature_collection_file_state(),
					QString::fromLatin1("former-orogenies"),
					QObject::tr("Former Orogenies")).get_file().get_feature_collection();
			former.features.push_back(create_orogeny(
					QObject::tr("Former %1 Collision Orogeny %2 Ma")
							.arg(type_name).arg(current_time, 0, 'f', 0),
					active_end, old_start > 1e-9
							? boost::optional<double>(old_start) : boost::none,
					current_time, d_receiving->plate_id, d_preview->geometry.orogenic_belt));
			groups.push_back(former);
			layer_names.push_back(QObject::tr("Former Orogenies"));
		}
		if (old_start > 1e-9 && old_start + 1e-9 < active_end)
		{
			FeatureGroup old;
			old.collection = resolve_or_create_worldbuilding_feature_collection(
					file_io, d_application_state.get_feature_collection_file_state(),
					QString::fromLatin1("old-orogenies"),
					QObject::tr("Old Orogenies")).get_file().get_feature_collection();
			old.features.push_back(create_orogeny(
					QObject::tr("Old %1 Collision Orogeny %2 Ma")
							.arg(type_name).arg(current_time, 0, 'f', 0),
					old_start, boost::none, current_time, d_receiving->plate_id,
					d_preview->geometry.orogenic_belt));
			groups.push_back(old);
			layer_names.push_back(QObject::tr("Old Orogenies"));
		}

		std::vector<TimeSliceChange> time_slices;
		std::set<const GPlatesModel::FeatureHandle *> sliced_features;
		const GPlatesAppLogic::Reconstruction &reconstruction =
				d_application_state.get_current_reconstruction();
		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				reconstruction.get_default_reconstruction_layer_output()
						->get_reconstruction_tree_creator();
		const auto append_time_slice = [&time_slices, &sliced_features, current_time, &tree_creator](
				const GPlatesModel::FeatureHandle::weak_ref &source,
				const GPlatesModel::FeatureHandle::iterator &geometry_property,
				const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type &geometry,
				GPlatesModel::integer_plate_id_type target_plate_id)
		{
			if (!source.is_valid() || !geometry_property.is_still_valid() ||
					!source->parent_ptr())
			{
				throw std::runtime_error("A selected collision feature is no longer editable.");
			}
			if (!sliced_features.insert(source.handle_ptr()).second)
			{
				return;
			}
			const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
					source->parent_ptr()->reference();
			const GPlatesModel::FeatureHandle::non_null_ptr_type successor =
					create_time_slice_successor(
							source, geometry_property, geometry, target_plate_id,
							current_time, tree_creator);
			const QString source_id = source->feature_id().get().qstring();
			const QString successor_id = successor->feature_id().get().qstring();
			const GPlatesViewOperations::FeatureEventVersioner::EventRecord source_event =
					GPlatesViewOperations::FeatureEventVersioner::make_event(
							QString::fromLatin1("collision-time-slice"), current_time,
							QString::fromLatin1("1"), QStringList() << source_id,
							QStringList() << successor_id, QString::fromLatin1("ended-source"));
			time_slices.push_back(TimeSliceChange(
					source,
					collection,
					GPlatesViewOperations::FeatureEventVersioner::clone_properties(*source),
					GPlatesViewOperations::FeatureEventVersioner::properties_for_event(
							source, GPlatesViewOperations::FeatureEventVersioner::END_AT_EVENT,
							source_event),
					successor));
		};

		if (d_preview->options.retire_incoming_plate ||
				d_preview->options.deform_contact_margins)
		{
			append_time_slice(
					d_incoming->feature,
					d_incoming->geometry_property,
					d_preview->options.deform_contact_margins
							? GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type(
									d_preview->geometry.deformed_incoming)
							: GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type(
									d_incoming->polygon),
					d_preview->options.retire_incoming_plate
							? d_receiving->plate_id : d_incoming->plate_id);
		}
		if (d_preview->options.deform_contact_margins)
		{
			append_time_slice(
					d_receiving->feature,
					d_receiving->geometry_property,
					GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type(
							d_preview->geometry.deformed_receiving),
					d_receiving->plate_id);
		}

		if (d_preview->options.retire_incoming_plate)
		{
			std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
			reconstruction.get_active_layer_outputs<GPlatesAppLogic::ReconstructLayerProxy>(layers);
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
							!rfg.get_feature_ref().is_valid() || !rfg.reconstruction_plate_id() ||
							*rfg.reconstruction_plate_id() != d_incoming->plate_id ||
							!should_retire_with_continent(rfg.get_feature_ref()->feature_type()) ||
							(d_trench && rfg.get_feature_ref() == d_trench->feature))
					{
						continue;
					}
					append_time_slice(
							rfg.get_feature_ref(), rfg.property(), rfg.reconstructed_geometry(),
							d_receiving->plate_id);
				}
			}
		}

		boost::optional<GPlatesModel::FeatureHandle::weak_ref> trench_to_end;
		if (d_preview->options.terminate_consumed_trench && has_trench())
		{
			trench_to_end = d_trench->feature;
		}
		std::unique_ptr<QUndoCommand> command(new CollisionCommitUndoCommand(
				d_feature_focus, d_model_interface, groups, time_slices,
				trench_to_end, current_time));
		GPlatesViewOperations::PlateEventTransaction::commit_command(
				std::move(command), QObject::tr("commit reviewed collision and orogeny"),
				GPlatesViewOperations::PlateEventTransaction::FEATURE_GEOMETRY);
		for (unsigned int group_index = 0; group_index < groups.size(); ++group_index)
		{
			name_layer(d_application_state, d_view_state,
					groups[group_index].collection, layer_names[group_index]);
		}
		const QString deformation_text = d_preview->options.deform_contact_margins
				? QObject::tr(", deformed both contact margins") : QString();
		const QString retirement_text = d_preview->options.retire_incoming_plate
				? QObject::tr(", and retired %1 incoming crustal feature slice(s) into Plate %2 without a jump while preserving the older .rot history")
						.arg(static_cast<qulonglong>(time_slices.size() -
								(d_preview->options.deform_contact_margins ? 1 : 0)))
						.arg(d_receiving->plate_id)
				: QString();
		const QString message =
				QObject::tr("Committed the %1 collision suture and active/former/old orogen lifecycle on receiving Plate %2%3%4%5. The loaded .rot collection was preserved; older incoming motion remains available for reconstruction.")
						.arg(type_name).arg(d_receiving->plate_id)
						.arg(trench_to_end ? QObject::tr(", ended the selected trench") : QString())
						.arg(deformation_text).arg(retirement_text);
		clear_preview();
		return Result(COLLISION_COMMITTED, message);
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not commit the collision: %1").arg(exception.what()));
	}
}


void GPlatesViewOperations::CollisionOrogenyOperation::clear_trench()
{
	clear_preview();
	d_trench = boost::none;
}


void GPlatesViewOperations::CollisionOrogenyOperation::clear_preview()
{
	d_preview_layer->clear_rendered_geometries();
	d_preview = boost::none;
}


void GPlatesViewOperations::CollisionOrogenyOperation::reset()
{
	clear_preview();
	d_selection_mode = NOT_SELECTING;
	d_incoming = boost::none;
	d_receiving = boost::none;
	d_trench = boost::none;
}
