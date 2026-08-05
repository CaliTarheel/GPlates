/* $Id$ */

/**
 * \file
 * Persistent Worldbuilding Pasta LIP, hotspot and native hotspot-trail review.
 */

#ifndef GPLATES_VIEWOPERATIONS_GENERATEMANTLEEVENTSOPERATION_H
#define GPLATES_VIEWOPERATIONS_GENERATEMANTLEEVENTSOPERATION_H

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <QString>

#include "MantleEventGeometry.h"
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
	class GenerateMantleEventsOperation : private boost::noncopyable
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
			PREVIEW_READY,
			EVENTS_COMMITTED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Options
		{
			Options();

			bool rift_triggered;
			double lip_diameter_km;
			double lip_irregularity;
			double maximum_segment_length_km;
			unsigned int random_seed;
			double active_lip_duration_ma;
			bool create_hotspot;
			double hotspot_lifetime_ma;
			double trail_step_ma;
			GPlatesModel::integer_plate_id_type mantle_plate_id;
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_), message(message_)
			{ }

			Outcome outcome;
			QString message;
		};

		GenerateMantleEventsOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result arm_continent_selection();
		Result arm_rift_selection();
		Result capture_armed_selection();
		Result preview(const Options &options);
		Result commit();

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool has_continent() const;
		bool has_rift() const;
		bool has_preview() const { return static_cast<bool>(d_preview); }
		QString continent_status() const;
		QString rift_status() const;
		void clear_rift();
		void clear_preview();
		void reset();

	private:
		struct CapturedContinent
		{
			CapturedContinent(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
					GPlatesModel::integer_plate_id_type plate_id_,
					double reconstruction_time_) :
				feature(feature_), polygon(polygon_), plate_id(plate_id_),
				reconstruction_time(reconstruction_time_)
			{ }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
			GPlatesModel::integer_plate_id_type plate_id;
			double reconstruction_time;
		};

		struct CapturedRift
		{
			CapturedRift(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
					double reconstruction_time_) :
				feature(feature_), polyline(polyline_), reconstruction_time(reconstruction_time_)
			{ }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline;
			double reconstruction_time;
		};

		struct Preview
		{
			Preview(const Options &options_, const MantleEventGeometry::Result &geometry_) :
				options(options_), geometry(geometry_)
			{ }

			Options options;
			MantleEventGeometry::Result geometry;
		};

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		boost::optional<CapturedContinent> d_continent;
		boost::optional<CapturedRift> d_rift;
		boost::optional<Preview> d_preview;
		RenderedGeometryCollection::child_layer_owner_ptr_type d_preview_layer;
	};
}

#endif // GPLATES_VIEWOPERATIONS_GENERATEMANTLEEVENTSOPERATION_H
