/* $Id$ */

/**
 * \file
 * Reusable validation and feature construction for a newly born plate.
 */

#ifndef GPLATES_VIEWOPERATIONS_ROTATIONPLATECREATION_H
#define GPLATES_VIEWOPERATIONS_ROTATIONPLATECREATION_H

#include <set>

#include <boost/optional.hpp>
#include <QString>

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/types.h"


namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesViewOperations
{
	namespace RotationPlateCreation
	{
		struct Result
		{
			Result() : success(false) { }

			bool success;
			QString error;
			boost::optional<GPlatesModel::FeatureHandle::non_null_ptr_type> feature;
		};

		/** Returns every plate ID referenced by a loaded rotation sequence. */
		std::set<GPlatesModel::integer_plate_id_type>
		collect_loaded_plate_ids(
				GPlatesAppLogic::ApplicationState &application_state);

		/** Returns a deterministic unused plate ID at or above @a first_candidate. */
		GPlatesModel::integer_plate_id_type
		next_unused_plate_id(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesModel::integer_plate_id_type first_candidate = 1);

		/**
		 * Validates a birth against all loaded rotation collections and prepares,
		 * but does not attach, a two-pole identity rotation sequence. Callers can
		 * therefore add it as part of their own atomic undo command.
		 */
		Result
		prepare_identity_sequence(
				GPlatesAppLogic::ApplicationState &application_state,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &rotation_collection,
				GPlatesModel::integer_plate_id_type moving_plate,
				GPlatesModel::integer_plate_id_type fixed_plate,
				double birth_time,
				double youngest_time = 0.0);
	}
}

#endif // GPLATES_VIEWOPERATIONS_ROTATIONPLATECREATION_H
