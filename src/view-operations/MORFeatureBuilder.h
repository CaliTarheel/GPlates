/* $Id$ */

/**
 * \file
 * Shared construction of editable half-stage mid-ocean-ridge features.
 */

#ifndef GPLATES_VIEWOPERATIONS_MORFEATUREBUILDER_H
#define GPLATES_VIEWOPERATIONS_MORFEATUREBUILDER_H

#include <QString>

#include "maths/PolylineOnSphere.h"
#include "model/FeatureHandle.h"
#include "model/types.h"


namespace GPlatesViewOperations
{
	namespace MORFeatureBuilder
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type
		create_half_stage_mor(
				const QString &name,
				double start_time,
				GPlatesModel::integer_plate_id_type left_plate,
				GPlatesModel::integer_plate_id_type right_plate,
				const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline);
	}
}

#endif // GPLATES_VIEWOPERATIONS_MORFEATUREBUILDER_H
