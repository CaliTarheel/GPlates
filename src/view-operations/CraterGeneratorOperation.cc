/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#include "CraterGeneratorOperation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include <boost/optional.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QStringList>
#include <QUndoCommand>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryCookieCutter.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/ReconstructUtils.h"

#include "feature-visitors/GeometrySetter.h"

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/UnitVector3D.h"
#include "maths/Vector3D.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/UndoRedo.h"


namespace
{
	const double PI = 3.141592653589793238462643383279502884;
	const char *const MODEL_CITATION =
			"Moore, Boyce & Hahn (1980), doi:10.1007/BF00899820";

	enum SpatialDistribution
	{
		UNIFORM_SPHERE,
		UNIFORM_LAT_LON_WINDOW
	};

	struct FeatureRecord
	{
		FeatureRecord(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &feature_) :
			collection(collection_),
			feature(feature_)
		{  }

		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		GPlatesModel::FeatureHandle::non_null_ptr_type feature;
	};

	typedef std::vector<FeatureRecord> feature_record_seq_type;


	class AddCraterFeaturesUndoCommand :
			public QUndoCommand
	{
	public:
		AddCraterFeaturesUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const feature_record_seq_type &features) :
			d_model_interface(model_interface),
			d_features(features)
		{
			setText(QObject::tr("generate impact craters"));
		}

		virtual void redo()
		{
			apply(true);
		}

		virtual void undo()
		{
			apply(false);
		}

	private:
		void apply(bool add)
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (feature_record_seq_type::iterator feature_iter = d_features.begin();
					feature_iter != d_features.end(); ++feature_iter)
			{
				if (!feature_iter->collection.is_valid())
				{
					continue;
				}
				if (add && !feature_iter->feature->parent_ptr())
				{
					feature_iter->collection->add(feature_iter->feature);
				}
				else if (!add && feature_iter->feature->parent_ptr())
				{
					feature_iter->feature->remove_from_parent();
				}
			}
			guard.release_guard();
		}

		GPlatesModel::ModelInterface d_model_interface;
		feature_record_seq_type d_features;
	};


	double
	next_unit(
			std::mt19937_64 &engine)
	{
		// Use the engine's high 53 bits directly instead of
		// std::uniform_real_distribution, whose mapping is implementation-defined.
		return static_cast<double>(engine() >> 11) * (1.0 / 9007199254740992.0);
	}


	double
	sample_diameter(
			std::mt19937_64 &engine,
			double minimum_diameter,
			double maximum_diameter,
			double cumulative_exponent)
	{
		const double minimum_power = std::pow(minimum_diameter, -cumulative_exponent);
		const double maximum_power = std::pow(maximum_diameter, -cumulative_exponent);
		const double sampled_power = minimum_power -
				next_unit(engine) * (minimum_power - maximum_power);
		return std::pow(sampled_power, -1.0 / cumulative_exponent);
	}


	GPlatesMaths::PointOnSphere
	sample_position(
			std::mt19937_64 &engine,
			SpatialDistribution distribution,
			double minimum_latitude,
			double maximum_latitude,
			double minimum_longitude,
			double maximum_longitude)
	{
		double minimum_z = -1.0;
		double maximum_z = 1.0;
		double minimum_longitude_radians = -PI;
		double maximum_longitude_radians = PI;
		if (distribution == UNIFORM_LAT_LON_WINDOW)
		{
			minimum_z = std::sin(minimum_latitude * PI / 180.0);
			maximum_z = std::sin(maximum_latitude * PI / 180.0);
			minimum_longitude_radians = minimum_longitude * PI / 180.0;
			maximum_longitude_radians = maximum_longitude * PI / 180.0;
		}

		const double z = minimum_z + next_unit(engine) * (maximum_z - minimum_z);
		const double longitude = minimum_longitude_radians + next_unit(engine) *
				(maximum_longitude_radians - minimum_longitude_radians);
		const double radius_at_z = std::sqrt(std::max(0.0, 1.0 - z * z));
		return GPlatesMaths::PointOnSphere(GPlatesMaths::UnitVector3D(
				radius_at_z * std::cos(longitude),
				radius_at_z * std::sin(longitude),
				z));
	}


	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	create_crater_circle(
			const GPlatesMaths::PointOnSphere &centre,
			double diameter_km,
			double planet_radius_km,
			unsigned int vertex_count)
	{
		const double angular_radius = diameter_km / (2.0 * planet_radius_km);
		const GPlatesMaths::UnitVector3D centre_vector = centre.position_vector();
		const GPlatesMaths::UnitVector3D tangent_x =
				GPlatesMaths::generate_perpendicular(centre_vector);
		const GPlatesMaths::UnitVector3D tangent_y =
				GPlatesMaths::cross(centre_vector, tangent_x).get_normalisation();
		const double centre_scale = std::cos(angular_radius);
		const double tangent_scale = std::sin(angular_radius);

		std::vector<GPlatesMaths::PointOnSphere> vertices;
		vertices.reserve(vertex_count);
		for (unsigned int vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
		{
			const double angle = 2.0 * PI * vertex_index / vertex_count;
			const double tangent_x_scale = tangent_scale * std::cos(angle);
			const double tangent_y_scale = tangent_scale * std::sin(angle);
			const GPlatesMaths::Vector3D vertex_vector(
					centre_scale * centre_vector.x().dval() +
						tangent_x_scale * tangent_x.x().dval() +
						tangent_y_scale * tangent_y.x().dval(),
					centre_scale * centre_vector.y().dval() +
						tangent_x_scale * tangent_x.y().dval() +
						tangent_y_scale * tangent_y.y().dval(),
					centre_scale * centre_vector.z().dval() +
						tangent_x_scale * tangent_x.z().dval() +
						tangent_y_scale * tangent_y.z().dval());
			vertices.push_back(GPlatesMaths::PointOnSphere(vertex_vector.get_normalisation()));
		}
		return GPlatesMaths::PolygonOnSphere::create(vertices);
	}


	GPlatesModel::FeatureHandle::iterator
	add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		const boost::optional<GPlatesModel::FeatureHandle::iterator> property =
				GPlatesModel::ModelUtils::add_property(feature, name, value);
		if (!property)
		{
			throw std::runtime_error("Could not create a required crater property.");
		}
		return property.get();
	}


	GPlatesModel::FeatureHandle::non_null_ptr_type
	create_crater_feature(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesMaths::PointOnSphere &event_centre,
			double event_time,
			double diameter_km,
			double planet_radius_km,
			double display_lifetime,
			double cumulative_exponent,
			std::uint64_t seed,
			std::size_t event_index,
			unsigned int vertex_count,
			GPlatesModel::integer_plate_id_type plate_id)
	{
		const GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("UnclassifiedFeature"));
		const double end_time = display_lifetime <= 0.0
				? 0.0
				: std::max(0.0, event_time - display_lifetime);
		const GPlatesMaths::LatLonPoint centre_lat_lon =
				GPlatesMaths::make_lat_lon_point(event_centre);
		const QString description = QObject::tr(
				"GreaterPlates impact crater; event_index=%1; event_time_ma=%2; "
				"diameter_km=%3; centre_lat=%4; centre_lon=%5; plate_id=%6; "
				"model=truncated cumulative power law; cumulative_exponent=%7; "
				"seed=%8; citation=%9;")
				.arg(event_index)
				.arg(event_time, 0, 'f', 6)
				.arg(diameter_km, 0, 'f', 6)
				.arg(centre_lat_lon.latitude(), 0, 'f', 6)
				.arg(centre_lat_lon.longitude(), 0, 'f', 6)
				.arg(plate_id)
				.arg(cumulative_exponent, 0, 'f', 6)
				.arg(static_cast<qulonglong>(seed))
				.arg(QString::fromLatin1(MODEL_CITATION));

		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(
								QObject::tr("Impact crater %1 (%2 km)")
										.arg(event_index).arg(diameter_km, 0, 'f', 3))));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gml("description"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(description)));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesPropertyValues::GmlTimePeriod::create(
						GPlatesModel::ModelUtils::create_gml_time_instant(
								GPlatesPropertyValues::GeoTimeInstant(event_time)),
						GPlatesModel::ModelUtils::create_gml_time_instant(
								GPlatesPropertyValues::GeoTimeInstant(end_time))));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));

		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type event_geometry =
				create_crater_circle(event_centre, diameter_km, planet_radius_km, vertex_count);
		const boost::optional<GPlatesModel::PropertyValue::non_null_ptr_type> geometry_value =
				GPlatesAppLogic::GeometryUtils::create_geometry_property_value(event_geometry);
		if (!geometry_value)
		{
			throw std::runtime_error("Could not encode the crater polygon.");
		}
		const GPlatesModel::FeatureHandle::iterator geometry_property = add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gpml("unclassifiedGeometry"),
				geometry_value.get());

		const GPlatesAppLogic::Reconstruction &reconstruction =
				application_state.get_current_reconstruction();
		const GPlatesAppLogic::ReconstructMethodRegistry registry;
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type present_day_geometry =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						event_geometry,
						registry,
						feature->reference(),
						event_time,
						reconstruction.get_default_reconstruction_layer_output()
								->get_reconstruction_tree_creator(),
						GPlatesAppLogic::ReconstructParams(),
						true);
		GPlatesModel::TopLevelProperty::non_null_ptr_type present_day_property =
				(*geometry_property)->clone();
		GPlatesFeatureVisitors::GeometrySetter geometry_setter(present_day_geometry);
		geometry_setter.set_geometry(present_day_property.get());
		feature->set(geometry_property, present_day_property);
		return feature;
	}


	QString
	build_report(
			std::size_t timestamp_count,
			std::size_t requested,
			std::size_t assigned,
			std::size_t unassigned,
			const std::vector<std::size_t> &diameter_bins,
			double minimum_diameter,
			double maximum_diameter,
			const std::map<GPlatesModel::integer_plate_id_type, std::size_t> &plate_counts,
			const QStringList &unassigned_details)
	{
		QString report = QObject::tr(
				"Timestamps: %1\nRequested impacts: %2\nAssigned impacts: %3\n"
				"Unassigned impacts: %4")
				.arg(timestamp_count).arg(requested).arg(assigned).arg(unassigned);
		report += QObject::tr("\n\nDiameter bins (all sampled impacts):");
		const double log_minimum = std::log(minimum_diameter);
		const double log_span = std::log(maximum_diameter) - log_minimum;
		for (std::size_t bin_index = 0; bin_index < diameter_bins.size(); ++bin_index)
		{
			const double bin_minimum = std::exp(log_minimum +
					log_span * bin_index / diameter_bins.size());
			const double bin_maximum = std::exp(log_minimum +
					log_span * (bin_index + 1) / diameter_bins.size());
			report += QObject::tr("\n%1--%2 km: %3")
					.arg(bin_minimum, 0, 'g', 6)
					.arg(bin_maximum, 0, 'g', 6)
					.arg(diameter_bins[bin_index]);
		}
		if (!plate_counts.empty())
		{
			report += QObject::tr("\n\nAssigned by Plate ID:");
			for (std::map<GPlatesModel::integer_plate_id_type, std::size_t>::const_iterator
					plate_iter = plate_counts.begin(); plate_iter != plate_counts.end(); ++plate_iter)
			{
				report += QObject::tr("\n%1: %2").arg(plate_iter->first).arg(plate_iter->second);
			}
		}
		if (!unassigned_details.isEmpty())
		{
			report += QObject::tr("\n\nUnassigned details (first 50):\n") +
					unassigned_details.join("\n");
		}
		return report;
	}
}


GPlatesViewOperations::CraterGeneratorOperation::CraterGeneratorOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		QWidget *parent_widget) :
	d_application_state(application_state),
	d_parent_widget(parent_widget)
{  }


void
GPlatesViewOperations::CraterGeneratorOperation::trigger()
{
	GPlatesAppLogic::FeatureCollectionFileState &file_state =
			d_application_state.get_feature_collection_file_state();
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
			file_state.get_loaded_files();
	if (files.empty())
	{
		QMessageBox::information(d_parent_widget, tr("Impact Crater Generator"),
				tr("Load a plate-polygon collection and an output collection first."));
		return;
	}

	QDialog dialog(d_parent_widget);
	dialog.setWindowTitle(tr("Generate Impact Craters"));
	QFormLayout *layout = new QFormLayout(&dialog);
	QLabel *intro = new QLabel(tr(
			"Creates deterministic circular crater features at inclusive project timestamps. "
			"Diameters follow a user-controlled truncated cumulative power law. The default "
			"exponent 2.8 follows the small lunar production example of Moore, Boyce & Hahn "
			"(1980), not a planet-specific calibrated chronology."), &dialog);
	intro->setWordWrap(true);
	layout->addRow(intro);

	QComboBox *partition_combo = new QComboBox(&dialog);
	QComboBox *output_combo = new QComboBox(&dialog);
	for (std::size_t file_index = 0; file_index < files.size(); ++file_index)
	{
		const QString display_name = files[file_index].get_file().get_file_info().get_display_name(true);
		partition_combo->addItem(display_name);
		output_combo->addItem(display_name);
	}
	if (files.size() > 1)
	{
		output_combo->setCurrentIndex(static_cast<int>(files.size() - 1));
	}

	QDoubleSpinBox *youngest_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *oldest_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *step_spin = new QDoubleSpinBox(&dialog);
	for (QDoubleSpinBox *spin : std::vector<QDoubleSpinBox *>{ youngest_spin, oldest_spin, step_spin })
	{
		spin->setRange(0.0, 10000.0);
		spin->setDecimals(3);
	}
	youngest_spin->setValue(0.0);
	oldest_spin->setValue(100.0);
	step_spin->setMinimum(0.001);
	step_spin->setValue(10.0);
	QSpinBox *impacts_spin = new QSpinBox(&dialog);
	impacts_spin->setRange(1, 1000);
	impacts_spin->setValue(5);
	QSpinBox *seed_spin = new QSpinBox(&dialog);
	seed_spin->setRange(0, 2147483647);
	seed_spin->setValue(42);

	QDoubleSpinBox *radius_spin = new QDoubleSpinBox(&dialog);
	radius_spin->setRange(1.0, 1000000.0);
	radius_spin->setDecimals(3);
	radius_spin->setValue(6371.0);
	QDoubleSpinBox *minimum_diameter_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *maximum_diameter_spin = new QDoubleSpinBox(&dialog);
	for (QDoubleSpinBox *spin : std::vector<QDoubleSpinBox *>{
			minimum_diameter_spin, maximum_diameter_spin })
	{
		spin->setRange(0.001, 1000000.0);
		spin->setDecimals(3);
	}
	minimum_diameter_spin->setValue(10.0);
	maximum_diameter_spin->setValue(100.0);
	QDoubleSpinBox *exponent_spin = new QDoubleSpinBox(&dialog);
	exponent_spin->setRange(0.1, 10.0);
	exponent_spin->setDecimals(3);
	exponent_spin->setValue(2.8);
	QDoubleSpinBox *lifetime_spin = new QDoubleSpinBox(&dialog);
	lifetime_spin->setRange(0.0, 10000.0);
	lifetime_spin->setDecimals(3);
	lifetime_spin->setValue(0.0);
	QSpinBox *vertices_spin = new QSpinBox(&dialog);
	vertices_spin->setRange(8, 360);
	vertices_spin->setValue(48);

	QComboBox *spatial_combo = new QComboBox(&dialog);
	spatial_combo->addItem(tr("Uniform on sphere"), UNIFORM_SPHERE);
	spatial_combo->addItem(tr("Uniform equal-area latitude/longitude window"), UNIFORM_LAT_LON_WINDOW);
	QDoubleSpinBox *minimum_latitude_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *maximum_latitude_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *minimum_longitude_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *maximum_longitude_spin = new QDoubleSpinBox(&dialog);
	for (QDoubleSpinBox *spin : std::vector<QDoubleSpinBox *>{
			minimum_latitude_spin, maximum_latitude_spin })
	{
		spin->setRange(-90.0, 90.0);
		spin->setDecimals(3);
	}
	for (QDoubleSpinBox *spin : std::vector<QDoubleSpinBox *>{
			minimum_longitude_spin, maximum_longitude_spin })
	{
		spin->setRange(-180.0, 180.0);
		spin->setDecimals(3);
	}
	minimum_latitude_spin->setValue(-90.0);
	maximum_latitude_spin->setValue(90.0);
	minimum_longitude_spin->setValue(-180.0);
	maximum_longitude_spin->setValue(180.0);
	QCheckBox *dry_run_checkbox = new QCheckBox(tr("Dry run only"), &dialog);
	dry_run_checkbox->setChecked(true);

	layout->addRow(tr("Plate polygon collection:"), partition_combo);
	layout->addRow(tr("Output collection:"), output_combo);
	layout->addRow(tr("Youngest timestamp (Ma):"), youngest_spin);
	layout->addRow(tr("Oldest timestamp (Ma):"), oldest_spin);
	layout->addRow(tr("Timestamp step (My):"), step_spin);
	layout->addRow(tr("Impacts per timestamp:"), impacts_spin);
	layout->addRow(tr("Random seed:"), seed_spin);
	layout->addRow(tr("Planet radius (km):"), radius_spin);
	layout->addRow(tr("Minimum diameter (km):"), minimum_diameter_spin);
	layout->addRow(tr("Maximum diameter (km):"), maximum_diameter_spin);
	layout->addRow(tr("Cumulative power-law exponent:"), exponent_spin);
	layout->addRow(tr("Display lifetime (My; 0 = to present):"), lifetime_spin);
	layout->addRow(tr("Circle vertices:"), vertices_spin);
	layout->addRow(tr("Spatial distribution:"), spatial_combo);
	layout->addRow(tr("Minimum latitude:"), minimum_latitude_spin);
	layout->addRow(tr("Maximum latitude:"), maximum_latitude_spin);
	layout->addRow(tr("Minimum longitude:"), minimum_longitude_spin);
	layout->addRow(tr("Maximum longitude:"), maximum_longitude_spin);
	layout->addRow(dry_run_checkbox);
	QLabel *note = new QLabel(tr(
			"Each sampled centre is assigned to the containing polygon at its event time. "
			"Unassigned impacts are reported and skipped, never forced to Plate ID 0. "
			"Applying the generated batch is one undo step."), &dialog);
	note->setWordWrap(true);
	layout->addRow(note);
	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addRow(buttons);
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	if (youngest_spin->value() > oldest_spin->value())
	{
		QMessageBox::warning(d_parent_widget, tr("Impact Crater Generator"),
				tr("Oldest timestamp must be at least the youngest timestamp."));
		return;
	}
	if (minimum_diameter_spin->value() >= maximum_diameter_spin->value())
	{
		QMessageBox::warning(d_parent_widget, tr("Impact Crater Generator"),
				tr("Maximum diameter must be greater than minimum diameter."));
		return;
	}
	if (maximum_diameter_spin->value() >= 2.0 * PI * radius_spin->value())
	{
		QMessageBox::warning(d_parent_widget, tr("Impact Crater Generator"),
				tr("Maximum diameter must be smaller than the planet circumference."));
		return;
	}
	const SpatialDistribution spatial_distribution = static_cast<SpatialDistribution>(
			spatial_combo->currentData().toInt());
	if (spatial_distribution == UNIFORM_LAT_LON_WINDOW &&
			(minimum_latitude_spin->value() >= maximum_latitude_spin->value() ||
			 minimum_longitude_spin->value() >= maximum_longitude_spin->value()))
	{
		QMessageBox::warning(d_parent_widget, tr("Impact Crater Generator"),
				tr("The latitude/longitude window requires increasing minimum and maximum values."));
		return;
	}

	std::vector<double> timestamps;
	try
	{
		timestamps = GPlatesAppLogic::ProjectTimestampSchedule::build(
				youngest_spin->value(), oldest_spin->value(), step_spin->value(), 10000);
	}
	catch (const std::exception &exception)
	{
		QMessageBox::warning(d_parent_widget, tr("Impact Crater Generator"),
				tr("Invalid timestamp schedule: %1").arg(QString::fromUtf8(exception.what())));
		return;
	}
	const std::size_t requested = timestamps.size() *
			static_cast<std::size_t>(impacts_spin->value());
	if (requested > 100000)
	{
		QMessageBox::warning(d_parent_widget, tr("Impact Crater Generator"),
				tr("This run requests %1 impacts; the safety limit is 100,000.").arg(requested));
		return;
	}

	const GPlatesModel::FeatureCollectionHandle::weak_ref partition_collection =
			files[partition_combo->currentIndex()].get_file().get_feature_collection();
	const GPlatesModel::FeatureCollectionHandle::weak_ref output_collection =
			files[output_combo->currentIndex()].get_file().get_feature_collection();
	std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref> partition_collections;
	partition_collections.push_back(partition_collection);

	std::mt19937_64 engine(static_cast<std::uint64_t>(seed_spin->value()));
	std::size_t event_index = 0;
	std::size_t assigned = 0;
	std::size_t unassigned = 0;
	std::vector<std::size_t> diameter_bins(4, 0);
	std::map<GPlatesModel::integer_plate_id_type, std::size_t> plate_counts;
	QStringList unassigned_details;
	feature_record_seq_type added;
	const GPlatesAppLogic::Reconstruction &reconstruction =
			d_application_state.get_current_reconstruction();
	const GPlatesAppLogic::ReconstructMethodRegistry registry;

	try
	{
		for (std::vector<double>::const_iterator timestamp_iter = timestamps.begin();
				timestamp_iter != timestamps.end(); ++timestamp_iter)
		{
			GPlatesAppLogic::GeometryCookieCutter cookie_cutter(
					*timestamp_iter,
					registry,
					partition_collections,
					reconstruction.get_default_reconstruction_layer_output()
							->get_reconstruction_tree_creator());
			for (int impact_index = 0; impact_index < impacts_spin->value(); ++impact_index)
			{
				++event_index;
				const double diameter = sample_diameter(
						engine,
						minimum_diameter_spin->value(),
						maximum_diameter_spin->value(),
						exponent_spin->value());
				const GPlatesMaths::PointOnSphere centre = sample_position(
						engine,
						spatial_distribution,
						minimum_latitude_spin->value(),
						maximum_latitude_spin->value(),
						minimum_longitude_spin->value(),
						maximum_longitude_spin->value());
				const double log_fraction = (std::log(diameter) -
						std::log(minimum_diameter_spin->value())) /
						(std::log(maximum_diameter_spin->value()) -
						 std::log(minimum_diameter_spin->value()));
				const std::size_t bin_index = std::min<std::size_t>(
						diameter_bins.size() - 1,
						static_cast<std::size_t>(std::max(0.0, log_fraction) * diameter_bins.size()));
				++diameter_bins[bin_index];

				const boost::optional<const GPlatesAppLogic::ReconstructionGeometry *> containing_geometry =
						cookie_cutter.partition_point(centre);
				const boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
						containing_geometry
						? GPlatesAppLogic::ReconstructionGeometryUtils::get_plate_id(
								containing_geometry.get())
						: boost::optional<GPlatesModel::integer_plate_id_type>();
				if (!plate_id)
				{
					++unassigned;
					if (unassigned_details.size() < 50)
					{
						const GPlatesMaths::LatLonPoint centre_lat_lon =
								GPlatesMaths::make_lat_lon_point(centre);
						unassigned_details.append(tr(
								"event %1: %2 Ma, %3 km, lat %4, lon %5")
								.arg(event_index)
								.arg(*timestamp_iter, 0, 'f', 3)
								.arg(diameter, 0, 'f', 3)
								.arg(centre_lat_lon.latitude(), 0, 'f', 3)
								.arg(centre_lat_lon.longitude(), 0, 'f', 3));
					}
					continue;
				}

				++assigned;
				++plate_counts[plate_id.get()];
				if (!dry_run_checkbox->isChecked())
				{
					added.push_back(FeatureRecord(
							output_collection,
							create_crater_feature(
									d_application_state,
									centre,
									*timestamp_iter,
									diameter,
									radius_spin->value(),
									lifetime_spin->value(),
									exponent_spin->value(),
									static_cast<std::uint64_t>(seed_spin->value()),
									event_index,
									static_cast<unsigned int>(vertices_spin->value()),
									plate_id.get())));
				}
			}
		}
	}
	catch (const std::exception &exception)
	{
		QMessageBox::critical(d_parent_widget, tr("Impact Crater Generator"),
				tr("No changes were made. Crater preparation failed: %1")
						.arg(QString::fromUtf8(exception.what())));
		return;
	}

	const QString report = build_report(
			timestamps.size(),
			requested,
			assigned,
			unassigned,
			diameter_bins,
			minimum_diameter_spin->value(),
			maximum_diameter_spin->value(),
			plate_counts,
			unassigned_details);
	if (dry_run_checkbox->isChecked() || added.empty())
	{
		QMessageBox::information(d_parent_widget, tr("Impact Crater Generator - Dry Run"), report);
		return;
	}

	std::unique_ptr<QUndoCommand> command(new AddCraterFeaturesUndoCommand(
			d_application_state.get_model_interface(), added));
	GPlatesViewOperations::UndoRedo::instance().get_active_undo_stack().push(command.release());
	QMessageBox::information(d_parent_widget, tr("Impact Crater Generator"),
			report + tr("\n\nGenerated in one undoable operation."));
}
