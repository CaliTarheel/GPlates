/* $Id$ */

/**
 * \file
 * Creates a new central oceanic plate inside a local three-MOR void.
 */

#ifndef GPLATES_VIEWOPERATIONS_CREATEPACIFICPLATEOPERATION_H
#define GPLATES_VIEWOPERATIONS_CREATEPACIFICPLATEOPERATION_H

#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <QString>

#include "maths/PointOnSphere.h"
#include "model/FeatureCollectionHandle.h"
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
	class CreateOceanCrustOperation;

	class CreatePacificPlateOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			SELECTION_REQUIRED,
			OPERATION_CANCELLED,
			OPERATION_COMPLETED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_), message(message_) { }

			Outcome outcome;
			QString message;
		};

		CreatePacificPlateOperation(
				CreateOceanCrustOperation &mor_selection,
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		/** Arms the next ordinary globe/map click as the local-void seed. */
		void arm_seed_capture();

		/** Returns true only when an armed on-Earth click was consumed. */
		bool capture_seed(const GPlatesMaths::PointOnSphere &point, bool is_on_earth, QString &message);

		void clear_seed();

		bool seed_is_captured() const { return static_cast<bool>(d_void_seed); }

		/** True while the next ordinary click is armed to capture the local-void seed. */
		bool seed_capture_is_armed() const { return d_seed_capture_armed; }

		/**
		 * Overrides where the three newly-created MidOceanRidge features go, set from the
		 * Ocean Crust Creation window up front rather than asked again in this tool's own
		 * dialog. If never set (or cleared), new MORs go to the same collection as the new
		 * crust, matching the previous combined behaviour.
		 */
		void
		set_preset_mor_destination(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
		{
			d_preset_mor_destination = collection;
		}

		void
		clear_preset_mor_destination()
		{
			d_preset_mor_destination = GPlatesModel::FeatureCollectionHandle::weak_ref();
		}

		const GPlatesModel::FeatureCollectionHandle::weak_ref &
		preset_mor_destination() const
		{
			return d_preset_mor_destination;
		}

		Result trigger(QWidget *parent_widget);

	private:
		CreateOceanCrustOperation &d_mor_selection;
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		bool d_seed_capture_armed;
		boost::optional<GPlatesMaths::PointOnSphere> d_void_seed;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_last_output_collection;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_last_rotation_collection;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_preset_mor_destination;
	};
}

#endif // GPLATES_VIEWOPERATIONS_CREATEPACIFICPLATEOPERATION_H
