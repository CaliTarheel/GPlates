/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef GPLATES_VIEWOPERATIONS_RELEASEPLATEOPERATION_H
#define GPLATES_VIEWOPERATIONS_RELEASEPLATEOPERATION_H

#include <memory>
#include <vector>
#include <boost/noncopyable.hpp>
#include <QString>
#include <QUndoCommand>

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"

class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
}
namespace GPlatesPresentation
{
	class ViewState;
}

namespace GPlatesViewOperations
{
	class ReleasePlateOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			OPERATION_CANCELLED,
			OPERATION_COMPLETED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(
					Outcome outcome_,
					const QString &message_) :
				outcome(outcome_), message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		struct Summary
		{
			QString error;
			QString review;
			unsigned int younger_samples = 0;
			unsigned int ancestor_pins = 0;
			GPlatesModel::integer_plate_id_type previous_parent = 0;
			std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref> changed_collections;
		};

		/**
		 * Prepare one detached, undoable release to Plate 0, holding the event pose through 0 Ma.
		 * The supplied collections must be exactly one rotation model's inputs, in model order.
		 * Replaced younger rows are retained disabled; older rows, wrappers and comments survive.
		 * Features supplied for preflight are reconstructed before/after at the release age;
		 * an altered pose (including half-stage ridges/flowlines) refuses the entire transaction.
		 * A null command reports the reason in Summary::error. No files or model objects change.
		 */
		static
		std::unique_ptr<QUndoCommand>
		create_undo_command(
				GPlatesModel::ModelInterface model,
				const std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref> &rotations,
				GPlatesModel::integer_plate_id_type plate,
				double time,
				Summary &summary,
				const std::vector<GPlatesModel::FeatureHandle::weak_ref> &features = {},
				bool extend_to_distant_past = false);

		ReleasePlateOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result
		trigger(
				QWidget *parent);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
	};
}

#endif // GPLATES_VIEWOPERATIONS_RELEASEPLATEOPERATION_H
