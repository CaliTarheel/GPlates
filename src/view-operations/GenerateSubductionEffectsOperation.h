/* $Id$ */

/**
 * \file
 * Persistent selection, preview and commit workflow for Worldbuilding Pasta
 * island arcs and active-margin orogenic belts.
 */

#ifndef GPLATES_VIEWOPERATIONS_GENERATESUBDUCTIONEFFECTSOPERATION_H
#define GPLATES_VIEWOPERATIONS_GENERATESUBDUCTIONEFFECTSOPERATION_H

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <QString>
#include <vector>

#include "SubductionEffectsGeometry.h"
#include "SubductionLifecyclePlanner.h"
#include "RenderedGeometryCollection.h"

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
	class GenerateSubductionEffectsOperation :
			private boost::noncopyable
	{
	public:
		enum SelectionMode
		{
			NOT_SELECTING,
			SELECTING_SUBDUCTION,
			SELECTING_CONTINENT
		};

		enum Outcome
		{
			SELECTION_ARMED,
			SUBDUCTION_CAPTURED,
			CONTINENT_CAPTURED,
			PREVIEW_READY,
			EFFECTS_COMMITTED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Options
		{
			Options();

			SubductionEffectsGeometry::EffectType effect_type;
			bool flip_declared_polarity;
			bool allow_early_island_arc;
			double island_arc_delay_ma;
			double trim_start_percent;
			double trim_end_percent;
			double offset_km;
			double island_irregularity;
			double belt_width_km;
			SubductionLifecyclePlanner::EventType lifecycle_event;
			GPlatesModel::integer_plate_id_type subducting_plate;
			double migration_offset_km;
			bool isolates_plate_fragment;
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_), message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		GenerateSubductionEffectsOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result arm_subduction_selection();
		Result arm_continent_selection();
		Result capture_armed_selection();
		Result preview(const Options &options);
		Result commit();

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool has_subduction() const;
		bool has_continent() const;
		bool has_preview() const { return static_cast<bool>(d_preview); }
		bool declared_overriding_side_is_left() const;
		bool recommended_polarity_flip() const;
		double subduction_age_ma() const;
		GPlatesModel::integer_plate_id_type suggested_subducting_plate() const;
		QString subduction_status() const;
		QString continent_status() const;
		void clear_continent();
		void clear_preview();
		void reset();

	private:
		struct CapturedSubduction
		{
			CapturedSubduction(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
					GPlatesModel::integer_plate_id_type overriding_plate_,
					GPlatesModel::integer_plate_id_type subducting_plate_,
					bool declared_left_,
					double start_time_,
					double reconstruction_time_) :
				feature(feature_), polyline(polyline_), overriding_plate(overriding_plate_),
				subducting_plate(subducting_plate_),
				declared_left(declared_left_), start_time(start_time_),
				reconstruction_time(reconstruction_time_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline;
			GPlatesModel::integer_plate_id_type overriding_plate;
			GPlatesModel::integer_plate_id_type subducting_plate;
			bool declared_left;
			double start_time;
			double reconstruction_time;
		};

		struct CapturedContinent
		{
			CapturedContinent(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
					GPlatesModel::integer_plate_id_type plate_id_,
					double reconstruction_time_) :
				feature(feature_), polygon(polygon_), plate_id(plate_id_),
				reconstruction_time(reconstruction_time_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
			GPlatesModel::integer_plate_id_type plate_id;
			double reconstruction_time;
		};

		struct Preview
		{
			struct LandBelt
			{
				LandBelt(
						const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
						GPlatesModel::integer_plate_id_type plate_id_) :
					polygon(polygon_), plate_id(plate_id_)
				{  }

				GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
				GPlatesModel::integer_plate_id_type plate_id;
			};

			Preview(
					const Options &options_,
					const SubductionEffectsGeometry::Result &geometry_,
					const SubductionLifecyclePlanner::Plan &lifecycle_plan_,
					const std::vector<LandBelt> &land_belts_ = std::vector<LandBelt>()) :
				options(options_), geometry(geometry_), lifecycle_plan(lifecycle_plan_), land_belts(land_belts_)
			{  }

			Options options;
			SubductionEffectsGeometry::Result geometry;
			SubductionLifecyclePlanner::Plan lifecycle_plan;
			std::vector<LandBelt> land_belts;
		};

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		boost::optional<CapturedSubduction> d_captured_subduction;
		boost::optional<CapturedContinent> d_captured_continent;
		boost::optional<Preview> d_preview;
		RenderedGeometryCollection::child_layer_owner_ptr_type d_preview_layer;
	};
}

#endif // GPLATES_VIEWOPERATIONS_GENERATESUBDUCTIONEFFECTSOPERATION_H
