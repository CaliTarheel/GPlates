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
				GPlatesAppLogic::ApplicationState &application_state);

		Result arm_first_selection();
		Result arm_operand_selection();
		Result capture_armed_selection();

		/**
		 * Applies the given Boolean operation, replacing the first polygon's geometry with the
		 * result.
		 *
		 * For POLYGON_UNION specifically, the operands are fully absorbed into the result, so
		 * they are also removed from their feature collection (undoable, like everything else
		 * here). Subtract/Intersect/Symmetric-Difference leave every operand untouched, since
		 * none of those absorb an operand the way Union does - a Subtract operand is a cutting
		 * tool, not something becoming part of the result.
		 */
		Result apply(SubductionCutterGeometry::BooleanOperation operation);

		/**
		 * Finds every other polygon in the first polygon's own feature collection that shares
		 * its Plate ID and valid-time range exactly, adds them all as operands in one step, and
		 * applies a union - without the user having to click each one individually.
		 *
		 * Requires only a captured first polygon; any operands already added manually are
		 * replaced by the matched set.
		 */
		Result apply_unify();

		void clear_operands();
		void reset();

		SelectionMode selection_mode() const { return d_selection_mode; }
		bool has_first() const { return static_cast<bool>(d_first); }
		unsigned int operand_count() const
		{
			return static_cast<unsigned int>(d_operands.size());
		}
		QString first_status() const;
		QString operands_status() const;

		/**
		 * The captured first polygon's geometry, for a caller that wants to highlight it -
		 * boost::none if nothing has been captured yet.
		 */
		boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>
		first_polygon() const
		{
			return d_first ? boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>(d_first->polygon)
					: boost::none;
		}

		/**
		 * The captured operand polygons' geometries, for a caller that wants to highlight them.
		 */
		std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>
		operand_polygons() const
		{
			std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> polygons;
			polygons.reserve(d_operands.size());
			for (std::vector<CapturedPolygon>::const_iterator operand_iter = d_operands.begin();
					operand_iter != d_operands.end(); ++operand_iter)
			{
				polygons.push_back(operand_iter->polygon);
			}
			return polygons;
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

		boost::optional<CapturedPolygon> focused_polygon(QString &error) const;
		bool already_captured(const CapturedPolygon &candidate) const;

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesModel::ModelInterface d_model_interface;
		SelectionMode d_selection_mode;
		boost::optional<CapturedPolygon> d_first;
		std::vector<CapturedPolygon> d_operands;
	};
}

#endif // GPLATES_VIEWOPERATIONS_BOOLEANPOLYGONOPERATION_H
