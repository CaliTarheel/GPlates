/* $Id$ */

/**
 * \file
 * Persistent post-collision suture reactivation and assemblage rerifting.
 */

#ifndef GPLATES_VIEWOPERATIONS_POSTCOLLISIONRIFTOPERATION_H
#define GPLATES_VIEWOPERATIONS_POSTCOLLISIONRIFTOPERATION_H

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <QString>

#include "PostCollisionRiftGeometry.h"
#include "RenderedGeometryCollection.h"
#include "SplitPlateOperation.h"

#include "app-logic/ReconstructedFeatureGeometry.h"

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
	class PostCollisionRiftOperation : private boost::noncopyable
	{
	public:
		enum SelectionMode
		{
			NOT_SELECTING,
			SELECTING_HOST_CONTINENT,
			SELECTING_SUTURE
		};

		enum Outcome
		{
			SELECTION_ARMED,
			HOST_CAPTURED,
			SUTURE_CAPTURED,
			PREVIEW_READY,
			RERIFT_COMMITTED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Options
		{
			Options();

			double suture_offset_km;
			double wiggle;
			double maximum_segment_length_km;
			double end_extension_km;
			int side;
			unsigned int random_seed;
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_), message(message_)
			{ }

			Outcome outcome;
			QString message;
		};

		PostCollisionRiftOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result arm_host_selection();
		Result arm_suture_selection();
		Result capture_armed_selection();
		Result preview(const Options &options);
		Result commit(
				const QString &left_name,
				GPlatesModel::integer_plate_id_type left_plate_id,
				const QString &right_name,
				GPlatesModel::integer_plate_id_type right_plate_id);

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool has_host() const;
		bool has_suture() const;
		bool has_preview() const { return static_cast<bool>(d_preview); }
		QString host_status() const;
		QString suture_status() const;
		GPlatesModel::integer_plate_id_type source_plate_id() const;
		GPlatesModel::integer_plate_id_type suggest_new_plate_id() const;
		void clear_preview();
		void reset();

	private:
		struct CapturedHost
		{
			CapturedHost(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesModel::FeatureHandle::iterator &geometry_property_,
					const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type &rfg_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
					GPlatesModel::integer_plate_id_type plate_id_,
					double reconstruction_time_) :
				feature(feature_), geometry_property(geometry_property_), rfg(rfg_),
				polygon(polygon_), plate_id(plate_id_), reconstruction_time(reconstruction_time_)
			{ }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesModel::FeatureHandle::iterator geometry_property;
			GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type rfg;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
			GPlatesModel::integer_plate_id_type plate_id;
			double reconstruction_time;
		};

		struct CapturedSuture
		{
			CapturedSuture(
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
			Preview(
					const Options &options_,
					const PostCollisionRiftGeometry::Result &geometry_,
					const SplitPlateGeometry::Result &host_split_,
					unsigned int rejected_craton_routes_) :
				options(options_), geometry(geometry_), host_split(host_split_),
				rejected_craton_routes(rejected_craton_routes_)
			{ }

			Options options;
			PostCollisionRiftGeometry::Result geometry;
			SplitPlateGeometry::Result host_split;
			unsigned int rejected_craton_routes;
		};

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		boost::optional<CapturedHost> d_host;
		boost::optional<CapturedSuture> d_suture;
		boost::optional<Preview> d_preview;
		RenderedGeometryCollection::child_layer_owner_ptr_type d_preview_layer;
	};
}

#endif // GPLATES_VIEWOPERATIONS_POSTCOLLISIONRIFTOPERATION_H
