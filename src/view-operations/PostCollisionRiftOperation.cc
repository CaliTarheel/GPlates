/* $Id$ */

/**
 * \file
 * Implements guided post-collision rerifting across a welded assemblage.
 */

#include "PostCollisionRiftOperation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QObject>
#include <QUndoCommand>

#include "MORFeatureBuilder.h"
#include "FeatureEventVersioner.h"
#include "PlateEventTransaction.h"
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
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/TRSUtils.h"

#include "feature-visitors/GeometrySetter.h"
#include "feature-visitors/PropertyValueFinder.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/FiniteRotation.h"
#include "maths/GeometryIntersect.h"
#include "maths/PointOnSphere.h"
#include "maths/Vector3D.h"

#include "model/FeatureCollectionHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/EnumerationType.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimeInstant.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlFiniteRotation.h"
#include "property-values/GpmlFiniteRotationSlerp.h"
#include "property-values/GpmlInterpolationFunction.h"
#include "property-values/GpmlIrregularSampling.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/GpmlTimeSample.h"
#include "property-values/StructuralType.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	typedef std::vector<GPlatesModel::TopLevelProperty::non_null_ptr_type> property_seq_type;
	typedef std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> feature_seq_type;
	typedef std::vector<GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type> sample_seq_type;

	struct Craton
	{
		Craton(const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_) :
			polygon(polygon_)
		{ }
		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
	};

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
				const property_seq_type &before_,
				const property_seq_type &after_,
				const feature_seq_type &successors_) :
			source(source_), collection(collection_), before(before_), after(after_),
			successors(successors_)
		{ }

		GPlatesModel::FeatureHandle::weak_ref source;
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		property_seq_type before;
		property_seq_type after;
		feature_seq_type successors;
	};

	void add_or_set_property(
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

	bool should_follow_rerift(const GPlatesModel::FeatureType &type)
	{
		return type == GPlatesModel::FeatureType::create_gpml("ContinentalCrust") ||
				type == GPlatesModel::FeatureType::create_gpml("ContinentalRift") ||
				type == GPlatesModel::FeatureType::create_gpml("IslandArc") ||
				type == GPlatesModel::FeatureType::create_gpml("LargeIgneousProvince") ||
				type == GPlatesModel::FeatureType::create_gpml("OrogenicBelt") ||
				type == GPlatesModel::FeatureType::create_gpml("Suture") ||
				type == GPlatesModel::FeatureType::create_gpml("PassiveContinentalBoundary");
	}

	std::vector<Craton> gather_cratons(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesMaths::PolygonOnSphere &host,
			GPlatesModel::integer_plate_id_type source_plate)
	{
		std::vector<Craton> cratons;
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
						rfg.get_feature_ref()->feature_type() !=
								GPlatesModel::FeatureType::create_gpml("Craton") ||
						!seen.insert((*rfg.property()).get()).second)
				{
					continue;
				}
				const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
								rfg.reconstructed_geometry().get());
				if (!polygon)
				{
					continue;
				}
				const GPlatesMaths::PointOnSphere centre(polygon->get_interior_centroid());
				if ((rfg.reconstruction_plate_id() &&
						*rfg.reconstruction_plate_id() == source_plate) ||
						host.is_point_in_polygon(centre))
				{
					cratons.push_back(Craton(polygon->get_non_null_pointer()));
				}
			}
		}
		return cratons;
	}

	bool intersects_cratons(
			const GPlatesMaths::PolylineOnSphere &rift,
			const std::vector<Craton> &cratons)
	{
		for (std::vector<Craton>::const_iterator craton_iter = cratons.begin();
				craton_iter != cratons.end(); ++craton_iter)
		{
			for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter = rift.vertex_begin();
					vertex_iter != rift.vertex_end(); ++vertex_iter)
			{
				if (craton_iter->polygon->is_point_in_polygon(*vertex_iter))
				{
					return true;
				}
			}
			GPlatesMaths::GeometryIntersect::Graph intersections;
			if (GPlatesMaths::GeometryIntersect::intersect(
					intersections, *craton_iter->polygon, rift, true) &&
						!intersections.unordered_intersections.empty())
			{
				return true;
			}
		}
		return false;
	}

	double point_side(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesMaths::PolylineOnSphere &rift)
	{
		double best_score = -1e100;
		double side = 0;
		for (unsigned int index = 0; index + 1 < rift.number_of_vertices(); ++index)
		{
			const GPlatesMaths::PointOnSphere &start = rift.get_vertex(index);
			const GPlatesMaths::PointOnSphere &end = rift.get_vertex(index + 1);
			const GPlatesMaths::Vector3D midpoint =
					GPlatesMaths::Vector3D(start.position_vector()) +
					GPlatesMaths::Vector3D(end.position_vector());
			const double proximity = GPlatesMaths::dot(
					midpoint, point.position_vector()).dval();
			if (proximity > best_score)
			{
				best_score = proximity;
				const GPlatesMaths::Vector3D normal = GPlatesMaths::cross(
						start.position_vector(), end.position_vector());
				side = GPlatesMaths::dot(normal, point.position_vector()).dval();
			}
		}
		return side;
	}

	GPlatesMaths::PointOnSphere representative_point(
			const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type &geometry)
	{
		if (const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(geometry.get()))
		{
			return GPlatesMaths::PointOnSphere(polygon->get_interior_centroid());
		}
		if (const GPlatesMaths::PolylineOnSphere *polyline =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(geometry.get()))
		{
			return polyline->get_vertex(polyline->number_of_vertices() / 2);
		}
		if (const GPlatesMaths::PointGeometryOnSphere *point =
				dynamic_cast<const GPlatesMaths::PointGeometryOnSphere *>(geometry.get()))
		{
			return point->position();
		}
		throw std::runtime_error("Unsupported crustal geometry in rerift side assignment.");
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_successor(
			const GPlatesModel::FeatureHandle::weak_ref &source,
			const GPlatesModel::FeatureHandle::iterator &source_geometry_property,
			const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type &reconstructed_geometry,
			const GPlatesMaths::FiniteRotation &target_absolute_rotation,
			GPlatesModel::integer_plate_id_type target_plate,
			double start_time,
			const boost::optional<QString> &name)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type successor =
				GPlatesModel::FeatureHandle::create(source->feature_type());
		GPlatesModel::FeatureHandle::iterator geometry_property;
		for (GPlatesModel::FeatureHandle::iterator property_iter = source->begin();
				property_iter != source->end(); ++property_iter)
		{
			const GPlatesModel::FeatureHandle::iterator new_property =
					successor->add((*property_iter)->clone());
			if (property_iter == source_geometry_property)
			{
				geometry_property = new_property;
			}
		}
		if (!geometry_property.is_still_valid())
		{
			throw std::runtime_error("Could not clone a rerifted feature's geometry property.");
		}
		add_or_set_property(successor->reference(),
				GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(target_plate));
		if (name)
		{
			add_or_set_property(successor->reference(),
					GPlatesModel::PropertyName::create_gml("name"),
					GPlatesPropertyValues::XsString::create(
							GPlatesUtils::make_icu_string_from_qstring(*name)));
		}
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type stored_geometry =
				GPlatesMaths::get_reverse(target_absolute_rotation) * reconstructed_geometry;
		GPlatesModel::TopLevelProperty::non_null_ptr_type cloned_geometry =
				(*geometry_property)->clone();
		GPlatesFeatureVisitors::GeometrySetter setter(stored_geometry);
		setter.set_geometry(cloned_geometry.get());
		successor->set(geometry_property, cloned_geometry);
		const QString source_id = source->feature_id().get().qstring();
		const QString successor_id = successor->feature_id().get().qstring();
		const GPlatesViewOperations::FeatureEventVersioner::EventRecord event =
				GPlatesViewOperations::FeatureEventVersioner::make_event(
						QString::fromLatin1("post-collision-rerift"), start_time,
						QString::fromLatin1("1"), QStringList() << source_id,
						QStringList() << successor_id, QString::fromLatin1("successor"));
		GPlatesViewOperations::FeatureEventVersioner::set_properties(
				successor->reference(),
				GPlatesViewOperations::FeatureEventVersioner::properties_for_event(
						successor->reference(),
						GPlatesViewOperations::FeatureEventVersioner::START_AT_EVENT, event));
		return successor;
	}

	GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type create_rotation_sample(
			double time,
			const GPlatesMaths::FiniteRotation &rotation,
			const QString &description)
	{
		using namespace GPlatesPropertyValues;
		return GpmlTimeSample::create(
				GpmlFiniteRotation::create(rotation),
				GmlTimeInstant::create(GeoTimeInstant(time)),
				XsString::create(GPlatesUtils::make_icu_string_from_qstring(description)),
				StructuralType::create_gpml("FiniteRotation"));
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type create_no_jump_rotation_branch(
			GPlatesModel::integer_plate_id_type moving_plate,
			double rift_time,
			const GPlatesMaths::FiniteRotation &rift_absolute_rotation)
	{
		sample_seq_type samples;
		samples.push_back(create_rotation_sample(
			0.0, GPlatesMaths::FiniteRotation::create_identity_rotation(),
				QObject::tr("Worldbuilding Pasta protected present-day identity")));
		samples.push_back(create_rotation_sample(rift_time, rift_absolute_rotation,
				QObject::tr("Worldbuilding Pasta no-jump post-collision rift")));
		const GPlatesPropertyValues::StructuralType value_type =
				GPlatesPropertyValues::StructuralType::create_gpml("FiniteRotation");
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("TotalReconstructionSequence"));
		feature->add(GPlatesModel::TopLevelPropertyInline::create(
				GPlatesModel::PropertyName::create_gpml("fixedReferenceFrame"),
				GPlatesPropertyValues::GpmlPlateId::create(0)));
		feature->add(GPlatesModel::TopLevelPropertyInline::create(
				GPlatesModel::PropertyName::create_gpml("movingReferenceFrame"),
				GPlatesPropertyValues::GpmlPlateId::create(moving_plate)));
		feature->add(GPlatesModel::TopLevelPropertyInline::create(
				GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"),
				GPlatesPropertyValues::GpmlIrregularSampling::create(
						samples,
						GPlatesPropertyValues::GpmlInterpolationFunction::non_null_ptr_type(
								GPlatesPropertyValues::GpmlFiniteRotationSlerp::create(value_type)),
						value_type)));
		return feature;
	}

	bool collection_has_moving_plate(
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
			GPlatesModel::integer_plate_id_type plate_id)
	{
		if (!collection.is_valid())
		{
			return false;
		}
		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
				feature_iter != collection->end(); ++feature_iter)
		{
			GPlatesAppLogic::TRSUtils::TRSFinder finder;
			finder.visit_feature((*feature_iter)->reference());
			if (finder.can_process_trs() && finder.moving_ref_frame_plate_id() &&
					*finder.moving_ref_frame_plate_id() == plate_id)
			{
				return true;
			}
		}
		return false;
	}

	boost::optional<GPlatesModel::FeatureCollectionHandle::weak_ref> find_rotation_collection(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesModel::integer_plate_id_type source_plate)
	{
		const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded =
				application_state.get_feature_collection_file_state().get_loaded_files();
		for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_reverse_iterator
				file_iter = loaded.rbegin(); file_iter != loaded.rend(); ++file_iter)
		{
			const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
					file_iter->get_file().get_feature_collection();
			if (collection_has_moving_plate(collection, source_plate))
			{
				return collection;
			}
		}
		return boost::none;
	}

	bool any_rotation_sequence(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesModel::integer_plate_id_type plate_id)
	{
		const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded =
				application_state.get_feature_collection_file_state().get_loaded_files();
		for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
				file_iter = loaded.begin(); file_iter != loaded.end(); ++file_iter)
		{
			if (collection_has_moving_plate(
					file_iter->get_file().get_feature_collection(), plate_id))
			{
				return true;
			}
		}
		return false;
	}

	class ReriftUndoCommand : public QUndoCommand
	{
	public:
		ReriftUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const std::vector<TimeSliceChange> &time_slices,
				const std::vector<FeatureGroup> &groups) :
			d_feature_focus(feature_focus), d_model_interface(model_interface),
			d_time_slices(time_slices), d_groups(groups)
		{
			setText(QObject::tr("commit reviewed post-collision rerift"));
		}

		virtual void redo()
		{
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::vector<TimeSliceChange>::iterator slice_iter = d_time_slices.begin();
					slice_iter != d_time_slices.end(); ++slice_iter)
			{
				if (!slice_iter->source.is_valid() || !slice_iter->collection.is_valid())
				{
					continue;
				}
				GPlatesViewOperations::FeatureEventVersioner::set_properties(
						slice_iter->source, slice_iter->after);
				for (feature_seq_type::iterator successor_iter = slice_iter->successors.begin();
						successor_iter != slice_iter->successors.end(); ++successor_iter)
				{
					if (!(*successor_iter)->parent_ptr())
					{
						const GPlatesModel::FeatureCollectionHandle::iterator inserted =
								slice_iter->collection->add(*successor_iter);
						*successor_iter = *inserted;
					}
				}
			}
			for (std::vector<FeatureGroup>::iterator group_iter = d_groups.begin();
					group_iter != d_groups.end(); ++group_iter)
			{
				if (!group_iter->collection.is_valid())
				{
					continue;
				}
				for (feature_seq_type::iterator feature_iter = group_iter->features.begin();
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
				for (feature_seq_type::reverse_iterator feature_iter = group_iter->features.rbegin();
						feature_iter != group_iter->features.rend(); ++feature_iter)
				{
					if ((*feature_iter)->parent_ptr())
					{
						(*feature_iter)->remove_from_parent();
					}
				}
			}
			for (std::vector<TimeSliceChange>::reverse_iterator slice_iter = d_time_slices.rbegin();
					slice_iter != d_time_slices.rend(); ++slice_iter)
			{
				for (feature_seq_type::reverse_iterator successor_iter = slice_iter->successors.rbegin();
						successor_iter != slice_iter->successors.rend(); ++successor_iter)
				{
					if ((*successor_iter)->parent_ptr())
					{
						(*successor_iter)->remove_from_parent();
					}
				}
				if (slice_iter->source.is_valid())
				{
					GPlatesViewOperations::FeatureEventVersioner::set_properties(
							slice_iter->source, slice_iter->before);
				}
			}
			guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<TimeSliceChange> d_time_slices;
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


GPlatesViewOperations::PostCollisionRiftOperation::Options::Options() :
	suture_offset_km(120.0),
	wiggle(0.25),
	maximum_segment_length_km(90.0),
	end_extension_km(500.0),
	side(1),
	random_seed(1)
{ }


GPlatesViewOperations::PostCollisionRiftOperation::PostCollisionRiftOperation(
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


GPlatesViewOperations::PostCollisionRiftOperation::Result
GPlatesViewOperations::PostCollisionRiftOperation::arm_host_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_HOST_CONTINENT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the ContinentalCrust polygon the proposed rerift must cut."));
}


GPlatesViewOperations::PostCollisionRiftOperation::Result
GPlatesViewOperations::PostCollisionRiftOperation::arm_suture_selection()
{
	clear_preview();
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_SUTURE;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the inherited Suture polyline that will bias, but not dictate, the new rift."));
}


GPlatesViewOperations::PostCollisionRiftOperation::Result
GPlatesViewOperations::PostCollisionRiftOperation::capture_armed_selection()
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

	if (d_selection_mode == SELECTING_HOST_CONTINENT)
	{
		const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
						(*rfg)->reconstructed_geometry().get());
		if (!polygon || d_feature_focus.focused_feature()->feature_type() !=
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust"))
		{
			return Result(OPERATION_ERROR,
					QObject::tr("That is not a ContinentalCrust polygon; selection remains armed."));
		}
		const boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
				(*rfg)->reconstruction_plate_id();
		if (!plate_id)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selected host crust has no reconstruction plate ID."));
		}
		d_host = CapturedHost(
				d_feature_focus.focused_feature(), (*rfg)->property(),
				(*rfg)->get_non_null_pointer_to_const(), polygon->get_non_null_pointer(),
				*plate_id, current_time);
		d_selection_mode = NOT_SELECTING;
		return Result(HOST_CAPTURED,
				QObject::tr("Rerift host captured on Plate %1.").arg(*plate_id));
	}

	const GPlatesMaths::PolylineOnSphere *polyline =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
					(*rfg)->reconstructed_geometry().get());
	if (!polyline || d_feature_focus.focused_feature()->feature_type() !=
			GPlatesModel::FeatureType::create_gpml("Suture"))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That is not a Suture polyline; selection remains armed."));
	}
	d_suture = CapturedSuture(
			d_feature_focus.focused_feature(), polyline->get_non_null_pointer(), current_time);
	d_selection_mode = NOT_SELECTING;
	return Result(SUTURE_CAPTURED,
			QObject::tr("Inherited suture captured with %1 vertices.")
					.arg(polyline->number_of_vertices()));
}


bool GPlatesViewOperations::PostCollisionRiftOperation::has_host() const
{
	return d_host && d_host->feature.is_valid() && d_host->geometry_property.is_still_valid();
}


bool GPlatesViewOperations::PostCollisionRiftOperation::has_suture() const
{
	return d_suture && d_suture->feature.is_valid();
}


QString GPlatesViewOperations::PostCollisionRiftOperation::host_status() const
{
	return has_host()
			? QObject::tr("Host: ContinentalCrust on Plate %1").arg(d_host->plate_id)
			: QObject::tr("Host: not selected");
}


QString GPlatesViewOperations::PostCollisionRiftOperation::suture_status() const
{
	return has_suture()
			? QObject::tr("Suture: %1 vertices").arg(d_suture->polyline->number_of_vertices())
			: QObject::tr("Suture: not selected");
}


GPlatesModel::integer_plate_id_type
GPlatesViewOperations::PostCollisionRiftOperation::source_plate_id() const
{
	return has_host() ? d_host->plate_id : 0;
}


GPlatesModel::integer_plate_id_type
GPlatesViewOperations::PostCollisionRiftOperation::suggest_new_plate_id() const
{
	std::set<GPlatesModel::integer_plate_id_type> used;
	std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
	d_application_state.get_current_reconstruction().get_active_layer_outputs<
			GPlatesAppLogic::ReconstructLayerProxy>(layers);
	for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
			layer_iter = layers.begin(); layer_iter != layers.end(); ++layer_iter)
	{
		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
		(*layer_iter)->get_reconstructed_feature_geometries(geometries);
		for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
				geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
		{
			if ((*geometry_iter)->reconstruction_plate_id())
			{
				used.insert(*(*geometry_iter)->reconstruction_plate_id());
			}
		}
	}
	GPlatesModel::integer_plate_id_type candidate = source_plate_id() + 1;
	while (used.find(candidate) != used.end() || any_rotation_sequence(d_application_state, candidate))
	{
		++candidate;
	}
	return candidate;
}


GPlatesViewOperations::PostCollisionRiftOperation::Result
GPlatesViewOperations::PostCollisionRiftOperation::preview(const Options &options)
{
	clear_preview();
	if (!has_host() || !has_suture())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select both the host ContinentalCrust and inherited Suture first."));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_host->reconstruction_time) > 1e-9 ||
			std::fabs(current_time - d_suture->reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after selection. Re-select both source features."));
	}

	try
	{
		const std::vector<Craton> cratons = gather_cratons(
				d_application_state, *d_host->polygon, d_host->plate_id);
		QString last_split_error;
		unsigned int rejected = 0;
		for (unsigned int attempt = 0; attempt < 32; ++attempt)
		{
			PostCollisionRiftGeometry::Parameters parameters;
			parameters.offset_km = options.suture_offset_km + 20.0 * (attempt / 12);
			parameters.wiggle = options.wiggle;
			parameters.maximum_segment_length_km = options.maximum_segment_length_km;
			parameters.end_extension_km = options.end_extension_km;
			parameters.side = options.side;
			parameters.random_seed = options.random_seed + attempt;
			const PostCollisionRiftGeometry::Result geometry =
					PostCollisionRiftGeometry::generate(d_suture->polyline, parameters);
			if (intersects_cratons(*geometry.rift, cratons))
			{
				++rejected;
				continue;
			}
			QString split_error;
			const boost::optional<SplitPlateGeometry::Result> split =
					SplitPlateGeometry::split_polygon(
							split_error, *d_host->polygon, *geometry.rift, *d_host->rfg);
			if (!split)
			{
				last_split_error = split_error;
				++rejected;
				continue;
			}

			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							d_host->polygon, GPlatesGui::Colour::get_white(), 1.5f));
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							d_suture->polyline, GPlatesGui::Colour::get_blue(), 2.5f));
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							geometry.rift, GPlatesGui::Colour::get_yellow(), 5.0f));
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							split->reconstructed_polygon1,
							GPlatesGui::Colour::get_aqua(), 2.0f, true,
							GPlatesGui::Colour(0.0f, 0.35f, 0.35f)));
			d_preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							split->reconstructed_polygon2,
							GPlatesGui::Colour(1.0f, 0.5f, 0.0f), 2.0f, true,
							GPlatesGui::Colour(0.35f, 0.18f, 0.0f)));
			d_preview = Preview(options, geometry, *split, rejected);
			return Result(PREVIEW_READY,
					QObject::tr("Previewed a %1-vertex rerift averaging %2 km from the inherited suture. It clears %3 relevant craton(s), cuts the selected crust cleanly, and rejected %4 alternate route(s). Aqua/orange show the two child sides; yellow is the proposed MOR.")
							.arg(geometry.metrics.vertex_count)
							.arg(geometry.metrics.mean_suture_offset_km, 0, 'f', 0)
							.arg(cratons.size()).arg(rejected));
		}
		return Result(OPERATION_ERROR,
				QObject::tr("No craton-safe route both cleared the inherited cores and cut the selected crust after 32 deterministic attempts.%1 Try the opposite side, a larger offset, more wiggle, or a longer endpoint extension.")
						.arg(last_split_error.isEmpty()
								? QString()
								: QObject::tr(" Last cut result: %1").arg(last_split_error)));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not generate the post-collision rerift preview: %1")
						.arg(exception.what()));
	}
}


GPlatesViewOperations::PostCollisionRiftOperation::Result
GPlatesViewOperations::PostCollisionRiftOperation::commit(
		const QString &left_name,
		GPlatesModel::integer_plate_id_type left_plate_id,
		const QString &right_name,
		GPlatesModel::integer_plate_id_type right_plate_id)
{
	if (!d_preview || !has_host())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Create and review a rerift preview before committing."));
	}
	if (left_name.trimmed().isEmpty() || right_name.trimmed().isEmpty())
	{
		return Result(OPERATION_ERROR, QObject::tr("Give both child continents a name."));
	}
	if (left_plate_id == right_plate_id)
	{
		return Result(OPERATION_ERROR, QObject::tr("The two child plates need distinct Plate IDs."));
	}
	if (left_plate_id != d_host->plate_id && right_plate_id != d_host->plate_id)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Keep source Plate %1 for one child. This preserves the existing plate circuit and creates only one no-jump branch for the other child.")
						.arg(d_host->plate_id));
	}
	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(current_time - d_host->reconstruction_time) > 1e-9)
	{
		clear_preview();
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after preview. Re-select and preview again."));
	}

	try
	{
		const boost::optional<GPlatesModel::FeatureCollectionHandle::weak_ref> rotation_collection =
				find_rotation_collection(d_application_state, d_host->plate_id);
		if (!rotation_collection)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Load the editable .rot collection that owns source Plate %1 before committing the rerift.")
							.arg(d_host->plate_id));
		}

		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction().
						get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesMaths::FiniteRotation source_absolute =
				tree->get_composed_absolute_rotation(d_host->plate_id);
		const GPlatesMaths::FiniteRotation left_absolute =
				(left_plate_id == d_host->plate_id ||
						!any_rotation_sequence(d_application_state, left_plate_id))
						? source_absolute
						: tree->get_composed_absolute_rotation(left_plate_id);
		const GPlatesMaths::FiniteRotation right_absolute =
				(right_plate_id == d_host->plate_id ||
						!any_rotation_sequence(d_application_state, right_plate_id))
						? source_absolute
						: tree->get_composed_absolute_rotation(right_plate_id);

		const GPlatesMaths::PolylineOnSphere &rift = *d_preview->geometry.rift;
		const bool polygon1_is_left = point_side(
				GPlatesMaths::PointOnSphere(
						d_preview->host_split.reconstructed_polygon1->get_interior_centroid()),
				rift) >= 0;
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type left_host =
				polygon1_is_left
						? d_preview->host_split.reconstructed_polygon1
						: d_preview->host_split.reconstructed_polygon2;
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type right_host =
				polygon1_is_left
						? d_preview->host_split.reconstructed_polygon2
						: d_preview->host_split.reconstructed_polygon1;

		std::vector<TimeSliceChange> time_slices;
		std::set<const GPlatesModel::FeatureHandle *> seen_features;
		unsigned int split_feature_count = 0;
		unsigned int assigned_feature_count = 0;
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
		d_application_state.get_current_reconstruction().get_active_layer_outputs<
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
						!rfg.get_feature_ref().is_valid() || !rfg.reconstruction_plate_id() ||
						*rfg.reconstruction_plate_id() != d_host->plate_id ||
						!should_follow_rerift(rfg.get_feature_ref()->feature_type()) ||
						!rfg.get_feature_ref()->parent_ptr() ||
						!seen_features.insert(rfg.get_feature_ref().handle_ptr()).second)
				{
					continue;
				}

				feature_seq_type successors;
				if (rfg.get_feature_ref() == d_host->feature)
				{
					successors.push_back(create_successor(
							rfg.get_feature_ref(), rfg.property(), left_host,
							left_absolute, left_plate_id, current_time, left_name.trimmed()));
					successors.push_back(create_successor(
							rfg.get_feature_ref(), rfg.property(), right_host,
							right_absolute, right_plate_id, current_time, right_name.trimmed()));
					++split_feature_count;
				}
				else if (const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
								rfg.reconstructed_geometry().get()))
				{
					QString split_error;
					const boost::optional<SplitPlateGeometry::Result> split =
							SplitPlateGeometry::split_polygon(
									split_error, *polygon, rift, rfg);
					if (split)
					{
						const bool first_left = point_side(
								GPlatesMaths::PointOnSphere(
										split->reconstructed_polygon1->get_interior_centroid()),
								rift) >= 0;
						successors.push_back(create_successor(
								rfg.get_feature_ref(), rfg.property(),
								split->reconstructed_polygon1,
								first_left ? left_absolute : right_absolute,
								first_left ? left_plate_id : right_plate_id,
								current_time, boost::none));
						successors.push_back(create_successor(
								rfg.get_feature_ref(), rfg.property(),
								split->reconstructed_polygon2,
								first_left ? right_absolute : left_absolute,
								first_left ? right_plate_id : left_plate_id,
								current_time, boost::none));
						++split_feature_count;
					}
				}
				if (successors.empty())
				{
					const bool left = point_side(
							representative_point(rfg.reconstructed_geometry()), rift) >= 0;
					successors.push_back(create_successor(
							rfg.get_feature_ref(), rfg.property(), rfg.reconstructed_geometry(),
							left ? left_absolute : right_absolute,
							left ? left_plate_id : right_plate_id,
							current_time, boost::none));
					++assigned_feature_count;
				}

				const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
						rfg.get_feature_ref()->parent_ptr()->reference();
				QStringList successor_ids;
				for (feature_seq_type::const_iterator successor = successors.begin();
					 successor != successors.end(); ++successor)
				{
					successor_ids.append((*successor)->feature_id().get().qstring());
				}
				const QString source_id = rfg.get_feature_ref()->feature_id().get().qstring();
				const GPlatesViewOperations::FeatureEventVersioner::EventRecord source_event =
						GPlatesViewOperations::FeatureEventVersioner::make_event(
								QString::fromLatin1("post-collision-rerift"), current_time,
								QString::fromLatin1("1"), QStringList() << source_id,
								successor_ids, QString::fromLatin1("ended-source"));
				time_slices.push_back(TimeSliceChange(
						rfg.get_feature_ref(), collection,
						GPlatesViewOperations::FeatureEventVersioner::clone_properties(
								*rfg.get_feature_ref()),
						GPlatesViewOperations::FeatureEventVersioner::properties_for_event(
								rfg.get_feature_ref(),
								GPlatesViewOperations::FeatureEventVersioner::END_AT_EVENT,
								source_event),
						successors));
			}
		}
		if (time_slices.empty())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("No editable crustal features on source Plate %1 were found.")
							.arg(d_host->plate_id));
		}

		std::vector<FeatureGroup> groups;
		GPlatesAppLogic::FeatureCollectionFileIO &file_io =
				d_application_state.get_feature_collection_file_io();
		FeatureGroup mor_group;
		mor_group.collection = resolve_or_create_worldbuilding_feature_collection(
				file_io, d_application_state.get_feature_collection_file_state(),
				QString::fromLatin1("mors"),
				QObject::tr("Active Mid-Ocean Ridges")).get_file().get_feature_collection();
		mor_group.features.push_back(MORFeatureBuilder::create_half_stage_mor(
				QObject::tr("%1 - %2 Post-Collision Rift").arg(left_name.trimmed(), right_name.trimmed()),
				current_time, left_plate_id, right_plate_id,
				d_preview->host_split.rift_polyline));
		groups.push_back(mor_group);

		FeatureGroup rotations;
		rotations.collection = *rotation_collection;
		if (left_plate_id != d_host->plate_id &&
				!any_rotation_sequence(d_application_state, left_plate_id))
		{
			rotations.features.push_back(create_no_jump_rotation_branch(
					left_plate_id, current_time, source_absolute));
		}
		if (right_plate_id != d_host->plate_id &&
				!any_rotation_sequence(d_application_state, right_plate_id))
		{
			rotations.features.push_back(create_no_jump_rotation_branch(
					right_plate_id, current_time, source_absolute));
		}
		if (!rotations.features.empty())
		{
			groups.push_back(rotations);
		}

		std::unique_ptr<QUndoCommand> command(new ReriftUndoCommand(
				d_feature_focus, d_model_interface, time_slices, groups));
		GPlatesViewOperations::PlateEventTransaction transaction(
				QObject::tr("commit reviewed post-collision rerift"));
		transaction.add_command(
				std::move(command), GPlatesViewOperations::PlateEventTransaction::FEATURE_GEOMETRY);
		transaction.commit();
		name_layer(d_application_state, d_view_state,
				mor_group.collection, QObject::tr("Active Mid-Ocean Ridges"));
		const unsigned int rotation_count = rotations.features.size();
		clear_preview();
		return Result(RERIFT_COMMITTED,
				QObject::tr("Committed the reviewed post-collision rerift at %1 Ma: %2 crossed crustal feature(s) were split, %3 whole feature(s) were assigned by side, one HalfStageRotationVersion3 MOR was created, and %4 independent Plate-0 no-jump rotation branch(es) were added to the loaded .rot collection. Older source slices remain intact; everything is one undo step.")
						.arg(current_time, 0, 'f', 1)
						.arg(split_feature_count).arg(assigned_feature_count).arg(rotation_count));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not commit the post-collision rerift: %1").arg(exception.what()));
	}
}


void GPlatesViewOperations::PostCollisionRiftOperation::clear_preview()
{
	d_preview_layer->clear_rendered_geometries();
	d_preview = boost::none;
}


void GPlatesViewOperations::PostCollisionRiftOperation::reset()
{
	clear_preview();
	d_selection_mode = NOT_SELECTING;
	d_host = boost::none;
	d_suture = boost::none;
}
