/* $Id$ */

/**
 * \file
 * Persistent polygon Boolean workflow for World Building.
 */

#ifndef GPLATES_VIEWOPERATIONS_BOOLEANPOLYGONOPERATION_H
#define GPLATES_VIEWOPERATIONS_BOOLEANPOLYGONOPERATION_H

#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>

#include <QString>

#include "SubductionCutterGeometry.h"
#include "RenderedGeometryCollection.h"

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

namespace GPlatesPresentation
{
	class ViewState;
}

namespace GPlatesViewOperations
{
	/**
	 * Captures a first polygon and one or more operand polygons while a modeless
	 * dialog remains open, then applies one undoable Boolean edit.
	 */
	class BooleanPolygonOperation : private boost::noncopyable
	{
	public:
		enum SelectionMode
		{
			NOT_SELECTING,
			SELECTING_FIRST,
			SELECTING_OPERAND
		};

		enum Outcome
		{
			SELECTION_ARMED,
			FIRST_CAPTURED,
			OPERAND_CAPTURED,
			PREVIEW_READY,
			BOOLEAN_COMPLETED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_), message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		BooleanPolygonOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result arm_first_selection();
		Result arm_operand_selection();
		Result capture_armed_selection();
		Result preview(SubductionCutterGeometry::BooleanOperation operation);
		Result apply(SubductionCutterGeometry::BooleanOperation operation);

		void remove_last_operand();
		void clear_operands();
		void clear_preview();
		void reset();

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool has_first() const { return static_cast<bool>(d_first); }
		unsigned int operand_count() const
		{
			return static_cast<unsigned int>(d_operands.size());
		}
		bool preview_ready() const { return d_preview_ready; }
		QString first_status() const;
		QString operands_status() const;

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

		boost::optional<CapturedPolygon> focused_polygon(QString &error) const;
		bool already_captured(const CapturedPolygon &candidate) const;
		bool calculate_boolean(
				SubductionCutterGeometry::BooleanOperation operation,
				SubductionCutterGeometry::BooleanResult &result,
				QString &error) const;

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesModel::ModelInterface d_model_interface;
		RenderedGeometryCollection::child_layer_owner_ptr_type d_preview_layer;
		SelectionMode d_selection_mode;
		boost::optional<CapturedPolygon> d_first;
		std::vector<CapturedPolygon> d_operands;
		bool d_preview_ready;
		SubductionCutterGeometry::BooleanOperation d_preview_operation;
	};
}

#endif // GPLATES_VIEWOPERATIONS_BOOLEANPOLYGONOPERATION_H
