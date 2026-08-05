/* $Id$ */

#include "AdvancePlateMotionOperation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "AdvancePlateMotionGeometry.h"
#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/TRSUtils.h"

#include "feature-visitors/PropertyValueFinder.h"

#include "gui/Colour.h"

#include "maths/FiniteRotation.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"
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
	namespace MotionGeometry = GPlatesViewOperations::AdvancePlateMotionGeometry;
	typedef std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> polyline_seq_type;
	typedef std::vector<GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type> sample_seq_type;
	const double PRESENT_DAY_DRIFT_LOCK_TIME_MA = 1.0;
	const double TIME_EPSILON = 1e-9;

	struct RotationSequence
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator sampling_property;
		GPlatesPropertyValues::GpmlIrregularSampling::non_null_ptr_type sampling;
		GPlatesModel::integer_plate_id_type moving_plate;
		GPlatesModel::integer_plate_id_type fixed_plate;
		double minimum_time;
		double maximum_time;
	};
	typedef std::vector<RotationSequence> sequence_seq_type;

	struct PlateGeometry
	{
		PlateGeometry() : largest_continent_area(-1) { }
		boost::optional<GPlatesMaths::PointOnSphere> centre;
		double largest_continent_area;
		polyline_seq_type ridges;
		polyline_seq_type subduction_zones;
	};

	struct RotationChange
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type before;
		GPlatesModel::TopLevelProperty::non_null_ptr_type after;
	};
	typedef std::vector<RotationChange> rotation_change_seq_type;

	struct PlateProposal
	{
		PlateProposal(
				GPlatesModel::integer_plate_id_type plate_id_,
				const MotionGeometry::Proposal &geometry_,
				const GPlatesMaths::FiniteRotation &target_absolute_,
				const RotationSequence *sequence_) :
			plate_id(plate_id_),
			geometry(geometry_),
			target_absolute(target_absolute_),
			sequence(sequence_) { }

		GPlatesModel::integer_plate_id_type plate_id;
		MotionGeometry::Proposal geometry;
		GPlatesMaths::FiniteRotation target_absolute;
		const RotationSequence *sequence;
	};
	typedef std::map<GPlatesModel::integer_plate_id_type, PlateProposal> proposal_map_type;

	double sample_time(const GPlatesPropertyValues::GpmlTimeSample &sample)
	{
		return sample.valid_time()->get_time_position().value();
	}

	bool sample_less_than(
			const GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type &lhs,
			const GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type &rhs)
	{
		return sample_time(*lhs) < sample_time(*rhs);
	}

	GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type create_rotation_sample(
			double time,
			const GPlatesMaths::FiniteRotation &rotation,
			const QString &description)
	{
		using namespace GPlatesPropertyValues;
		return GpmlTimeSample::create(
				GpmlFiniteRotation::create(rotation),
				GmlTimeInstant::create(GeoTimeInstant(time)),
				XsString::create(GPlatesUtils::make_icu_string_from_qstring(description)),
				StructuralType::create_gpml("FiniteRotation"));
	}

	GPlatesModel::TopLevelProperty::non_null_ptr_type create_sampling_property(
			const sample_seq_type &samples)
	{
		using namespace GPlatesPropertyValues;
		const StructuralType value_type = StructuralType::create_gpml("FiniteRotation");
		return GPlatesModel::TopLevelPropertyInline::create(
				GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"),
				GpmlIrregularSampling::create(
						samples,
						GpmlInterpolationFunction::non_null_ptr_type(
								GpmlFiniteRotationSlerp::create(value_type)),
						value_type));
	}

	sequence_seq_type collect_sequences(
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		sequence_seq_type sequences;
		if (!collection.is_valid())
		{
			return sequences;
		}
		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
				feature_iter != collection->end(); ++feature_iter)
		{
			GPlatesModel::FeatureHandle::weak_ref feature = (*feature_iter)->reference();
			GPlatesAppLogic::TRSUtils::TRSFinder finder;
			finder.visit_feature(feature);
			if (!finder.can_process_trs() || !finder.irregular_sampling() ||
					!finder.irregular_sampling_property_iterator())
			{
				continue;
			}
			const GPlatesPropertyValues::GpmlIrregularSampling::non_null_ptr_type sampling =
					*finder.irregular_sampling();
			double minimum_time = 1e100;
			double maximum_time = -1e100;
			for (GPlatesModel::RevisionedVector<GPlatesPropertyValues::GpmlTimeSample>::const_iterator
					sample_iter = sampling->time_samples().begin();
					sample_iter != sampling->time_samples().end(); ++sample_iter)
			{
				if (sample_iter->valid_time()->get_time_position().is_real())
				{
					minimum_time = std::min(minimum_time, sample_time(**sample_iter));
					maximum_time = std::max(maximum_time, sample_time(**sample_iter));
				}
			}
			if (maximum_time < minimum_time)
			{
				continue;
			}
			RotationSequence sequence = {
				feature,
				*finder.irregular_sampling_property_iterator(),
				sampling,
				*finder.moving_ref_frame_plate_id(),
				*finder.fixed_ref_frame_plate_id(),
				minimum_time,
				maximum_time
			};
			sequences.push_back(sequence);
		}
		return sequences;
	}

	const RotationSequence *find_younger_sequence(
			const sequence_seq_type &sequences,
			GPlatesModel::integer_plate_id_type plate_id,
			double current_time)
	{
		const RotationSequence *best = NULL;
		for (sequence_seq_type::const_iterator sequence_iter = sequences.begin();
				sequence_iter != sequences.end(); ++sequence_iter)
		{
			if (sequence_iter->moving_plate != plate_id ||
					sequence_iter->minimum_time > current_time + 1e-9 ||
					sequence_iter->maximum_time < current_time - 1e-9)
			{
				continue;
			}
			if (!best || sequence_iter->minimum_time < best->minimum_time)
			{
				best = &*sequence_iter;
			}
		}
		return best;
	}

	boost::optional<double> find_previous_history_time(
			const sequence_seq_type &sequences,
			GPlatesModel::integer_plate_id_type plate_id,
			double current_time)
	{
		double previous_time = 1e100;
		for (sequence_seq_type::const_iterator sequence_iter = sequences.begin();
				sequence_iter != sequences.end(); ++sequence_iter)
		{
			if (sequence_iter->moving_plate != plate_id)
			{
				continue;
			}
			for (GPlatesModel::RevisionedVector<GPlatesPropertyValues::GpmlTimeSample>::const_iterator
					sample_iter = sequence_iter->sampling->time_samples().begin();
					sample_iter != sequence_iter->sampling->time_samples().end(); ++sample_iter)
			{
				if (!sample_iter->valid_time()->get_time_position().is_real())
				{
					continue;
				}
				const double time = sample_time(**sample_iter);
				if (time > current_time + 1e-9 && time < previous_time)
				{
					previous_time = time;
				}
			}
		}
		return previous_time < 1e90 ? boost::optional<double>(previous_time) : boost::none;
	}

	void gather_plate_geometry(
			GPlatesAppLogic::ApplicationState &application_state,
			std::map<GPlatesModel::integer_plate_id_type, PlateGeometry> &plates)
	{
		static const GPlatesModel::FeatureType CONTINENTAL_CRUST =
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust");
		static const GPlatesModel::FeatureType MID_OCEAN_RIDGE =
				GPlatesModel::FeatureType::create_gpml("MidOceanRidge");
		static const GPlatesModel::FeatureType SUBDUCTION_ZONE =
				GPlatesModel::FeatureType::create_gpml("SubductionZone");
		std::set<const GPlatesModel::TopLevelProperty *> seen;
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
		application_state.get_current_reconstruction().get_active_layer_outputs<
				GPlatesAppLogic::ReconstructLayerProxy>(layers);
		for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
				layer_iter = layers.begin(); layer_iter != layers.end(); ++layer_iter)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			(*layer_iter)->get_reconstructed_feature_geometries(geometries);
			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
			{
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
				if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
						!rfg.get_feature_ref().is_valid() ||
						!seen.insert((*rfg.property()).get()).second)
				{
					continue;
				}
				const GPlatesModel::FeatureType type = rfg.get_feature_ref()->feature_type();
				if (type == CONTINENTAL_CRUST && rfg.reconstruction_plate_id())
				{
					const GPlatesMaths::PolygonOnSphere *polygon =
							dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(rfg.reconstructed_geometry().get());
					if (polygon)
					{
						PlateGeometry &plate = plates[*rfg.reconstruction_plate_id()];
						const double area = polygon->get_area().dval();
						if (!plate.centre || area > plate.largest_continent_area)
						{
							plate.centre = GPlatesMaths::PointOnSphere(polygon->get_interior_centroid());
							plate.largest_continent_area = area;
						}
					}
				}
				else if (type == MID_OCEAN_RIDGE)
				{
					const GPlatesMaths::PolylineOnSphere *polyline =
							dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(rfg.reconstructed_geometry().get());
					const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> left =
							GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
									rfg.get_feature_ref(), GPlatesModel::PropertyName::create_gpml("leftPlate"));
					const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> right =
							GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
									rfg.get_feature_ref(), GPlatesModel::PropertyName::create_gpml("rightPlate"));
					if (polyline && left && right)
					{
						const GPlatesModel::integer_plate_id_type left_id = (*left)->get_value();
						const GPlatesModel::integer_plate_id_type right_id = (*right)->get_value();
						const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type ridge =
								polyline->get_non_null_pointer();
						plates[left_id].ridges.push_back(ridge);
						plates[right_id].ridges.push_back(ridge);
					}
				}
				else if (type == SUBDUCTION_ZONE && rfg.reconstruction_plate_id())
				{
					const GPlatesMaths::PolylineOnSphere *polyline =
							dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(rfg.reconstructed_geometry().get());
					if (polyline)
					{
						plates[*rfg.reconstruction_plate_id()].subduction_zones.push_back(
								polyline->get_non_null_pointer());
					}
				}
			}
		}
	}

	class AdvanceMotionUndoCommand : public QUndoCommand
	{
	public:
		AdvanceMotionUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const rotation_change_seq_type &rotation_changes) :
			d_model_interface(model_interface),
			d_rotation_changes(rotation_changes)
		{
			setText(QObject::tr("advance Worldbuilding Pasta plate motion"));
		}

		virtual void redo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (rotation_change_seq_type::iterator change_iter = d_rotation_changes.begin();
					change_iter != d_rotation_changes.end(); ++change_iter)
			{
				if (change_iter->feature.is_valid() && change_iter->property.is_still_valid())
				{
					change_iter->feature->set(change_iter->property, change_iter->after->clone());
				}
			}
			guard.release_guard();
		}

		virtual void undo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (rotation_change_seq_type::iterator change_iter = d_rotation_changes.begin();
					change_iter != d_rotation_changes.end(); ++change_iter)
			{
				if (change_iter->feature.is_valid() && change_iter->property.is_still_valid())
				{
					change_iter->feature->set(change_iter->property, change_iter->before->clone());
				}
			}
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		rotation_change_seq_type d_rotation_changes;
	};

}


GPlatesViewOperations::AdvancePlateMotionOperation::AdvancePlateMotionOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface())
{ }


GPlatesViewOperations::AdvancePlateMotionOperation::Result
GPlatesViewOperations::AdvancePlateMotionOperation::trigger(QWidget *parent)
{
	try
	{
		const double current_time =
				d_application_state.get_current_reconstruction().get_reconstruction_time();
		if (current_time <= PRESENT_DAY_DRIFT_LOCK_TIME_MA + TIME_EPSILON)
		{
			return Result(OPERATION_ERROR, QObject::tr(
					"The reconstruction is already at the 1 Ma effective-present drift lock. "
					"The 0 Ma rotation sample remains identity to avoid present-day drift correction."));
		}

		GPlatesAppLogic::FeatureCollectionFileState &file_state =
				d_application_state.get_feature_collection_file_state();
		const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded_files =
				file_state.get_loaded_files();
		std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> rotation_files;
		for (size_t file_index = 0; file_index < loaded_files.size(); ++file_index)
		{
			const QString suffix = loaded_files[file_index].get_file().get_file_info()
					.get_qfileinfo().suffix().toLower();
			if (suffix == "rot" || suffix == "rot2" || suffix == "grot" ||
					!collect_sequences(loaded_files[file_index].get_file().get_feature_collection()).empty())
			{
				rotation_files.push_back(loaded_files[file_index]);
			}
		}
		if (rotation_files.empty())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Load the editable .rot file that owns the continents' rotation history first."));
		}

		QDialog dialog(parent);
		dialog.setWindowTitle(QObject::tr("Worldbuilding Pasta - Advance Plate Motion"));
		dialog.setModal(true);
		QVBoxLayout *layout = new QVBoxLayout(&dialog);
		QLabel *intro = new QLabel(QObject::tr(
				"Propose the next younger rotation pole for every visible ContinentalCrust plate. "
				"The previous .rot stage supplies inertia; normalized MOR normals supply ridge push; "
				"normalized trench normals supply slab pull. The result is a starting point for editing. "
				"Each turn also copies its final pole to 1 Ma as a present-day drift lock while preserving "
				"the required identity pole at 0 Ma (the safe workaround for GPlates issue #23)."), &dialog);
		intro->setWordWrap(true);
		layout->addWidget(intro);
		QFormLayout *form = new QFormLayout;
		QComboBox *file_combo = new QComboBox(&dialog);
		for (size_t file_index = 0; file_index < rotation_files.size(); ++file_index)
		{
			QString name = rotation_files[file_index].get_file().get_file_info().get_display_name(false);
			if (name.isEmpty())
			{
				name = QObject::tr("Untitled rotation collection %1").arg(file_index + 1);
			}
			file_combo->addItem(name, static_cast<int>(file_index));
		}
		QDoubleSpinBox *interval_spin = new QDoubleSpinBox(&dialog);
		interval_spin->setRange(1.0, 100.0);
		interval_spin->setDecimals(1);
		interval_spin->setMaximum(std::min(100.0, current_time - PRESENT_DAY_DRIFT_LOCK_TIME_MA));
		interval_spin->setValue(std::min(50.0, current_time - PRESENT_DAY_DRIFT_LOCK_TIME_MA));
		interval_spin->setSuffix(QObject::tr(" Ma"));
		QDoubleSpinBox *default_speed_spin = new QDoubleSpinBox(&dialog);
		default_speed_spin->setRange(0.5, 10.0);
		default_speed_spin->setDecimals(1);
		default_speed_spin->setValue(4.0);
		default_speed_spin->setSuffix(QObject::tr(" cm/yr"));
		QDoubleSpinBox *history_weight_spin = new QDoubleSpinBox(&dialog);
		history_weight_spin->setRange(0.0, 5.0);
		history_weight_spin->setValue(1.5);
		QDoubleSpinBox *push_weight_spin = new QDoubleSpinBox(&dialog);
		push_weight_spin->setRange(0.0, 5.0);
		push_weight_spin->setValue(1.0);
		QDoubleSpinBox *pull_weight_spin = new QDoubleSpinBox(&dialog);
		pull_weight_spin->setRange(0.0, 5.0);
		pull_weight_spin->setValue(2.0);
		form->addRow(QObject::tr("Rotation collection:"), file_combo);
		form->addRow(QObject::tr("Turn length:"), interval_spin);
		form->addRow(QObject::tr("First-turn speed:"), default_speed_spin);
		form->addRow(QObject::tr("Previous-stage weight:"), history_weight_spin);
		form->addRow(QObject::tr("MOR push weight:"), push_weight_spin);
		form->addRow(QObject::tr("Subduction pull weight:"), pull_weight_spin);
		layout->addLayout(form);
		QDialogButtonBox *buttons = new QDialogButtonBox(
				QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
		buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Build Proposal"));
		QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
		QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
		layout->addWidget(buttons);
		if (dialog.exec() != QDialog::Accepted)
		{
			return Result(OPERATION_CANCELLED, QObject::tr("Plate-motion proposal cancelled; no data changed."));
		}

		const double interval_ma = std::min(
				interval_spin->value(), current_time - PRESENT_DAY_DRIFT_LOCK_TIME_MA);
		const double target_time = current_time - interval_ma;
		MotionGeometry::Parameters parameters;
		parameters.default_speed_cm_per_year = default_speed_spin->value();
		parameters.history_direction_weight = history_weight_spin->value();
		parameters.ridge_push_weight = push_weight_spin->value();
		parameters.slab_pull_weight = pull_weight_spin->value();

		const size_t selected_file_index = static_cast<size_t>(file_combo->currentData().toInt());
		const GPlatesModel::FeatureCollectionHandle::weak_ref rotation_collection =
				rotation_files[selected_file_index].get_file().get_feature_collection();
		const sequence_seq_type sequences = collect_sequences(rotation_collection);
		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction()
						.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type current_tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type target_tree =
				tree_creator.get_reconstruction_tree(target_time);
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type drift_lock_tree =
				tree_creator.get_reconstruction_tree(PRESENT_DAY_DRIFT_LOCK_TIME_MA);

		std::map<GPlatesModel::integer_plate_id_type, PlateGeometry> plates;
		gather_plate_geometry(d_application_state, plates);
		rotation_change_seq_type changes;
		proposal_map_type proposals;
		QStringList skipped;
		for (std::map<GPlatesModel::integer_plate_id_type, PlateGeometry>::const_iterator
				plate_iter = plates.begin(); plate_iter != plates.end(); ++plate_iter)
		{
			const GPlatesModel::integer_plate_id_type plate_id = plate_iter->first;
			if (!plate_iter->second.centre)
			{
				continue;
			}
			const RotationSequence *sequence = find_younger_sequence(sequences, plate_id, current_time);
			if (!sequence)
			{
				skipped << QObject::tr("Plate %1 (no active sequence in selected .rot)").arg(plate_id);
				continue;
			}
			const boost::optional<GPlatesMaths::FiniteRotation> current_absolute =
					current_tree->get_composed_absolute_rotation_or_none(plate_id);
			if (!current_absolute)
			{
				skipped << QObject::tr("Plate %1 (incomplete plate circuit)").arg(plate_id);
				continue;
			}

			boost::optional<GPlatesMaths::PointOnSphere> previous_centre;
			double previous_interval = 0;
			const boost::optional<double> previous_time =
					find_previous_history_time(sequences, plate_id, current_time);
			if (previous_time)
			{
				const boost::optional<GPlatesMaths::FiniteRotation> previous_absolute =
						tree_creator.get_reconstruction_tree(*previous_time)
								->get_composed_absolute_rotation_or_none(plate_id);
				if (previous_absolute)
				{
					previous_centre = GPlatesMaths::compose(
							*previous_absolute, GPlatesMaths::get_reverse(*current_absolute)) *
							*plate_iter->second.centre;
					previous_interval = *previous_time - current_time;
				}
			}

			if (!previous_centre && plate_iter->second.ridges.empty() &&
					plate_iter->second.subduction_zones.empty())
			{
				skipped << QObject::tr("Plate %1 (no previous stage, MOR or trench signal)").arg(plate_id);
				continue;
			}
			const MotionGeometry::Proposal geometry = MotionGeometry::generate_motion(
					*plate_iter->second.centre, previous_centre, previous_interval,
					plate_iter->second.ridges, plate_iter->second.subduction_zones,
					interval_ma, parameters);
			const GPlatesMaths::FiniteRotation target_absolute =
					GPlatesMaths::compose(geometry.motion_rotation, *current_absolute);
			proposals.insert(std::make_pair(
					plate_id, PlateProposal(
							plate_id, geometry, target_absolute, sequence)));
		}
		if (proposals.empty())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("No visible continental plate could be matched to an editable rotation sequence. %1")
							.arg(skipped.join("; ")));
		}

		// Convert each desired absolute younger position back into the selected
		// sequence's relative frame. If its fixed plate is also moving in this
		// turn, use that parent's proposed target position so the complete circuit
		// remains coherent after all samples are installed simultaneously.
		for (proposal_map_type::const_iterator proposal_iter = proposals.begin();
				proposal_iter != proposals.end(); ++proposal_iter)
		{
			const RotationSequence &sequence = *proposal_iter->second.sequence;
			boost::optional<GPlatesMaths::FiniteRotation> fixed_target_absolute;
			const proposal_map_type::const_iterator proposed_parent =
					proposals.find(sequence.fixed_plate);
			if (proposed_parent != proposals.end())
			{
				fixed_target_absolute = proposed_parent->second.target_absolute;
			}
			else
			{
				const boost::optional<GPlatesMaths::FiniteRotation> existing_fixed_target =
						target_tree->get_composed_absolute_rotation_or_none(sequence.fixed_plate);
				if (!existing_fixed_target)
				{
					throw std::runtime_error(QObject::tr(
							"Plate %1's fixed plate %2 is absent at %3 Ma.")
								.arg(proposal_iter->first).arg(sequence.fixed_plate)
								.arg(target_time, 0, 'f', 1).toStdString());
				}
				fixed_target_absolute = *existing_fixed_target;
			}
			const GPlatesMaths::FiniteRotation target_relative = GPlatesMaths::compose(
					GPlatesMaths::get_reverse(*fixed_target_absolute),
					proposal_iter->second.target_absolute);
			boost::optional<GPlatesMaths::FiniteRotation> fixed_drift_lock_absolute;
			if (proposed_parent != proposals.end())
			{
				fixed_drift_lock_absolute = proposed_parent->second.target_absolute;
			}
			else
			{
				fixed_drift_lock_absolute =
						drift_lock_tree->get_composed_absolute_rotation_or_none(sequence.fixed_plate);
				if (!fixed_drift_lock_absolute)
				{
					throw std::runtime_error(QObject::tr(
							"Plate %1's fixed plate %2 is absent at the 1 Ma drift lock.")
								.arg(proposal_iter->first).arg(sequence.fixed_plate).toStdString());
				}
			}
			const GPlatesMaths::FiniteRotation drift_lock_relative = GPlatesMaths::compose(
					GPlatesMaths::get_reverse(*fixed_drift_lock_absolute),
					proposal_iter->second.target_absolute);
			sample_seq_type samples;
			for (GPlatesModel::RevisionedVector<GPlatesPropertyValues::GpmlTimeSample>::const_iterator
					sample_iter = sequence.sampling->time_samples().begin();
					sample_iter != sequence.sampling->time_samples().end(); ++sample_iter)
			{
				if (!sample_iter->valid_time()->get_time_position().is_real())
				{
					samples.push_back(sample_iter->clone());
					continue;
				}
				const double time = sample_time(**sample_iter);
				if (std::fabs(time - target_time) > TIME_EPSILON &&
						std::fabs(time - PRESENT_DAY_DRIFT_LOCK_TIME_MA) > TIME_EPSILON &&
						std::fabs(time) > TIME_EPSILON)
				{
					samples.push_back(sample_iter->clone());
				}
			}
			const QString proposal_description = QObject::tr(
					"Worldbuilding Pasta: proposed Plate %1 motion from %2 Ma")
						.arg(proposal_iter->first).arg(current_time, 0, 'f', 1);
			const QString drift_lock_description = QObject::tr(
					"Worldbuilding Pasta: Plate %1 effective-present drift lock; 0 Ma stays identity")
						.arg(proposal_iter->first);
			if (std::fabs(target_time - PRESENT_DAY_DRIFT_LOCK_TIME_MA) <= TIME_EPSILON)
			{
				samples.push_back(create_rotation_sample(
						PRESENT_DAY_DRIFT_LOCK_TIME_MA, drift_lock_relative, drift_lock_description));
			}
			else
			{
				samples.push_back(create_rotation_sample(
						target_time, target_relative, proposal_description));
				samples.push_back(create_rotation_sample(
						PRESENT_DAY_DRIFT_LOCK_TIME_MA, drift_lock_relative, drift_lock_description));
			}
			samples.push_back(create_rotation_sample(
					0.0, GPlatesMaths::FiniteRotation::create_identity_rotation(),
					QObject::tr("Worldbuilding Pasta: required 0 Ma identity pole")));
			std::sort(samples.begin(), samples.end(), sample_less_than);
			RotationChange change = {
				sequence.feature,
				sequence.sampling_property,
				(*sequence.sampling_property)->clone(),
				create_sampling_property(samples)
			};
			changes.push_back(change);
		}

		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_view_state.get_rendered_geometry_collection().create_child_rendered_layer_and_transfer_ownership(
						RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		for (proposal_map_type::const_iterator proposal_iter = proposals.begin();
				proposal_iter != proposals.end(); ++proposal_iter)
		{
			preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_arrowed_polyline(
							proposal_iter->second.geometry.motion_path,
							GPlatesGui::Colour::get_green(), 12.0f, 4.0f));
		}
		QDialog confirmation(parent);
		confirmation.setWindowTitle(QObject::tr("Review Plate-Motion Proposal"));
		QVBoxLayout *confirmation_layout = new QVBoxLayout(&confirmation);
		QLabel *summary = new QLabel(QObject::tr(
				"Green arrows show the proposed %1-%2 Ma plate motion. The target-time rotation samples "
				"remain editable after creation. The proposed final poles will also be written at 1 Ma, "
				"with identity at 0 Ma, so the simulation holds its final positions without triggering "
				"present-day drift correction.")
					.arg(current_time, 0, 'f', 1).arg(target_time, 0, 'f', 1), &confirmation);
		summary->setWordWrap(true);
		confirmation_layout->addWidget(summary);
		QTableWidget *table = new QTableWidget(static_cast<int>(proposals.size()), 5, &confirmation);
		table->setHorizontalHeaderLabels(QStringList()
				<< QObject::tr("Plate") << QObject::tr("Speed") << QObject::tr("History")
				<< QObject::tr("MORs") << QObject::tr("Trenches"));
		int row = 0;
		for (proposal_map_type::const_iterator proposal_iter = proposals.begin();
				proposal_iter != proposals.end(); ++proposal_iter, ++row)
		{
			table->setItem(row, 0, new QTableWidgetItem(QString::number(proposal_iter->first)));
			table->setItem(row, 1, new QTableWidgetItem(
					QObject::tr("%1 cm/yr").arg(proposal_iter->second.geometry.speed_cm_per_year, 0, 'f', 1)));
			table->setItem(row, 2, new QTableWidgetItem(
					proposal_iter->second.geometry.used_history ? QObject::tr("yes") : QObject::tr("first turn")));
			table->setItem(row, 3, new QTableWidgetItem(QString::number(proposal_iter->second.geometry.ridge_count)));
			table->setItem(row, 4, new QTableWidgetItem(QString::number(proposal_iter->second.geometry.subduction_count)));
		}
		table->resizeColumnsToContents();
		confirmation_layout->addWidget(table);
		if (!skipped.isEmpty())
		{
			QLabel *skipped_summary = new QLabel(
					QObject::tr("Skipped: %1").arg(skipped.join("; ")), &confirmation);
			skipped_summary->setWordWrap(true);
			confirmation_layout->addWidget(skipped_summary);
		}
		QDialogButtonBox *confirm_buttons = new QDialogButtonBox(
				QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &confirmation);
		confirm_buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Write Rotation Poles"));
		QObject::connect(confirm_buttons, SIGNAL(accepted()), &confirmation, SLOT(accept()));
		QObject::connect(confirm_buttons, SIGNAL(rejected()), &confirmation, SLOT(reject()));
		confirmation_layout->addWidget(confirm_buttons);
		confirmation.resize(760, 460);
		if (confirmation.exec() != QDialog::Accepted)
		{
			preview_layer->clear_rendered_geometries();
			return Result(OPERATION_CANCELLED,
					QObject::tr("Plate-motion preview rejected; no rotation data changed."));
		}
		preview_layer->clear_rendered_geometries();

		std::unique_ptr<QUndoCommand> command(new AdvanceMotionUndoCommand(
				d_model_interface, changes));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		d_application_state.set_reconstruction_time(target_time);

		return Result(OPERATION_COMPLETED,
				QObject::tr("Advanced %1 plate(s) from %2 Ma to %3 Ma. The selected rotation collection "
						"now includes target-time samples, the "
						"1 Ma effective-present drift lock and a protected identity pole at 0 Ma.")
						.arg(proposals.size()).arg(current_time, 0, 'f', 1).arg(target_time, 0, 'f', 1));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not build the plate-motion turn: %1").arg(exception.what()));
	}
}
