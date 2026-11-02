/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The University of Sydney, Australia
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef GPLATES_VIEWOPERATIONS_MAKEOCEANCRUSTOPERATION_H
#define GPLATES_VIEWOPERATIONS_MAKEOCEANCRUSTOPERATION_H

#include <vector>
#include <boost/noncopyable.hpp>
#include <QString>

#include "model/FeatureHandle.h"
#include "model/types.h"

class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
	class ReconstructionTreeCreator;
}

namespace GPlatesMaths
{
	class PolylineOnSphere;
}

namespace GPlatesPresentation
{
	class ViewState;
}

namespace GPlatesViewOperations
{
	/** Create symmetric native flowlines from the vertices of visible mid-ocean ridges. */
	class MakeOceanCrustOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			FLOWLINES_CREATED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(
					Outcome outcome_,
					const QString &message_) :
				outcome(outcome_),
				message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		MakeOceanCrustOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result
		trigger(
				QWidget *parent_widget);

		/** Youngest-first times, including both endpoints and a shorter final interval if needed.
		 * Throws std::invalid_argument for invalid/unrepresentable ranges or excessive sample counts.
		 */
		static
		std::vector<double>
		sample_times(
				double start_time,
				double future_duration,
				double step_interval);

		/**
		 * Return a detached feature whose seeds coincide with reconstructed_ridge at start_time.
		 * Each vertex seeds both halves of a native Flowline. All rotations must exist at the
		 * requested times. The caller retains the ridge's reconstruction-model connection.
		 * Throws before changing the model if the inputs cannot produce the requested flowlines.
		 */
		static
		GPlatesModel::FeatureHandle::non_null_ptr_type
		create_flowline(
				const GPlatesMaths::PolylineOnSphere &reconstructed_ridge,
				GPlatesModel::integer_plate_id_type left_plate_id,
				GPlatesModel::integer_plate_id_type right_plate_id,
				const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
				double start_time,
				double future_duration,
				double step_interval,
				const QString &name);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
	};
}

#endif // GPLATES_VIEWOPERATIONS_MAKEOCEANCRUSTOPERATION_H
