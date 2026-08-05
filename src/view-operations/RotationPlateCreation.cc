/* $Id$ */

/**
 * \file
 * Reusable validation and feature construction for a newly born plate.
 */

#include "RotationPlateCreation.h"

#include <algorithm>
#include <set>
#include <vector>

#include <QObject>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/TRSUtils.h"

#include "maths/FiniteRotation.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/PropertyName.h"
#include "model/TopLevelPropertyInline.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimeInstant.h"
#include "property-values/GpmlFiniteRotation.h"
#include "property-values/GpmlFiniteRotationSlerp.h"
#include "property-values/GpmlInterpolationFunction.h"
#include "property-values/GpmlIrregularSampling.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/GpmlTimeSample.h"
#include "property-values/StructuralType.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	typedef std::vector<GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type> sample_seq_type;

	void
	collect_plate_ids(
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
			std::set<GPlatesModel::integer_plate_id_type> &plate_ids)
	{
		if (!collection.is_valid())
		{
			return;
		}
		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
			feature_iter != collection->end(); ++feature_iter)
		{
			GPlatesAppLogic::TRSUtils::TRSFinder finder;
			finder.visit_feature((*feature_iter)->reference());
			if (finder.can_process_trs())
			{
				plate_ids.insert(*finder.moving_ref_frame_plate_id());
				plate_ids.insert(*finder.fixed_ref_frame_plate_id());
			}
		}
	}

	GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type
	create_identity_sample(
			double time)
	{
		using namespace GPlatesPropertyValues;
		const StructuralType value_type = StructuralType::create_gpml("FiniteRotation");
		return GpmlTimeSample::create(
				GpmlFiniteRotation::create(GPlatesMaths::FiniteRotation::create_identity_rotation()),
				GmlTimeInstant::create(GeoTimeInstant(time)),
				XsString::create(GPlatesUtils::make_icu_string_from_qstring(
						QObject::tr("GreaterPlates: new plate"))),
				value_type);
	}
}


std::set<GPlatesModel::integer_plate_id_type>
GPlatesViewOperations::RotationPlateCreation::collect_loaded_plate_ids(
		GPlatesAppLogic::ApplicationState &application_state)
{
	std::set<GPlatesModel::integer_plate_id_type> plate_ids;
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded_files =
			application_state.get_feature_collection_file_state().get_loaded_files();
	for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
		file_iter = loaded_files.begin(); file_iter != loaded_files.end(); ++file_iter)
	{
		collect_plate_ids(file_iter->get_file().get_feature_collection(), plate_ids);
	}
	return plate_ids;
}


GPlatesModel::integer_plate_id_type
GPlatesViewOperations::RotationPlateCreation::next_unused_plate_id(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesModel::integer_plate_id_type first_candidate)
{
	const std::set<GPlatesModel::integer_plate_id_type> used =
			collect_loaded_plate_ids(application_state);
	GPlatesModel::integer_plate_id_type candidate = std::max(
			static_cast<GPlatesModel::integer_plate_id_type>(1), first_candidate);
	while (used.count(candidate))
	{
		++candidate;
	}
	return candidate;
}


GPlatesViewOperations::RotationPlateCreation::Result
GPlatesViewOperations::RotationPlateCreation::prepare_identity_sequence(
		GPlatesAppLogic::ApplicationState &application_state,
		const GPlatesModel::FeatureCollectionHandle::weak_ref &rotation_collection,
		GPlatesModel::integer_plate_id_type moving_plate,
		GPlatesModel::integer_plate_id_type fixed_plate,
		double birth_time,
		double youngest_time)
{
	Result result;
	if (!rotation_collection.is_valid())
	{
		result.error = QObject::tr("The selected rotation collection is no longer loaded.");
		return result;
	}
	if (moving_plate == fixed_plate)
	{
		result.error = QObject::tr("A plate cannot be its own parent.");
		return result;
	}
	if (birth_time < youngest_time)
	{
		result.error = QObject::tr("The plate birth time must not be younger than the youngest rotation sample.");
		return result;
	}
	if (collect_loaded_plate_ids(application_state).count(moving_plate))
	{
		result.error = QObject::tr("Plate %1 already exists in a loaded rotation collection.")
				.arg(moving_plate);
		return result;
	}

	std::set<double> validation_times;
	validation_times.insert(youngest_time);
	validation_times.insert(birth_time);
	const std::vector<double> project_times = application_state
			.get_project_timestamp_schedule().timestamps_older_to_younger();
	for (std::vector<double>::const_iterator time_iter = project_times.begin();
		time_iter != project_times.end(); ++time_iter)
	{
		if (*time_iter >= youngest_time && *time_iter <= birth_time)
		{
			validation_times.insert(*time_iter);
		}
	}

	const GPlatesAppLogic::ReconstructionTreeCreator tree_creator = application_state
			.get_current_reconstruction().get_default_reconstruction_layer_output()
					->get_reconstruction_tree_creator();
	for (std::set<double>::const_iterator time_iter = validation_times.begin();
		time_iter != validation_times.end(); ++time_iter)
	{
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type tree =
				tree_creator.get_reconstruction_tree(*time_iter);
		if (!tree->get_composed_absolute_rotation_or_none(fixed_plate))
		{
			result.error = QObject::tr("Parent plate %1 is absent from the rotation tree at %2 Ma.")
					.arg(fixed_plate).arg(*time_iter, 0, 'f', 2);
			return result;
		}
	}

	using namespace GPlatesPropertyValues;
	sample_seq_type samples;
	samples.push_back(create_identity_sample(youngest_time));
	if (birth_time > youngest_time)
	{
		samples.push_back(create_identity_sample(birth_time));
	}
	const StructuralType value_type = StructuralType::create_gpml("FiniteRotation");
	const GpmlIrregularSampling::non_null_ptr_type sampling = GpmlIrregularSampling::create(
			samples,
			GpmlInterpolationFunction::non_null_ptr_type(GpmlFiniteRotationSlerp::create(value_type)),
			value_type);

	GPlatesModel::FeatureHandle::non_null_ptr_type feature = GPlatesModel::FeatureHandle::create(
			GPlatesModel::FeatureType::create_gpml("TotalReconstructionSequence"));
	feature->add(GPlatesModel::TopLevelPropertyInline::create(
			GPlatesModel::PropertyName::create_gpml("fixedReferenceFrame"),
			GpmlPlateId::create(fixed_plate)));
	feature->add(GPlatesModel::TopLevelPropertyInline::create(
			GPlatesModel::PropertyName::create_gpml("movingReferenceFrame"),
			GpmlPlateId::create(moving_plate)));
	feature->add(GPlatesModel::TopLevelPropertyInline::create(
			GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"), sampling));

	result.success = true;
	result.feature = feature;
	return result;
}
