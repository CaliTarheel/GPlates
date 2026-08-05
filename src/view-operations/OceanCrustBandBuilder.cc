/* $Id$ */

/**
 * \file
 * Reusable geometry service for Worldbuilding Pasta ocean-crust generation.
 */

#include "OceanCrustBandBuilder.h"

#include <exception>
#include <set>
#include <stdexcept>
#include <vector>

#include "SubductionCutterGeometry.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructionTree.h"

#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/ModelUtils.h"
#include "model/PropertyName.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
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

	GPlatesViewOperations::OceanCrustBandBuilder::polygon_ptr_type
	create_candidate_band(
			const GPlatesMaths::PolylineOnSphere &current_ridge,
			const GPlatesMaths::PolylineOnSphere &older_ridge,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesAppLogic::ReconstructionTree &older_tree,
			const GPlatesAppLogic::ReconstructionTree &current_tree)
	{
		const std::size_t current_vertex_count = static_cast<std::size_t>(
				std::distance(current_ridge.vertex_begin(), current_ridge.vertex_end()));
		const std::size_t older_vertex_count = static_cast<std::size_t>(
				std::distance(older_ridge.vertex_begin(), older_ridge.vertex_end()));
		if (current_vertex_count < 2 || current_vertex_count != older_vertex_count)
		{
			throw std::runtime_error(
					"The ridge geometry changed vertex topology between the two ages.");
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
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator current_iter =
				current_ridge.vertex_begin();
		for (std::size_t vertex_index = 0; vertex_index < older_vertex_count;
				++vertex_index, ++current_iter)
		{
			if (!GPlatesMaths::points_are_coincident(
					*current_iter, advected_older_ridge[vertex_index]))
			{
				has_spreading_width = true;
				break;
			}
		}
		if (!has_spreading_width)
		{
			throw std::runtime_error(
					"This plate has no recorded motion across the requested age band.");
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
}


GPlatesViewOperations::OceanCrustBandBuilder::Result
GPlatesViewOperations::OceanCrustBandBuilder::subtract_existing_crust(
		const GPlatesMaths::PolygonOnSphere &candidate,
		const polygon_seq_type &existing_ocean_crust)
{
	Result result;
	const SubductionCutterGeometry::CutResult cut =
			SubductionCutterGeometry::cut_polygon(candidate, existing_ocean_crust);
	if (!cut.success)
	{
		result.success = false;
		result.error = cut.error;
		return result;
	}
	result.existing_overlap_removed = cut.overlap;
	result.polygons = cut.outside;
	return result;
}


GPlatesViewOperations::OceanCrustBandBuilder::Result
GPlatesViewOperations::OceanCrustBandBuilder::build_side_band(
		const GPlatesMaths::PolylineOnSphere &current_ridge,
		const GPlatesMaths::PolylineOnSphere &older_ridge,
		GPlatesModel::integer_plate_id_type plate_id,
		const GPlatesAppLogic::ReconstructionTree &older_tree,
		const GPlatesAppLogic::ReconstructionTree &current_tree,
		const polygon_seq_type &existing_ocean_crust)
{
	try
	{
		const polygon_ptr_type candidate = create_candidate_band(
				current_ridge, older_ridge, plate_id, older_tree, current_tree);
		return subtract_existing_crust(*candidate, existing_ocean_crust);
	}
	catch (const std::exception &exception)
	{
		Result result;
		result.success = false;
		result.error = QString::fromLocal8Bit(exception.what());
		return result;
	}
}


GPlatesViewOperations::OceanCrustBandBuilder::polygon_ptr_type
GPlatesViewOperations::OceanCrustBandBuilder::reverse_reconstruct_polygon(
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
	std::vector< std::vector<GPlatesMaths::PointOnSphere> > stored_interior_rings;
	for (unsigned int ring_index = 0;
			ring_index < polygon.number_of_interior_rings(); ++ring_index)
	{
		stored_interior_rings.push_back(std::vector<GPlatesMaths::PointOnSphere>());
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				polygon.interior_ring_vertex_begin(ring_index);
				vertex_iter != polygon.interior_ring_vertex_end(ring_index); ++vertex_iter)
		{
			stored_interior_rings.back().push_back(
					GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
							*vertex_iter, plate_id, current_tree, true));
		}
	}
	return GPlatesMaths::PolygonOnSphere::create(
			stored_ring.begin(), stored_ring.end(),
			stored_interior_rings.begin(), stored_interior_rings.end(), true);
}


GPlatesViewOperations::OceanCrustBandBuilder::polygon_seq_type
GPlatesViewOperations::OceanCrustBandBuilder::get_existing_ocean_crust(
		GPlatesAppLogic::ApplicationState &application_state)
{
	static const GPlatesModel::FeatureType OCEANIC_CRUST =
			GPlatesModel::FeatureType::create_gpml("OceanicCrust");
	polygon_seq_type polygons;
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
				polygons.push_back(polygon_ptr_type(polygon));
			}
		}
	}
	return polygons;
}


GPlatesModel::FeatureHandle::non_null_ptr_type
GPlatesViewOperations::OceanCrustBandBuilder::create_oceanic_crust_feature(
		const QString &name,
		double appearance_time,
		double geometry_import_time,
		GPlatesModel::integer_plate_id_type plate_id,
		const polygon_ptr_type &stored_polygon)
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
