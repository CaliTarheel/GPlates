/* $Id$ */

/**
 * \file
 * Selects a rifted continent and its half-stage MOR, previews the inferred
 * spreading direction, and creates an opposite-margin subduction zone.
 */

#ifndef GPLATES_VIEWOPERATIONS_GENERATEINITIALSUBDUCTIONOPERATION_H
#define GPLATES_VIEWOPERATIONS_GENERATEINITIALSUBDUCTIONOPERATION_H

#include <cstddef>
#include <utility>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <QString>

#include "app-logic/ReconstructedFeatureGeometry.h"

#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"
#include "model/types.h"


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
	class VisualLayer;
	class ViewState;
}

namespace GPlatesViewOperations
{
	class RenderedGeometryLayer;

	class GenerateInitialSubductionOperation :
			private boost::noncopyable
	{
	public:
		enum SelectionMode
		{
			NOT_SELECTING,
			SELECTING_CONTINENT,
			SELECTING_MOR
		};

		enum Outcome
		{
			SELECTION_ARMED,
			CONTINENT_CAPTURED,
			MOR_CAPTURED,
			SUBDUCTION_COMPLETED,
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

		GenerateInitialSubductionOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);
		~GenerateInitialSubductionOperation();

		Result arm_continent_selection();
		Result arm_mor_selection();
		Result cancel_selection();
		Result capture_armed_selection();
		Result generate(QWidget *parent_widget);

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool can_generate() const;
		QString continent_status() const;
		QString mor_status() const;
		void reset();

	private:
		struct CapturedContinent
		{
			CapturedContinent(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type &rfg_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
					GPlatesModel::integer_plate_id_type plate_id_,
					double reconstruction_time_) :
				feature(feature_), rfg(rfg_), polygon(polygon_), plate_id(plate_id_),
				reconstruction_time(reconstruction_time_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type rfg;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
			GPlatesModel::integer_plate_id_type plate_id;
			double reconstruction_time;
		};

		struct CapturedMor
		{
			CapturedMor(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
					GPlatesModel::integer_plate_id_type left_plate_,
					GPlatesModel::integer_plate_id_type right_plate_,
					double reconstruction_time_) :
				feature(feature_), polyline(polyline_), left_plate(left_plate_),
				right_plate(right_plate_), reconstruction_time(reconstruction_time_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline;
			GPlatesModel::integer_plate_id_type left_plate;
			GPlatesModel::integer_plate_id_type right_plate;
			double reconstruction_time;
		};

		typedef std::vector<std::pair<boost::weak_ptr<GPlatesPresentation::VisualLayer>, bool> >
				visual_layer_visibility_seq_type;

		std::size_t show_half_stage_mor_candidates();
		void restore_mor_candidate_display();

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		boost::optional<CapturedContinent> d_captured_continent;
		boost::optional<CapturedMor> d_captured_mor;
		boost::shared_ptr<RenderedGeometryLayer> d_mor_candidate_layer;
		visual_layer_visibility_seq_type d_saved_visual_layer_visibility;
	};
}

#endif // GPLATES_VIEWOPERATIONS_GENERATEINITIALSUBDUCTIONOPERATION_H
