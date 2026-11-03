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

#ifndef GPLATES_VIEWOPERATIONS_RIFTMAKEROPERATION_H
#define GPLATES_VIEWOPERATIONS_RIFTMAKEROPERATION_H

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
	/** Cut polygons with a retained sketch and create ridges along interior cut spans. */
	class RiftMakerOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			RIFT_CREATED,
			CANCELLED,
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

		RiftMakerOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result
		trigger(
				QWidget *parent_widget);

		/** Detached native half-stage ridge, coincident with the given span at start_time.
		 * Requires distinct plates with rotations at that age. Throws before model changes.
		 */
		static
		GPlatesModel::FeatureHandle::non_null_ptr_type
		create_mid_ocean_ridge(
				const GPlatesMaths::PolylineOnSphere &span,
				GPlatesModel::integer_plate_id_type left_plate,
				GPlatesModel::integer_plate_id_type right_plate,
				const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
				double start_time,
				double end_time,
				const QString &name);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
	};
}
#endif // GPLATES_VIEWOPERATIONS_RIFTMAKEROPERATION_H