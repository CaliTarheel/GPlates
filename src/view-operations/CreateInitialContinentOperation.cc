/* $Id$ */

/**
 * \file
 * Worldbuilding Pasta "Create Initial Continent" operation.
 */

#include "CreateInitialContinentOperation.h"

#include <algorithm>
#include <climits>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "InitialContinentGeometry.h"
#include "UndoRedo.h"
#include "WorldbuildingFeatureCollectionUtils.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructGraph.h"

#include "gui/DrawStyleManager.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "presentation/ReconstructVisualLayerParams.h"
#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	class InitialContinentDialog :
			public QDialog
	{
	public:
		InitialContinentDialog(
				QWidget *parent_widget) :
			QDialog(parent_widget),
			d_start_time(new QDoubleSpinBox(this)),
			d_craton_count(new QSpinBox(this)),
			d_packing(new QSlider(Qt::Horizontal, this)),
			d_packing_description(new QLabel(this)),
			d_surface_coverage(new QDoubleSpinBox(this)),
			d_coastline_swiggle(new QSlider(Qt::Horizontal, this)),
			d_coastline_swiggle_description(new QLabel(this)),
			d_random_seed(new QSpinBox(this)),
			d_maximum_segment_length(new QDoubleSpinBox(this))
		{
			setWindowTitle(tr("Worldbuilding Pasta - Create Initial Continent"));
			setModal(true);

			QVBoxLayout *main_layout = new QVBoxLayout(this);
			QLabel *introduction = new QLabel(
					tr("Generate a reproducible supercontinent and an Earth-like set of cratons directly on the sphere."),
					this);
			introduction->setWordWrap(true);
			main_layout->addWidget(introduction);

			QFormLayout *form = new QFormLayout;
			d_start_time->setRange(0.0, 4600.0);
			d_start_time->setDecimals(1);
			d_start_time->setSingleStep(50.0);
			d_start_time->setValue(1000.0);
			d_start_time->setSuffix(tr(" Ma"));
			d_start_time->setToolTip(tr("The reconstruction time at which the generated geometry is imported."));
			form->addRow(tr("Start time:"), d_start_time);

			d_craton_count->setRange(8, 12);
			d_craton_count->setValue(10);
			d_craton_count->setToolTip(
					tr("Worldbuilding Pasta recommends 8-12 cratons for an Earth-like world."));
			form->addRow(tr("Number of cratons:"), d_craton_count);

			d_packing->setRange(0, 100);
			d_packing->setValue(50);
			d_packing->setTickInterval(10);
			d_packing->setTickPosition(QSlider::TicksBelow);
			QWidget *packing_row = new QWidget(this);
			QVBoxLayout *packing_layout = new QVBoxLayout(packing_row);
			packing_layout->setContentsMargins(0, 0, 0, 0);
			packing_layout->addWidget(d_packing);
			packing_layout->addWidget(d_packing_description);
			form->addRow(tr("Craton packing:"), packing_row);

			d_surface_coverage->setRange(5.0, 45.0);
			d_surface_coverage->setDecimals(1);
			d_surface_coverage->setSingleStep(1.0);
			d_surface_coverage->setValue(25.0);
			d_surface_coverage->setSuffix(tr(" %"));
			d_surface_coverage->setToolTip(
					tr("Area of the continental-crust polygon as a percentage of the full planet."));
			form->addRow(tr("Planet surface coverage:"), d_surface_coverage);

			d_coastline_swiggle->setRange(0, 100);
			d_coastline_swiggle->setValue(55);
			d_coastline_swiggle->setTickInterval(10);
			d_coastline_swiggle->setTickPosition(QSlider::TicksBelow);
			QWidget *coastline_swiggle_row = new QWidget(this);
			QVBoxLayout *coastline_swiggle_layout = new QVBoxLayout(coastline_swiggle_row);
			coastline_swiggle_layout->setContentsMargins(0, 0, 0, 0);
			coastline_swiggle_layout->addWidget(d_coastline_swiggle);
			coastline_swiggle_layout->addWidget(d_coastline_swiggle_description);
			form->addRow(tr("Coastline swiggle:"), coastline_swiggle_row);

			QWidget *seed_row = new QWidget(this);
			QHBoxLayout *seed_layout = new QHBoxLayout(seed_row);
			seed_layout->setContentsMargins(0, 0, 0, 0);
			d_random_seed->setRange(0, INT_MAX);
			d_random_seed->setValue(static_cast<int>(std::random_device()() & INT_MAX));
			QPushButton *new_seed_button = new QPushButton(tr("New Seed"), seed_row);
			seed_layout->addWidget(d_random_seed, 1);
			seed_layout->addWidget(new_seed_button);
			form->addRow(tr("Random seed:"), seed_row);

			QGroupBox *advanced_group = new QGroupBox(tr("Advanced quality"), this);
			QFormLayout *advanced_form = new QFormLayout(advanced_group);
			d_maximum_segment_length->setRange(50.0, 950.0);
			d_maximum_segment_length->setDecimals(0);
			d_maximum_segment_length->setSingleStep(25.0);
			d_maximum_segment_length->setValue(180.0);
			d_maximum_segment_length->setSuffix(tr(" km"));
			d_maximum_segment_length->setToolTip(
					tr("Defaults to 180 km, over 5% finer than Artifexia's 196 km average continental segment; hard-capped at 950 km."));
			advanced_form->addRow(tr("Maximum boundary segment:"), d_maximum_segment_length);

			main_layout->addLayout(form);
			main_layout->addWidget(advanced_group);

			QDialogButtonBox *buttons = new QDialogButtonBox(
					QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
					Qt::Horizontal,
					this);
			buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
			main_layout->addWidget(buttons);

			QObject::connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
			QObject::connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
			QObject::connect(
					new_seed_button,
					&QPushButton::clicked,
					[this]()
					{
						d_random_seed->setValue(static_cast<int>(std::random_device()() & INT_MAX));
					});
			QObject::connect(
					d_packing,
					&QSlider::valueChanged,
					[this](int value) { update_packing_description(value); });
			QObject::connect(
					d_coastline_swiggle,
					&QSlider::valueChanged,
					[this](int value) { update_coastline_swiggle_description(value); });
			update_packing_description(d_packing->value());
			update_coastline_swiggle_description(d_coastline_swiggle->value());
			resize(520, sizeHint().height());
		}

		double start_time() const { return d_start_time->value(); }
		unsigned int craton_count() const { return static_cast<unsigned int>(d_craton_count->value()); }
		double packing() const { return 0.01 * d_packing->value(); }
		double surface_fraction() const { return 0.01 * d_surface_coverage->value(); }
		double coastline_swiggle() const { return 0.01 * d_coastline_swiggle->value(); }
		boost::uint32_t random_seed() const { return static_cast<boost::uint32_t>(d_random_seed->value()); }
		double maximum_segment_length_km() const { return d_maximum_segment_length->value(); }

	private:
		void
		update_packing_description(
				int value)
		{
			const double fill_percent = 100.0 * (0.18 + 0.16 * value / 100.0);
			QString description;
			if (value < 34)
			{
				description = tr("Loose");
			}
			else if (value > 66)
			{
				description = tr("Tight");
			}
			else
			{
				description = tr("Balanced");
			}
			d_packing_description->setText(
					tr("%1 - cratons occupy approximately %2% of the continent")
					.arg(description)
					.arg(fill_percent, 0, 'f', 1));
		}

		void
		update_coastline_swiggle_description(
				int value)
		{
			QString description;
			if (value < 25)
			{
				description = tr("Broad and smooth");
			}
			else if (value < 70)
			{
				description = tr("Natural variation");
			}
			else
			{
				description = tr("Wild and jagged");
			}
			d_coastline_swiggle_description->setText(
					tr("%1 - %2% multi-scale variation").arg(description).arg(value));
		}

		QDoubleSpinBox *d_start_time;
		QSpinBox *d_craton_count;
		QSlider *d_packing;
		QLabel *d_packing_description;
		QDoubleSpinBox *d_surface_coverage;
		QSlider *d_coastline_swiggle;
		QLabel *d_coastline_swiggle_description;
		QSpinBox *d_random_seed;
		QDoubleSpinBox *d_maximum_segment_length;
	};

	void
	add_required_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &property_name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &property_value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, property_name, property_value))
		{
			throw std::runtime_error(
					QString("Unable to add required GPML property '%1'.")
					.arg(property_name.get_name().qstring())
					.toStdString());
		}
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type
	create_polygon_feature(
			const GPlatesModel::FeatureType &feature_type,
			const QString &name,
			GPlatesModel::integer_plate_id_type plate_id,
			double start_time,
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(feature_type);
		const GPlatesModel::FeatureHandle::weak_ref feature_ref = feature->reference();

		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(start_time),
						GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));
		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
				GPlatesModel::ModelUtils::create_gml_time_instant(
						GPlatesPropertyValues::GeoTimeInstant(start_time)));
		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gpml("outlineOf"),
				GPlatesPropertyValues::GmlPolygon::create(polygon));

		return feature;
	}

	struct FeatureGroup
	{
		FeatureGroup(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> &features_) :
			collection(collection_),
			features(features_)
		{  }

		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> features;
		std::vector<GPlatesModel::FeatureCollectionHandle::iterator> iterators;
	};

	class CreateInitialContinentUndoCommand :
			public QUndoCommand
	{
	public:
		CreateInitialContinentUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const std::vector<FeatureGroup> &groups) :
			d_model_interface(model_interface),
			d_groups(groups)
		{
			setText(QObject::tr("create initial Worldbuilding Pasta continent"));
		}

		virtual void
		redo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::vector<FeatureGroup>::iterator group_iter = d_groups.begin();
				group_iter != d_groups.end(); ++group_iter)
			{
				if (!group_iter->collection.is_valid())
				{
					continue;
				}
				group_iter->iterators.clear();
				for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::iterator feature_iter =
						group_iter->features.begin();
					feature_iter != group_iter->features.end(); ++feature_iter)
				{
					const GPlatesModel::FeatureCollectionHandle::iterator collection_iter =
							group_iter->collection->add(*feature_iter);
					group_iter->iterators.push_back(collection_iter);
					*feature_iter = *collection_iter;
				}
			}
			guard.release_guard();
		}

		virtual void
		undo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::size_t group_reverse = d_groups.size(); group_reverse > 0; --group_reverse)
			{
				FeatureGroup &group = d_groups[group_reverse - 1];
				if (!group.collection.is_valid())
				{
					continue;
				}
				for (std::size_t feature_reverse = group.iterators.size(); feature_reverse > 0; --feature_reverse)
				{
					const std::size_t index = feature_reverse - 1;
					if (group.iterators[index].is_still_valid())
					{
						group.features[index] = group.collection->remove(group.iterators[index]);
					}
				}
				group.iterators.clear();
			}
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<FeatureGroup> d_groups;
	};

	boost::optional<GPlatesAppLogic::Layer>
	find_reconstruct_layer_for_collection(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		GPlatesAppLogic::ReconstructGraph &reconstruct_graph = application_state.get_reconstruct_graph();
		for (GPlatesAppLogic::ReconstructGraph::iterator layer_iter = reconstruct_graph.begin();
			layer_iter != reconstruct_graph.end(); ++layer_iter)
		{
			GPlatesAppLogic::Layer layer = *layer_iter;
			if (layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
			{
				continue;
			}

			const std::vector<GPlatesAppLogic::Layer::InputConnection> inputs =
					layer.get_channel_inputs(layer.get_main_input_feature_collection_channel());
			for (std::vector<GPlatesAppLogic::Layer::InputConnection>::const_iterator input_iter = inputs.begin();
				input_iter != inputs.end(); ++input_iter)
			{
				const boost::optional<GPlatesAppLogic::Layer::InputFile> input_file = input_iter->get_input_file();
				if (input_file && input_file->get_feature_collection() == collection)
				{
					return layer;
				}
			}
		}

		return boost::none;
	}

	void
	apply_artifexia_reconstruct_layer_defaults(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesPresentation::ViewState &view_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
			const QString &layer_name)
	{
		const boost::optional<GPlatesAppLogic::Layer> layer =
				find_reconstruct_layer_for_collection(application_state, collection);
		if (!layer)
		{
			throw std::runtime_error(
					QString("Unable to find the reconstruction layer for '%1'.")
					.arg(layer_name)
					.toStdString());
		}

		boost::shared_ptr<GPlatesPresentation::VisualLayer> visual_layer =
				view_state.get_visual_layers().get_visual_layer(*layer).lock();
		if (!visual_layer)
		{
			throw std::runtime_error(
					QString("Unable to find the visual layer for '%1'.")
					.arg(layer_name)
					.toStdString());
		}

		GPlatesPresentation::ReconstructVisualLayerParams *params =
				dynamic_cast<GPlatesPresentation::ReconstructVisualLayerParams *>(
						visual_layer->get_visual_layer_params().get());
		if (!params)
		{
			throw std::runtime_error(
					QString("The visual layer for '%1' is not a reconstruction layer.")
					.arg(layer_name)
					.toStdString());
		}

		// These are the exact vector-layer settings used by Artifexia's
		// 'continents' and 'cratons' layers. Its solid shading comes from two
		// additional raster layers, not from filled reconstruction polygons.
		visual_layer->set_custom_name(layer_name);
		visual_layer->set_visible(true);
		params->set_style_adapter(GPlatesGui::DrawStyleManager::instance()->default_style());
		params->set_fill_polygons(false);
		params->set_fill_polylines(false);
		params->set_fill_opacity(1.0);
		params->set_fill_intensity(1.0);
	}
}


GPlatesViewOperations::CreateInitialContinentOperation::CreateInitialContinentOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface())
{  }


GPlatesViewOperations::CreateInitialContinentOperation::Result
GPlatesViewOperations::CreateInitialContinentOperation::trigger(
		QWidget *parent_widget)
{
	InitialContinentDialog dialog(parent_widget);
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED, QObject::tr("Initial continent creation cancelled; no data changed."));
	}

	try
	{
		InitialContinentGeometry::Parameters parameters;
		parameters.craton_count = dialog.craton_count();
		parameters.packing = dialog.packing();
		parameters.continent_area_fraction = dialog.surface_fraction();
		parameters.coastline_swiggle = dialog.coastline_swiggle();
		parameters.random_seed = dialog.random_seed();
		parameters.maximum_segment_length_km = dialog.maximum_segment_length_km();

		const InitialContinentGeometry::Result generated =
				InitialContinentGeometry::generate(parameters);

		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> continent_features;
		continent_features.push_back(create_polygon_feature(
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust"),
				QObject::tr("Initial Supercontinent"),
				100,
				dialog.start_time(),
				generated.continent));

		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> craton_features;
		craton_features.reserve(generated.cratons.size());
		for (unsigned int index = 0; index < generated.cratons.size(); ++index)
		{
			const QString name = QObject::tr("Craton %1").arg(QChar('A' + index));
			craton_features.push_back(create_polygon_feature(
					GPlatesModel::FeatureType::create_gpml("Craton"),
					name,
					100 * (index + 1),
					dialog.start_time(),
					generated.cratons[index]));
		}

		GPlatesAppLogic::FeatureCollectionFileState::file_reference continent_file =
				create_named_empty_feature_collection(
						d_application_state.get_feature_collection_file_io(),
						QObject::tr("Continental Crust"));
		const GPlatesModel::FeatureCollectionHandle::weak_ref continent_collection =
				continent_file.get_file().get_feature_collection();
		GPlatesAppLogic::FeatureCollectionFileState::file_reference craton_file =
				create_named_empty_feature_collection(
						d_application_state.get_feature_collection_file_io(),
						QObject::tr("Cratons"));
		const GPlatesModel::FeatureCollectionHandle::weak_ref craton_collection =
				craton_file.get_file().get_feature_collection();

		std::vector<FeatureGroup> feature_groups;
		feature_groups.push_back(FeatureGroup(continent_collection, continent_features));
		feature_groups.push_back(FeatureGroup(craton_collection, craton_features));

		std::unique_ptr<QUndoCommand> command(new CreateInitialContinentUndoCommand(
				d_model_interface,
				feature_groups));
		UndoRedo::instance().get_active_undo_stack().push(command.release());

		apply_artifexia_reconstruct_layer_defaults(
				d_application_state,
				d_view_state,
				continent_collection,
				QObject::tr("Continental Crust"));
		apply_artifexia_reconstruct_layer_defaults(
				d_application_state,
				d_view_state,
				craton_collection,
				QObject::tr("Cratons"));

		d_application_state.set_reconstruction_time(dialog.start_time());

		return Result(
				OPERATION_COMPLETED,
				QObject::tr(
						"Created a Continental Crust collection with one continent and a separate "
						"Cratons collection with %1 cratons at %2 Ma. "
						"Coverage: %3%; craton fill: %4%; coastline swiggle: %5%; "
						"coastline segments average %6 km (maximum %7 km); seed: %8.")
				.arg(generated.cratons.size())
				.arg(dialog.start_time(), 0, 'f', 1)
				.arg(100.0 * generated.metrics.continent_area_fraction, 0, 'f', 2)
				.arg(100.0 * generated.metrics.craton_fill_fraction, 0, 'f', 2)
				.arg(100.0 * parameters.coastline_swiggle, 0, 'f', 0)
				.arg(generated.metrics.average_continent_segment_length_km, 0, 'f', 1)
				.arg(generated.metrics.maximum_continent_segment_length_km, 0, 'f', 1)
				.arg(parameters.random_seed));
	}
	catch (const std::exception &exception)
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr("Could not create the initial continent: %1").arg(exception.what()));
	}
}
