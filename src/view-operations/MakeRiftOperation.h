/* $Id$ */

/**
 * \file
 * Turns a user-selected rift polyline into a continental split and a real
 * half-stage-rotation ridge.
 */

#ifndef GPLATES_VIEWOPERATIONS_MAKERIFTOPERATION_H
#define GPLATES_VIEWOPERATIONS_MAKERIFTOPERATION_H

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <QString>

#include "app-logic/ReconstructedFeatureGeometry.h"

#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"


class QWidget;

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

namespace GPlatesViewOperations
{
	class MakeRiftOperation :
			private boost::noncopyable
	{
	public:
		enum SelectionMode
		{
			NOT_SELECTING,
			SELECTING_CONTINENT,
			SELECTING_RIFT
		};

		enum Outcome
		{
			SELECTION_ARMED,
			CONTINENT_CAPTURED,
			RIFT_CAPTURED,
			RIFT_COMPLETED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_),
				message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		MakeRiftOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result arm_continent_selection();

		Result arm_rift_selection();

		Result capture_armed_selection();

		Result cut(QWidget *parent_widget);

		SelectionMode selection_mode() const { return d_selection_mode; }

		bool can_cut() const;

		QString continent_status() const;

		QString rift_status() const;

		void
		reset();

	private:
		struct CapturedContinent
		{
			CapturedContinent(
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

		struct CapturedRift
		{
			CapturedRift(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type &reconstructed_feature_geometry_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
					double reconstruction_time_) :
				feature(feature_),
				reconstructed_feature_geometry(reconstructed_feature_geometry_),
				polyline(polyline_),
				reconstruction_time(reconstruction_time_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type reconstructed_feature_geometry;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline;
			double reconstruction_time;
		};

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		boost::optional<CapturedContinent> d_captured_continent;
		boost::optional<CapturedRift> d_captured_rift;
	};
}

#endif // GPLATES_VIEWOPERATIONS_MAKERIFTOPERATION_H
