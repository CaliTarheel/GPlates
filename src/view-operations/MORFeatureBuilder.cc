/* $Id$ */

/**
 * \file
 * Shared construction of editable half-stage mid-ocean-ridge features.
 */

#include "MORFeatureBuilder.h"

#include <stdexcept>

#include "app-logic/GeometryUtils.h"

#include "model/ModelUtils.h"
#include "model/PropertyName.h"

#include "property-values/Enumeration.h"
#include "property-values/EnumerationType.h"
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
}


GPlatesModel::FeatureHandle::non_null_ptr_type
GPlatesViewOperations::MORFeatureBuilder::create_half_stage_mor(
		const QString &name,
		double start_time,
		GPlatesModel::integer_plate_id_type left_plate,
		GPlatesModel::integer_plate_id_type right_plate,
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline)
{
	GPlatesModel::FeatureHandle::non_null_ptr_type feature =
			GPlatesModel::FeatureHandle::create(
					GPlatesModel::FeatureType::create_gpml("MidOceanRidge"));
	const GPlatesModel::FeatureHandle::weak_ref ref = feature->reference();
	set_required_property(ref, GPlatesModel::PropertyName::create_gml("name"),
			GPlatesPropertyValues::XsString::create(
					GPlatesUtils::make_icu_string_from_qstring(name)));
	set_required_property(ref, GPlatesModel::PropertyName::create_gml("validTime"),
			GPlatesModel::ModelUtils::create_gml_time_period(
					GPlatesPropertyValues::GeoTimeInstant(start_time),
					GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
	set_required_property(ref, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
			GPlatesModel::ModelUtils::create_gml_time_instant(
					GPlatesPropertyValues::GeoTimeInstant(start_time)));
	set_required_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionMethod"),
			GPlatesPropertyValues::Enumeration::create(
					GPlatesPropertyValues::EnumerationType::create_gpml(
							"ReconstructionMethodEnumeration"),
					"HalfStageRotationVersion3"));
	set_required_property(ref, GPlatesModel::PropertyName::create_gpml("leftPlate"),
			GPlatesPropertyValues::GpmlPlateId::create(left_plate));
	set_required_property(ref, GPlatesModel::PropertyName::create_gpml("rightPlate"),
			GPlatesPropertyValues::GpmlPlateId::create(right_plate));
	set_required_property(ref, GPlatesModel::PropertyName::create_gpml("centerLineOf"),
			GPlatesAppLogic::GeometryUtils::create_polyline_geometry_property_value(polyline));
	return feature;
}
