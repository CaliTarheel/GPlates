/* $Id$ */

/**
 * \file
 * Persistent Worldbuilding Pasta continent-collision and orogeny review.
 */

#ifndef GPLATES_VIEWOPERATIONS_COLLISIONOROGENYOPERATION_H
#define GPLATES_VIEWOPERATIONS_COLLISIONOROGENYOPERATION_H

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <QString>

#include "CollisionGeometry.h"
#include "RenderedGeometryCollection.h"

#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"
#include "model/types.h"


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
	class CollisionOrogenyOperation : private boost::noncopyable
	{
	public:
		enum SelectionMode
		{
			NOT_SELECTING,
			SELECTING_INCOMING_CONTINENT,
			SELECTING_RECEIVING_CONTINENT,
			SELECTING_CONSUMED_TRENCH
		};

		enum Outcome
		{
			SELECTION_ARMED,
			INCOMING_CAPTURED,
			RECEIVING_CAPTURED,
			TRENCH_CAPTURED,
			PREVIEW_READY,
			COLLISION_COMMITTED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Options
		{
			Options();

			bool auto_classify;
			CollisionGeometry::CollisionType collision_type;
			unsigned int precursor_collision_count;
			double contact_threshold_km;
			bool automatic_belt_width;
			double belt_width_km;
			unsigned int smoothing_iterations;
			double belt_irregularity;
			double active_duration_ma;
			double old_orogen_age_ma;
			bool terminate_consumed_trench;
			bool allow_non_convergent;
			bool deform_contact_margins;
			double deformation_reach_km;
			bool retire_incoming_plate;
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_), message(message_)
			{ }
			Outcome outcome;
			QString message;
		};

		CollisionOrogenyOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result arm_incoming_selection();
		Result arm_receiving_selection();
		Result arm_trench_selection();
		Result capture_armed_selection();
		Result preview(const Options &options);
		Result commit();

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool has_incoming() const;
		bool has_receiving() const;
		bool has_trench() const;
		bool has_preview() const { return static_cast<bool>(d_preview); }
		QString incoming_status() const;
		QString receiving_status() const;
		QString trench_status() const;
		void clear_trench();
		void clear_preview();
		void reset();

	private:
		struct CapturedContinent
		{
			CapturedContinent(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesModel::FeatureHandle::iterator &geometry_property_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
					GPlatesModel::integer_plate_id_type plate_id_,
					double reconstruction_time_) :
				feature(feature_), geometry_property(geometry_property_), polygon(polygon_), plate_id(plate_id_),
				reconstruction_time(reconstruction_time_)
			{ }
			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesModel::FeatureHandle::iterator geometry_property;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
			GPlatesModel::integer_plate_id_type plate_id;
			double reconstruction_time;
		};

		struct CapturedTrench
		{
			CapturedTrench(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
					double start_time_,
					double reconstruction_time_) :
				feature(feature_), polyline(polyline_), start_time(start_time_),
				reconstruction_time(reconstruction_time_)
			{ }
			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline;
			double start_time;
			double reconstruction_time;
		};

		struct Preview
		{
			Preview(
					const Options &options_,
					CollisionGeometry::CollisionType collision_type_,
					double belt_width_km_,
					const CollisionGeometry::Result &geometry_,
					const boost::optional<double> &closing_speed_) :
				options(options_), collision_type(collision_type_),
				belt_width_km(belt_width_km_), geometry(geometry_),
				closing_speed_cm_per_year(closing_speed_)
			{ }
			Options options;
			CollisionGeometry::CollisionType collision_type;
			double belt_width_km;
			CollisionGeometry::Result geometry;
			boost::optional<double> closing_speed_cm_per_year;
		};

		boost::optional<double> estimate_closing_speed(
				const CollisionGeometry::Result &geometry) const;

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		boost::optional<CapturedContinent> d_incoming;
		boost::optional<CapturedContinent> d_receiving;
		boost::optional<CapturedTrench> d_trench;
		boost::optional<Preview> d_preview;
		RenderedGeometryCollection::child_layer_owner_ptr_type d_preview_layer;
	};
}

#endif // GPLATES_VIEWOPERATIONS_COLLISIONOROGENYOPERATION_H
