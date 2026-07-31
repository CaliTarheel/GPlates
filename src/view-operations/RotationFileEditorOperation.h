/* $Id$ */

/**
 * \file
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
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef GPLATES_VIEWOPERATIONS_ROTATIONFILEEDITOROPERATION_H
#define GPLATES_VIEWOPERATIONS_ROTATIONFILEEDITOROPERATION_H

#include <boost/noncopyable.hpp>
#include <QString>

#include "model/ModelInterface.h"


class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesViewOperations
{
	class RotationFileEditorOperation :
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
			Result(Outcome outcome_, const QString &message_) : outcome(outcome_), message(message_) {  }
			Outcome outcome;
			QString message;
		};

		explicit
		RotationFileEditorOperation(
				GPlatesAppLogic::ApplicationState &application_state);

		Result
		trigger(
				QWidget *parent);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesModel::ModelInterface d_model_interface;
	};
}

#endif // GPLATES_VIEWOPERATIONS_ROTATIONFILEEDITOROPERATION_H
