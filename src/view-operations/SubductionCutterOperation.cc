/* $Id$ */

/**
 * \file
 * World Building "Subduction Cutter" operation.
 */

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <boost/optional.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QUndoCommand>
#include <QVariant>
#include <QVBoxLayout>

#include "SubductionCutterOperation.h"

#include "SubductionCutterGeometry.h"
#include "RenderedGeometryCollection.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/PartitionFeatureUtils.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/Reconstruction.h"

#include "feature-visitors/GeometrySetter.h"

#include "gui/FeatureFocus.h"
#include "gui/AnimationController.h"

#include "maths/PolygonOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimePeriod.h"


namespace
{
	namespace CutterGeometry = GPlatesViewOperations::SubductionCutterGeometry;
	typedef CutterGeometry::polygon_ptr_type polygon_ptr_type;
	typedef CutterGeometry::polygon_seq_type polygon_seq_type;
	typedef std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>
			reconstruct_layer_seq_type;
	typedef std::vector<GPlatesModel::TopLevelProperty::non_null_ptr_type> property_seq_type;

	struct PlateChoice
	{
		GPlatesModel::integer_plate_id_type plate_id;
		unsigned int polygon_count;
	};

	bool
	is_oceanic_crust(
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg)
	{
		return rfg.get_feature_ref().is_valid() &&
				rfg.get_feature_ref()->feature_type() ==
						GPlatesModel::FeatureType::create_gpml("OceanicCrust");
	}

	struct TimedPiece
	{
		TimedPiece(
				const polygon_ptr_type &polygon_,
				boost::optional<double> disappearance_time_ = boost::none) :
			polygon(polygon_),
			disappearance_time(disappearance_time_)
		{  }

		polygon_ptr_type polygon;
		boost::optional<double> disappearance_time;
	};

	struct TrackedSource
	{
		TrackedSource(
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg,
				unsigned int geometry_property_index_,
				const polygon_ptr_type &present_day_polygon) :
			feature(rfg.get_feature_ref()),
			property(rfg.property()),
			geometry_property_index(geometry_property_index_),
			changed(false)
		{
			remaining.push_back(present_day_polygon);
		}

		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator property;
		unsigned int geometry_property_index;
		polygon_seq_type remaining;
		std::vector<TimedPiece> subducted;
		bool changed;
	};

	typedef std::vector<TrackedSource> tracked_source_seq_type;

	void
	get_visible_reconstruct_layers(
			reconstruct_layer_seq_type &outputs,
			const GPlatesPresentation::ViewState &view_state)
	{
		const GPlatesPresentation::VisualLayers &visual_layers = view_state.get_visual_layers();
		for (size_t layer_index = 0; layer_index < visual_layers.size(); ++layer_index)
		{
			const boost::shared_ptr<const GPlatesPresentation::VisualLayer> visual_layer =
					visual_layers.visual_layer_at(layer_index).lock();
			if (!visual_layer || !visual_layer->is_visible() ||
					visual_layer->get_layer_type() !=
							static_cast<unsigned int>(GPlatesAppLogic::LayerTaskType::RECONSTRUCT))
			{
				continue;
			}
			const boost::optional<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> output =
					visual_layer->get_reconstruct_graph_layer().get_layer_output<
							GPlatesAppLogic::ReconstructLayerProxy>();
			if (output)
			{
				outputs.push_back(*output);
			}
		}
	}

	void
	get_reconstructed_feature_geometries(
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> &geometries,
			const reconstruct_layer_seq_type &layers)
	{
		for (reconstruct_layer_seq_type::const_iterator layer_iter = layers.begin();
				layer_iter != layers.end(); ++layer_iter)
		{
			(*layer_iter)->get_reconstructed_feature_geometries(geometries);
		}
	}

	std::vector<PlateChoice>
	get_plate_choices(
			const reconstruct_layer_seq_type &layers,
			bool oceanic_crust_only)
	{
		std::map<GPlatesModel::integer_plate_id_type, unsigned int> counts;
		std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
		get_reconstructed_feature_geometries(geometries, layers);
		for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
				geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
		{
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
			if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
					!rfg.reconstruction_plate_id() ||
					!dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(rfg.reconstructed_geometry().get()) ||
					(oceanic_crust_only && !is_oceanic_crust(rfg)))
			{
				continue;
			}
			const GPlatesModel::TopLevelProperty *property_ptr = (*rfg.property()).get();
			if (seen_properties.insert(property_ptr).second)
			{
				++counts[*rfg.reconstruction_plate_id()];
			}
		}

		std::vector<PlateChoice> choices;
		for (std::map<GPlatesModel::integer_plate_id_type, unsigned int>::const_iterator
				count_iter = counts.begin(); count_iter != counts.end(); ++count_iter)
		{
			PlateChoice choice = { count_iter->first, count_iter->second };
			choices.push_back(choice);
		}
		return choices;
	}

	boost::optional<unsigned int>
	get_property_index(
			const GPlatesModel::FeatureHandle &feature,
			const GPlatesModel::FeatureHandle::iterator &property)
	{
		unsigned int property_index = 0;
		for (GPlatesModel::FeatureHandle::const_iterator property_iter = feature.begin();
				property_iter != feature.end(); ++property_iter, ++property_index)
		{
			if (property_iter == property)
			{
				return property_index;
			}
		}
		return boost::none;
	}

	polygon_ptr_type
	rotate_polygon(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg,
			bool reverse_reconstruct)
	{
		if (!rfg.reconstruction_plate_id())
		{
			return polygon_ptr_type(&polygon);
		}

		std::vector<GPlatesMaths::PointOnSphere> exterior;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
				polygon.exterior_ring_vertex_begin();
				point_iter != polygon.exterior_ring_vertex_end(); ++point_iter)
		{
			exterior.push_back(GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
					*point_iter, *rfg.reconstruction_plate_id(), *rfg.get_reconstruction_tree(),
					reverse_reconstruct));
		}

		std::vector< std::vector<GPlatesMaths::PointOnSphere> > interior_rings;
		for (unsigned int ring_index = 0; ring_index < polygon.number_of_interior_rings(); ++ring_index)
		{
			interior_rings.push_back(std::vector<GPlatesMaths::PointOnSphere>());
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
					polygon.interior_ring_vertex_begin(ring_index);
					point_iter != polygon.interior_ring_vertex_end(ring_index); ++point_iter)
			{
				interior_rings.back().push_back(
						GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
								*point_iter, *rfg.reconstruction_plate_id(),
								*rfg.get_reconstruction_tree(), reverse_reconstruct));
			}
		}

		return GPlatesMaths::PolygonOnSphere::create(
				exterior.begin(), exterior.end(),
				interior_rings.begin(), interior_rings.end(), true);
	}

	TrackedSource *
	find_tracked_source(
			tracked_source_seq_type &tracked_sources,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg)
	{
		for (tracked_source_seq_type::iterator source_iter = tracked_sources.begin();
				source_iter != tracked_sources.end(); ++source_iter)
		{
			if (source_iter->feature == rfg.get_feature_ref() && source_iter->property == rfg.property())
			{
				return &*source_iter;
			}
		}
		return NULL;
	}

	property_seq_type
	clone_properties(const GPlatesModel::FeatureHandle &feature)
	{
		property_seq_type result;
		for (GPlatesModel::FeatureHandle::const_iterator property_iter = feature.begin();
				property_iter != feature.end(); ++property_iter)
		{
			result.push_back((*property_iter)->clone());
		}
		return result;
	}

	property_seq_type
	create_piece_properties(
			const TrackedSource &source,
			const TimedPiece &piece)
	{
		property_seq_type result;
		const GPlatesModel::PropertyName valid_time_property_name =
				GPlatesModel::PropertyName::create_gml("validTime");
		unsigned int property_index = 0;
		for (GPlatesModel::FeatureHandle::const_iterator property_iter = source.feature->begin();
				property_iter != source.feature->end(); ++property_iter, ++property_index)
		{
			if (piece.disappearance_time &&
					(*property_iter)->get_property_name() == valid_time_property_name)
			{
				continue;
			}

			GPlatesModel::TopLevelProperty::non_null_ptr_type property_clone =
					(*property_iter)->clone();
			if (property_index == source.geometry_property_index)
			{
				GPlatesFeatureVisitors::GeometrySetter geometry_setter(piece.polygon);
				geometry_setter.set_geometry(property_clone.get());
			}
			result.push_back(property_clone);
		}

		if (piece.disappearance_time)
		{
			const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type>
					original_valid_time =
							GPlatesAppLogic::PartitionFeatureUtils::get_valid_time_from_feature(source.feature);
			const GPlatesPropertyValues::GeoTimeInstant begin_time = original_valid_time
					? (*original_valid_time)->begin()->get_time_position()
					: GPlatesPropertyValues::GeoTimeInstant::create_distant_past();
			const GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_type time_period =
					GPlatesModel::ModelUtils::create_gml_time_period(
							begin_time,
							GPlatesPropertyValues::GeoTimeInstant(*piece.disappearance_time));
			boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> valid_time_property =
					GPlatesModel::ModelUtils::create_top_level_property(
							valid_time_property_name, time_period);
			if (valid_time_property)
			{
				result.push_back(*valid_time_property);
			}
			else
			{
				result.push_back(GPlatesModel::TopLevelPropertyInline::create(
						valid_time_property_name, time_period));
			}
		}
		return result;
	}

	void
	set_properties(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const property_seq_type &properties)
	{
		while (feature->begin() != feature->end())
		{
			feature->remove(feature->begin());
		}
		for (property_seq_type::const_iterator property_iter = properties.begin();
				property_iter != properties.end(); ++property_iter)
		{
			feature->add((*property_iter)->clone());
		}
	}

	class SubductionCutterUndoCommand :
			public QUndoCommand
	{
	public:
		struct Edit
		{
			Edit(
					const GPlatesModel::FeatureHandle::weak_ref &feature_,
					const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_) :
				feature(feature_),
				collection(collection_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesModel::FeatureCollectionHandle::weak_ref collection;
			property_seq_type before_properties;
			property_seq_type after_properties;
			std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> clones;
		};

		SubductionCutterUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const tracked_source_seq_type &sources,
				GPlatesModel::integer_plate_id_type overriding_plate_id,
				GPlatesModel::integer_plate_id_type subducting_plate_id,
				unsigned int checks) :
			QUndoCommand(0),
			d_model_interface(model_interface)
		{
			for (tracked_source_seq_type::const_iterator source_iter = sources.begin();
					source_iter != sources.end(); ++source_iter)
			{
				if (!source_iter->changed || !source_iter->feature.is_valid() ||
						!source_iter->feature->parent_ptr())
				{
					continue;
				}

				std::vector<TimedPiece> pieces(source_iter->subducted);
				for (polygon_seq_type::const_iterator remaining_iter = source_iter->remaining.begin();
						remaining_iter != source_iter->remaining.end(); ++remaining_iter)
				{
					pieces.push_back(TimedPiece(*remaining_iter));
				}
				if (pieces.empty())
				{
					continue;
				}

				unsigned int primary_index = 0;
				double primary_area = -1.0;
				bool primary_is_survivor = false;
				for (unsigned int piece_index = 0; piece_index < pieces.size(); ++piece_index)
				{
					const bool is_survivor = !pieces[piece_index].disappearance_time;
					const double area = pieces[piece_index].polygon->get_area().dval();
					if ((is_survivor && !primary_is_survivor) ||
							(is_survivor == primary_is_survivor && area > primary_area))
					{
						primary_index = piece_index;
						primary_area = area;
						primary_is_survivor = is_survivor;
					}
				}

				Edit edit(
						source_iter->feature,
						source_iter->feature->parent_ptr()->reference());
				edit.before_properties = clone_properties(*source_iter->feature);
				edit.after_properties = create_piece_properties(*source_iter, pieces[primary_index]);

				for (unsigned int piece_index = 0; piece_index < pieces.size(); ++piece_index)
				{
					if (piece_index == primary_index)
					{
						continue;
					}
					const GPlatesModel::FeatureHandle::non_null_ptr_type clone =
							GPlatesModel::FeatureHandle::create(source_iter->feature->feature_type());
					const property_seq_type clone_properties_for_piece =
							create_piece_properties(*source_iter, pieces[piece_index]);
					for (property_seq_type::const_iterator property_iter =
							clone_properties_for_piece.begin();
							property_iter != clone_properties_for_piece.end(); ++property_iter)
					{
						clone->add((*property_iter)->clone());
					}
					edit.clones.push_back(clone);
				}
				d_edits.push_back(edit);
			}

			setText(QObject::tr(
					"retire subducted OceanicCrust - overriding plate %1; subducting plate %2; %3 checks")
					.arg(static_cast<qulonglong>(overriding_plate_id))
					.arg(static_cast<qulonglong>(subducting_plate_id))
					.arg(checks));
		}

		virtual void redo() { apply(true); }
		virtual void undo() { apply(false); }

	private:
		void apply(bool use_after)
		{
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());
			for (std::vector<Edit>::iterator edit_iter = d_edits.begin();
					edit_iter != d_edits.end(); ++edit_iter)
			{
				if (!edit_iter->feature.is_valid())
				{
					continue;
				}
				if (use_after)
				{
					set_properties(edit_iter->feature, edit_iter->after_properties);
					if (edit_iter->collection.is_valid())
					{
						for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::const_iterator
								clone_iter = edit_iter->clones.begin();
								clone_iter != edit_iter->clones.end(); ++clone_iter)
						{
							if (!(*clone_iter)->parent_ptr())
							{
								edit_iter->collection->add(*clone_iter);
							}
						}
					}
				}
				else
				{
					for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::const_iterator
							clone_iter = edit_iter->clones.begin();
							clone_iter != edit_iter->clones.end(); ++clone_iter)
					{
						if ((*clone_iter)->parent_ptr())
						{
							(*clone_iter)->remove_from_parent();
						}
					}
					set_properties(edit_iter->feature, edit_iter->before_properties);
				}
			}
			notification_guard.release_guard();
		}

		GPlatesModel::ModelInterface d_model_interface;
		std::vector<Edit> d_edits;
	};
}


GPlatesViewOperations::SubductionCutterOperation::SubductionCutterOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface())
{  }


GPlatesViewOperations::SubductionCutterOperation::Result
GPlatesViewOperations::SubductionCutterOperation::trigger(
		QWidget *parent_widget)
{
	const double original_time = d_application_state.get_current_reconstruction_time();
	reconstruct_layer_seq_type visible_layers;
	get_visible_reconstruct_layers(visible_layers, d_view_state);
	if (visible_layers.empty())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("No visible reconstruct layers are available."));
	}

	const std::vector<PlateChoice> overriding_plate_choices =
			get_plate_choices(visible_layers, false);
	const std::vector<PlateChoice> oceanic_crust_plate_choices =
			get_plate_choices(visible_layers, true);
	if (oceanic_crust_plate_choices.empty())
	{
		return Result(OPERATION_ERROR,
				QObject::tr(
						"No visible gpml:OceanicCrust polygon is available at the current View time."));
	}
	if (overriding_plate_choices.size() < 2)
	{
		return Result(OPERATION_ERROR,
				QObject::tr(
						"At least two plate IDs with visible polygon geometry are required to retire subducted oceanic crust."));
	}

	QDialog dialog(parent_widget);
	dialog.setWindowTitle(QObject::tr("Retire Subducted Oceanic Crust"));
	dialog.setModal(true);
	QVBoxLayout *dialog_layout = new QVBoxLayout(&dialog);

	QLabel *description = new QLabel(QObject::tr(
			"Rewind from the current View time to an older bound, advance through equal detection checks, "
			"and split only OceanicCrust on the selected subducting plate where it first overlaps the "
			"overriding plate. Retired pieces receive a disappearance time and the complete edit is undoable."), &dialog);
	description->setWordWrap(true);
	description->setMinimumWidth(540);
	dialog_layout->addWidget(description);

	QGroupBox *range_group = new QGroupBox(QObject::tr("Time range"), &dialog);
	QFormLayout *range_form = new QFormLayout(range_group);
	QDoubleSpinBox *older_time = new QDoubleSpinBox(range_group);
	older_time->setRange(-1000.0, 10000.0);
	older_time->setDecimals(2);
	const boost::optional<double> project_older_bound =
			d_application_state.get_project_timestamp_schedule().default_older_bound(original_time);
	const double fallback_increment =
			d_view_state.get_animation_controller().time_increment();
	older_time->setValue(project_older_bound
			? *project_older_bound : original_time + fallback_increment);
	older_time->setSuffix(QObject::tr(" Ma"));
	range_form->addRow(QObject::tr("Older bound:"), older_time);

	QLabel *older_bound_source = new QLabel(range_group);
	older_bound_source->setWordWrap(true);
	older_bound_source->setText(project_older_bound
			? QObject::tr("Defaulted to the next older Project Timestamp (%1 Ma).")
					.arg(*project_older_bound, 0, 'f', 2)
			: QObject::tr("No usable Project Timeline; defaulted to the current animation increment (%1 My).")
					.arg(fallback_increment, 0, 'f', 2));
	range_form->addRow(QObject::tr("Default source:"), older_bound_source);

	QDoubleSpinBox *younger_time = new QDoubleSpinBox(range_group);
	younger_time->setRange(-1000.0, 10000.0);
	younger_time->setDecimals(2);
	younger_time->setValue(original_time);
	younger_time->setSuffix(QObject::tr(" Ma"));
	younger_time->setReadOnly(true);
	range_form->addRow(QObject::tr("Current View time:"), younger_time);

	QSpinBox *checks = new QSpinBox(range_group);
	checks->setRange(1, 1000);
	checks->setValue(5);
	range_form->addRow(QObject::tr("Number of checks:"), checks);

	QLabel *interval_label = new QLabel(range_group);
	range_form->addRow(QObject::tr("Step interval:"), interval_label);
	dialog_layout->addWidget(range_group);

	QGroupBox *plates_group = new QGroupBox(QObject::tr("Plate roles"), &dialog);
	QFormLayout *plates_form = new QFormLayout(plates_group);
	QComboBox *overriding_plate = new QComboBox(plates_group);
	QComboBox *subducting_plate = new QComboBox(plates_group);
	for (std::vector<PlateChoice>::const_iterator choice_iter = overriding_plate_choices.begin();
			choice_iter != overriding_plate_choices.end(); ++choice_iter)
	{
		const QString label = QObject::tr("Plate %1 (%2 polygon%3)")
				.arg(static_cast<qulonglong>(choice_iter->plate_id))
				.arg(choice_iter->polygon_count)
				.arg(choice_iter->polygon_count == 1 ? QString() : QObject::tr("s"));
		const QVariant plate_data = QVariant::fromValue(
				static_cast<qulonglong>(choice_iter->plate_id));
		overriding_plate->addItem(label, plate_data);
	}
	for (std::vector<PlateChoice>::const_iterator choice_iter = oceanic_crust_plate_choices.begin();
			choice_iter != oceanic_crust_plate_choices.end(); ++choice_iter)
	{
		const QString label = QObject::tr("Plate %1 (%2 OceanicCrust polygon%3)")
				.arg(static_cast<qulonglong>(choice_iter->plate_id))
				.arg(choice_iter->polygon_count)
				.arg(choice_iter->polygon_count == 1 ? QString() : QObject::tr("s"));
		subducting_plate->addItem(label, QVariant::fromValue(
				static_cast<qulonglong>(choice_iter->plate_id)));
	}
	for (int index = 0; index < overriding_plate->count(); ++index)
	{
		if (overriding_plate->itemData(index) != subducting_plate->currentData())
		{
			overriding_plate->setCurrentIndex(index);
			break;
		}
	}
	plates_form->addRow(QObject::tr("Overriding plate:"), overriding_plate);
	plates_form->addRow(QObject::tr("Subducting OceanicCrust plate:"), subducting_plate);
	dialog_layout->addWidget(plates_group);

	QLabel *validation_label = new QLabel(&dialog);
	validation_label->setWordWrap(true);
	dialog_layout->addWidget(validation_label);

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QPushButton *run_button = buttons->button(QDialogButtonBox::Ok);
	run_button->setText(QObject::tr("Preview Retirement"));
	dialog_layout->addWidget(buttons);

	auto update_validation = [&]()
	{
		const double span = older_time->value() - younger_time->value();
		interval_label->setText(QObject::tr("%1 My").arg(
				span / static_cast<double>(checks->value()), 0, 'f', 2));
		QString problem;
		if (span <= 0.0)
		{
			problem = QObject::tr("The older bound must be older (numerically greater) than the younger bound.");
		}
		else if (overriding_plate->currentData() == subducting_plate->currentData())
		{
			problem = QObject::tr("Choose different overriding and subducting plate IDs.");
		}
		validation_label->setText(problem.isEmpty()
				? QObject::tr("Only rigid gpml:OceanicCrust polygons in currently visible reconstruct layers are candidates.")
				: problem);
		run_button->setEnabled(problem.isEmpty());
	};

	QObject::connect(older_time,
			static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
			update_validation);
	QObject::connect(younger_time,
			static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
			update_validation);
	QObject::connect(checks,
			static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
			update_validation);
	QObject::connect(overriding_plate,
			static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
			update_validation);
	QObject::connect(subducting_plate,
			static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
			update_validation);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	update_validation();

	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(CUT_CANCELLED, QObject::tr("Oceanic-crust retirement cancelled; no data changed."));
	}

	const double selected_older_time = older_time->value();
	const double selected_younger_time = younger_time->value();
	const unsigned int selected_checks = static_cast<unsigned int>(checks->value());
	const double time_step =
			(selected_older_time - selected_younger_time) / static_cast<double>(selected_checks);
	const GPlatesModel::integer_plate_id_type overriding_plate_id =
			static_cast<GPlatesModel::integer_plate_id_type>(overriding_plate->currentData().toULongLong());
	const GPlatesModel::integer_plate_id_type subducting_plate_id =
			static_cast<GPlatesModel::integer_plate_id_type>(subducting_plate->currentData().toULongLong());

	QProgressDialog progress(
			QObject::tr("Rewinding to %1 Ma...").arg(selected_older_time, 0, 'f', 2),
			QObject::tr("Cancel"), 0, static_cast<int>(selected_checks), parent_widget);
	progress.setWindowTitle(QObject::tr("Retire Subducted Oceanic Crust"));
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(0);
	progress.setAutoClose(false);
	progress.setValue(0);
	QApplication::processEvents();

	tracked_source_seq_type tracked_sources;
	unsigned int cut_events = 0;
	unsigned int subducted_pieces = 0;
	unsigned int checks_without_overriding_polygon = 0;

	try
	{
		d_application_state.set_reconstruction_time(selected_older_time);
		for (unsigned int check_index = 0; check_index < selected_checks; ++check_index)
		{
			if (progress.wasCanceled())
			{
				d_application_state.set_reconstruction_time(original_time);
				return Result(CUT_CANCELLED,
						QObject::tr("Oceanic-crust retirement cancelled; no data changed."));
			}

			const double check_time = check_index + 1 == selected_checks
					? selected_younger_time
					: selected_older_time - static_cast<double>(check_index + 1) * time_step;
			progress.setLabelText(QObject::tr("Checking overlap at %1 Ma...").arg(check_time, 0, 'f', 2));
			d_application_state.set_reconstruction_time(check_time);
			QApplication::processEvents();

			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			get_reconstructed_feature_geometries(geometries, visible_layers);
			polygon_seq_type overriding_polygons;
			std::vector<const GPlatesAppLogic::ReconstructedFeatureGeometry *> subducting_geometries;
			std::set<const GPlatesModel::TopLevelProperty *> seen_overriding_properties;
			std::set<const GPlatesModel::TopLevelProperty *> seen_subducting_properties;

			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
			{
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
				if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
						!rfg.reconstruction_plate_id())
				{
					continue;
				}
				const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(rfg.reconstructed_geometry().get());
				if (!polygon)
				{
					continue;
				}

				const GPlatesModel::TopLevelProperty *property_ptr = (*rfg.property()).get();
				if (*rfg.reconstruction_plate_id() == overriding_plate_id &&
						seen_overriding_properties.insert(property_ptr).second)
				{
					overriding_polygons.push_back(polygon_ptr_type(polygon));
				}
				if (*rfg.reconstruction_plate_id() == subducting_plate_id &&
						is_oceanic_crust(rfg) &&
						seen_subducting_properties.insert(property_ptr).second)
				{
					subducting_geometries.push_back(&rfg);
				}
			}

			if (overriding_polygons.empty())
			{
				++checks_without_overriding_polygon;
			}
			else
			{
				for (std::vector<const GPlatesAppLogic::ReconstructedFeatureGeometry *>::const_iterator
						rfg_iter = subducting_geometries.begin();
						rfg_iter != subducting_geometries.end(); ++rfg_iter)
				{
					const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **rfg_iter;
					TrackedSource *source = find_tracked_source(tracked_sources, rfg);
					if (!source)
					{
						const boost::optional<unsigned int> property_index =
								get_property_index(*rfg.feature_handle_ptr(), rfg.property());
						if (!property_index || !rfg.feature_handle_ptr()->parent_ptr())
						{
							continue;
						}
						const GPlatesMaths::PolygonOnSphere *reconstructed_polygon =
								dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
									rfg.reconstructed_geometry().get());
						tracked_sources.push_back(TrackedSource(
								rfg, *property_index,
								rotate_polygon(*reconstructed_polygon, rfg, true)));
						source = &tracked_sources.back();
					}

					polygon_seq_type next_remaining;
					for (polygon_seq_type::const_iterator piece_iter = source->remaining.begin();
							piece_iter != source->remaining.end(); ++piece_iter)
					{
						const polygon_ptr_type reconstructed_piece =
								rotate_polygon(**piece_iter, rfg, false);
						const CutterGeometry::CutResult cut_result =
								CutterGeometry::cut_polygon(*reconstructed_piece, overriding_polygons);
						if (!cut_result.success)
						{
							d_application_state.set_reconstruction_time(original_time);
							return Result(OPERATION_ERROR,
									QObject::tr("Cut failed at %1 Ma for feature %2: %3")
											.arg(check_time, 0, 'f', 2)
											.arg(rfg.feature_handle_ptr()->feature_id().get().qstring())
											.arg(cut_result.error));
						}
						if (!cut_result.overlap)
						{
							next_remaining.push_back(*piece_iter);
							continue;
						}

						source->changed = true;
						++cut_events;
						for (polygon_seq_type::const_iterator inside_iter = cut_result.inside.begin();
								inside_iter != cut_result.inside.end(); ++inside_iter)
						{
							source->subducted.push_back(TimedPiece(
									rotate_polygon(**inside_iter, rfg, true), check_time + 0.01));
							++subducted_pieces;
						}
						for (polygon_seq_type::const_iterator outside_iter = cut_result.outside.begin();
								outside_iter != cut_result.outside.end(); ++outside_iter)
						{
							next_remaining.push_back(rotate_polygon(**outside_iter, rfg, true));
						}
					}
					source->remaining.swap(next_remaining);
				}
			}

			progress.setValue(static_cast<int>(check_index + 1));
			QApplication::processEvents();
		}
	}
	catch (const std::exception &exception)
	{
		d_application_state.set_reconstruction_time(original_time);
		return Result(OPERATION_ERROR,
				QObject::tr("Oceanic-crust retirement stopped without changing data: %1").arg(exception.what()));
	}
	catch (...)
	{
		d_application_state.set_reconstruction_time(original_time);
		return Result(OPERATION_ERROR,
				QObject::tr("Oceanic-crust retirement stopped on an unexpected geometry error; no data changed."));
	}

	progress.close();
	unsigned int changed_features = 0;
	for (tracked_source_seq_type::const_iterator source_iter = tracked_sources.begin();
			source_iter != tracked_sources.end(); ++source_iter)
	{
		if (source_iter->changed)
		{
			++changed_features;
		}
	}
	if (!changed_features)
	{
		d_application_state.set_reconstruction_time(original_time);
		return Result(OPERATION_ERROR,
				QObject::tr(
						"No OceanicCrust area from plate %1 overlapped plate %2 at the %3 requested checks.")
						.arg(static_cast<qulonglong>(subducting_plate_id))
						.arg(static_cast<qulonglong>(overriding_plate_id))
						.arg(selected_checks));
	}

	unsigned int surviving_pieces = 0;
	for (tracked_source_seq_type::const_iterator source_iter = tracked_sources.begin();
			source_iter != tracked_sources.end(); ++source_iter)
	{
		if (source_iter->changed)
		{
			surviving_pieces += static_cast<unsigned int>(source_iter->remaining.size());
		}
	}
	const QString preview_summary = QObject::tr(
			"Preview complete.\n\n"
			"OceanicCrust source features changed: %1\n"
			"First-overlap cut events: %2\n"
			"Surviving pieces: %3\n"
			"Pieces receiving disappearance times: %4\n"
			"Detection checks: %5\n\n"
			"The View has been restored to %6 Ma. No feature data has changed yet.")
				.arg(changed_features).arg(cut_events).arg(surviving_pieces)
				.arg(subducted_pieces).arg(selected_checks).arg(original_time, 0, 'f', 2);
	d_application_state.set_reconstruction_time(original_time);
	QMessageBox confirmation(
			QMessageBox::Question,
			QObject::tr("Apply Oceanic-Crust Retirement?"),
			preview_summary,
			QMessageBox::Ok | QMessageBox::Cancel,
			parent_widget);
	confirmation.button(QMessageBox::Ok)->setText(QObject::tr("Apply Retirement"));
	confirmation.setDefaultButton(QMessageBox::Cancel);
	if (confirmation.exec() != QMessageBox::Ok)
	{
		return Result(CUT_CANCELLED,
				QObject::tr("Oceanic-crust retirement cancelled after preview; no data changed."));
	}

	d_view_state.get_feature_focus().unset_focus();
	std::unique_ptr<QUndoCommand> command(new SubductionCutterUndoCommand(
			d_model_interface, tracked_sources, overriding_plate_id,
			subducting_plate_id, selected_checks));
	UndoRedo::instance().get_active_undo_stack().push(command.release());
	d_application_state.set_reconstruction_time(original_time);

	QString skipped_checks;
	if (checks_without_overriding_polygon)
	{
		skipped_checks = QObject::tr(" %1 check(s) had no overriding polygon at that time.")
				.arg(checks_without_overriding_polygon);
	}
	return Result(CUT_COMPLETED,
			QObject::tr(
					"Oceanic-crust retirement complete: %1 source feature(s), %2 overlap event(s), "
					"and %3 timed retired piece(s). View time was restored to %4 Ma.%5")
					.arg(changed_features).arg(cut_events).arg(subducted_pieces)
					.arg(selected_younger_time, 0, 'f', 2).arg(skipped_checks));
}
