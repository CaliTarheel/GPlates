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
#include <memory>
#include <vector>
#include <QString>

#include "app-logic/ReconstructedFeatureGeometry.h"

#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

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

namespace GPlatesPresentation
{
	class ViewState;
}

class QWidget;
class QUndoCommand;

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
			OPERATION_ERROR,
			OPERATION_CANCELLED
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

		typedef std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>
				polygon_seq_type;
		typedef std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>
				polyline_seq_type;

		/**
		 * Apply cutters in order to every intersected piece, retaining disjoint pieces.
		 * Every cutter must split at least one piece. The same clean-crossing rules as
		 * split_polygon apply. Failure returns none without modifying any input.
		 */
		static
		boost::optional<polygon_seq_type>
		split_polygon_with_polylines(
				QString &error_message,
				const GPlatesMaths::PolygonOnSphere &polygon,
				const polyline_seq_type &polylines);

		/**
		 * Prepare one undo command for already validated pieces in the source coordinate frame.
		 * Keeps the original feature for the first piece and clones its properties for the rest.
		 * Cutter features are never passed to or modified by this command.
		 * Returns an empty pointer for an unavailable source or fewer than two pieces.
		 */
		static
		std::unique_ptr<QUndoCommand>
		create_split_command(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureHandle::weak_ref &polygon_feature,
				const GPlatesModel::FeatureHandle::iterator &polygon_property,
				const polygon_seq_type &polygons);

		SplitPlateOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state);

		/**
		 * Capture the focused polygon, or use the focused polyline to split a
		 * polygon captured by a previous call.
		 */
		Result
		trigger();

		//! Select a polygon and existing polyline cutters in a modal dialog.
		Result
		trigger(
				QWidget *parent,
				const GPlatesPresentation::ViewState &view_state);

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
