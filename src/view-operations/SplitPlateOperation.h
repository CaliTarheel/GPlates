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

#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
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

namespace GPlatesViewOperations
{
	/**
	 * A picker-driven operation: arm polygon selection, capture a click (repeatable, to split
	 * more than one polygon with the same cutter), arm polyline selection, capture a click,
	 * then commit the split(s) as one explicit, atomic step - mirroring BooleanPolygonOperation's
	 * shape rather than the old single trigger() that captured the polyline and performed the
	 * split in the same call.
	 */
	class SplitPlateOperation :
			private boost::noncopyable
	{
	public:
		enum SelectionMode
		{
			NOT_SELECTING,
			SELECTING_POLYGON,
			SELECTING_POLYLINE
		};

		enum Outcome
		{
			SELECTION_ARMED,
			POLYGON_CAPTURED,
			POLYLINE_CAPTURED,
			SPLIT_COMPLETED,
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

		SplitPlateOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state);

		Result arm_polygon_selection();
		Result arm_polyline_selection();

		/**
		 * The globe's feature focus changed. If a selection is armed, capture it as another
		 * polygon (added to the running set) or the polyline according to @a selection_mode;
		 * otherwise does nothing.
		 */
		Result capture_armed_selection();

		/** Forgets every captured polygon, keeping any captured polyline. */
		void clear_polygons();

		/**
		 * Perform the split on every captured polygon using the captured polyline, as one
		 * undoable edit. Requires at least one polygon and a polyline to be captured first.
		 */
		Result commit();

		void reset();

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool has_polygon() const { return !d_captured_polygons.empty(); }
		unsigned int polygon_count() const { return static_cast<unsigned int>(d_captured_polygons.size()); }
		bool has_polyline() const { return static_cast<bool>(d_captured_polyline); }
		QString polygon_status() const;
		QString polyline_status() const;

		/**
		 * The captured polygons' geometries, for a caller that wants to highlight them.
		 */
		std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>
		polygons() const
		{
			std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> result;
			result.reserve(d_captured_polygons.size());
			for (std::vector<CapturedPolygon>::const_iterator polygon_iter = d_captured_polygons.begin();
					polygon_iter != d_captured_polygons.end(); ++polygon_iter)
			{
				result.push_back(polygon_iter->polygon);
			}
			return result;
		}

		/**
		 * The captured polyline's geometry, for a caller that wants to highlight it.
		 */
		boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>
		polyline() const
		{
			return d_captured_polyline
					? boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>(
							d_captured_polyline->polyline)
					: boost::none;
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

		struct CapturedPolyline
		{
			CapturedPolyline(
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
					double reconstruction_time_) :
				polyline(polyline_),
				reconstruction_time(reconstruction_time_)
			{  }

			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline;
			double reconstruction_time;
		};

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		std::vector<CapturedPolygon> d_captured_polygons;
		boost::optional<CapturedPolyline> d_captured_polyline;
	};
}

#endif // GPLATES_VIEWOPERATIONS_SPLITPLATEOPERATION_H
