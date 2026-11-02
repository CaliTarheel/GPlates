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

#ifndef GPLATES_VIEWOPERATIONS_ZEROMAROTATIONSOPERATION_H
#define GPLATES_VIEWOPERATIONS_ZEROMAROTATIONSOPERATION_H

#include <memory>
#include <boost/noncopyable.hpp>
#include <QString>
#include <QStringList>
#include <QUndoCommand>

#include "model/FeatureCollectionHandle.h"
#include "model/ModelInterface.h"

class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesViewOperations
{
	class ZeroMaRotationsOperation :
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
			unsigned int updated_plates = 0;
			unsigned int added_zero_samples = 0;
			unsigned int replaced_zero_samples = 0;
			QStringList skipped_plates;
		};

		/**
		 * Prepare detached snapshots; the model changes only when the command is executed.
		 * One enabled sample at the youngest strictly positive time supplies each plate's
		 * 0 Ma rotation and reference frame. Disabled rows/sequences are left alone.
		 * Ambiguous sources and plates without positive samples are reported and skipped.
		 * A null command means there are no applicable plates. No files are written.
		 */
		static
		std::unique_ptr<QUndoCommand>
		create_undo_command(
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				Summary &summary);

		explicit
		ZeroMaRotationsOperation(
				GPlatesAppLogic::ApplicationState &application_state);

		Result
		trigger(
				QWidget *parent);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
	};
}

#endif // GPLATES_VIEWOPERATIONS_ZEROMAROTATIONSOPERATION_H
