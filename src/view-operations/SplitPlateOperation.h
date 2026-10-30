/* $Id$ */

/**
 * \file
 * Declares the World Building "Split Plate" operation.
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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef GPLATES_VIEWOPERATIONS_SPLITPLATEOPERATION_H
#define GPLATES_VIEWOPERATIONS_SPLITPLATEOPERATION_H

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <QString>

#include "app-logic/ReconstructedFeatureGeometry.h"

#include "maths/PolygonOnSphere.h"

#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"


namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesGui
{
	class FeatureFocus;
}

namespace GPlatesMaths
{
	class PolylineOnSphere;
}

namespace GPlatesViewOperations
{
	/**
	 * A two-stage operation that captures a focused polygon and then splits it
	 * using a subsequently focused polyline.
	 */
	class SplitPlateOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			POLYGON_CAPTURED,
			SPLIT_COMPLETED,
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

		struct SplitPolygonResult
		{
			SplitPolygonResult(
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon1_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon2_) :
				polygon1(polygon1_),
				polygon2(polygon2_)
			{  }

			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon1;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon2;
		};

		/**
		 * Split in the input coordinate frame, without changing features or reconstruction.
		 *
		 * Requires exactly two clean segment crossings of the exterior boundary.
		 * Vertex contacts, tangencies, overlaps and intersections with holes are rejected;
		 * uncut holes are preserved in the child containing them. On rejection, returns
		 * none and sets error_message. Construction errors can throw.
		 * The caller reverse reconstructs successful outputs before editing the model.
		 */
		static
		boost::optional<SplitPolygonResult>
		split_polygon(
				QString &error_message,
				const GPlatesMaths::PolygonOnSphere &polygon,
				const GPlatesMaths::PolylineOnSphere &polyline);

		SplitPlateOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state);

		/**
		 * Capture the focused polygon, or use the focused polyline to split a
		 * polygon captured by a previous call.
		 */
		Result
		trigger();

		void
		reset();

		bool
		has_captured_polygon() const
		{
			return static_cast<bool>(d_captured_polygon);
		}

	private:
		struct CapturedPolygon
		{
			CapturedPolygon(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesModel::FeatureHandle::iterator &geometry_property_,
					const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type &reconstructed_feature_geometry_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
					double reconstruction_time_) :
				feature(feature_),
				geometry_property(geometry_property_),
				reconstructed_feature_geometry(reconstructed_feature_geometry_),
				polygon(polygon_),
				reconstruction_time(reconstruction_time_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesModel::FeatureHandle::iterator geometry_property;
			GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type reconstructed_feature_geometry;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
			double reconstruction_time;
		};

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesModel::ModelInterface d_model_interface;
		boost::optional<CapturedPolygon> d_captured_polygon;
	};
}

#endif // GPLATES_VIEWOPERATIONS_SPLITPLATEOPERATION_H
