/* $Id$ */

/**
 * \file
 * Worldbuilding Pasta initial rotation-file creation and loading operation.
 */

#include "CreateInitialRotationFileOperation.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSaveFile>
#include <QStringList>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include "InitialRotationFile.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"

#include "model/FeatureType.h"
#include "model/TopLevelProperty.h"


namespace
{
	typedef GPlatesModel::integer_plate_id_type plate_id_type;

	struct PolygonPlate
	{
		PolygonPlate(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
				plate_id_type plate_id_) :
			polygon(polygon_),
			plate_id(plate_id_)
		{ }

		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
		plate_id_type plate_id;
	};

	struct DetectedPlate
	{
		DetectedPlate() : feature_count(0), has_continent(false), has_craton(false) { }

		unsigned int feature_count;
		bool has_continent;
		bool has_craton;
	};

	typedef std::map<plate_id_type, DetectedPlate> detected_plate_map_type;
	typedef std::vector<PolygonPlate> polygon_plate_seq_type;
	typedef std::map<plate_id_type, plate_id_type> parent_map_type;

	void
	gather_detected_plates(
			GPlatesAppLogic::ApplicationState &application_state,
			detected_plate_map_type &detected_plates,
			polygon_plate_seq_type &continents,
			polygon_plate_seq_type &cratons)
	{
		static const GPlatesModel::FeatureType CONTINENTAL_CRUST =
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust");
		static const GPlatesModel::FeatureType CRATON =
				GPlatesModel::FeatureType::create_gpml("Craton");
		std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
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
						!rfg.get_feature_ref().is_valid() || !rfg.reconstruction_plate_id() ||
						*rfg.reconstruction_plate_id() == 0)
				{
					continue;
				}
				const GPlatesModel::TopLevelProperty *property = (*rfg.property()).get();
				if (!seen_properties.insert(property).second)
				{
					continue;
				}
				const plate_id_type plate_id = *rfg.reconstruction_plate_id();
				DetectedPlate &detected = detected_plates[plate_id];
				++detected.feature_count;
				const GPlatesModel::FeatureType feature_type = rfg.get_feature_ref()->feature_type();
				const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
								rfg.reconstructed_geometry().get());
				if (feature_type == CONTINENTAL_CRUST)
				{
					detected.has_continent = true;
					if (polygon)
					{
						continents.push_back(PolygonPlate(polygon->get_non_null_pointer(), plate_id));
					}
				}
				else if (feature_type == CRATON)
				{
					detected.has_craton = true;
					if (polygon)
					{
						cratons.push_back(PolygonPlate(polygon->get_non_null_pointer(), plate_id));
					}
				}
			}
		}
	}

	plate_id_type
	choose_root_plate(
			const detected_plate_map_type &detected_plates)
	{
		for (detected_plate_map_type::const_iterator plate_iter = detected_plates.begin();
			plate_iter != detected_plates.end(); ++plate_iter)
		{
			if (plate_iter->second.has_continent)
			{
				return plate_iter->first;
			}
		}
		return detected_plates.begin()->first;
	}

	parent_map_type
	suggest_parents(
			const detected_plate_map_type &detected_plates,
			const polygon_plate_seq_type &continents,
			const polygon_plate_seq_type &cratons,
			plate_id_type root_plate)
	{
		parent_map_type parents;
		for (detected_plate_map_type::const_iterator plate_iter = detected_plates.begin();
			plate_iter != detected_plates.end(); ++plate_iter)
		{
			parents[plate_iter->first] = plate_iter->second.has_continent ||
					plate_iter->first == root_plate || plate_iter->first == 1 ? 0 : root_plate;
		}

		// A craton follows the smallest containing continental-crust polygon. This
		// naturally groups it with its post-rift child instead of a larger overlapping landmass.
		for (polygon_plate_seq_type::const_iterator craton_iter = cratons.begin();
			craton_iter != cratons.end(); ++craton_iter)
		{
			const GPlatesMaths::PointOnSphere craton_centre(
					craton_iter->polygon->get_interior_centroid());
			const PolygonPlate *smallest_containing_continent = NULL;
			for (polygon_plate_seq_type::const_iterator continent_iter = continents.begin();
				continent_iter != continents.end(); ++continent_iter)
			{
				if (continent_iter->plate_id != craton_iter->plate_id &&
						continent_iter->polygon->is_point_in_polygon(craton_centre) &&
						(!smallest_containing_continent || continent_iter->polygon->get_area() <
								smallest_containing_continent->polygon->get_area()))
				{
					smallest_containing_continent = &*continent_iter;
				}
			}
			if (smallest_containing_continent &&
					!detected_plates.find(craton_iter->plate_id)->second.has_continent)
			{
				parents[craton_iter->plate_id] = smallest_containing_continent->plate_id;
			}
		}
		return parents;
	}

	QString
	plate_summary(
			plate_id_type plate_id,
			const DetectedPlate &plate,
			plate_id_type root_plate)
	{
		QStringList roles;
		if (plate.has_continent)
		{
			roles << QObject::tr("continental crust");
		}
		if (plate.has_craton)
		{
			roles << QObject::tr("craton");
		}
		if (roles.empty())
		{
			roles << QObject::tr("other feature");
		}
		QString summary = QObject::tr("%1; %2 active feature(s)")
				.arg(roles.join(QObject::tr(" + "))).arg(plate.feature_count);
		if (plate_id == root_plate)
		{
			summary += QObject::tr("; recommended root");
		}
		return summary;
	}

	class RotationSetupDialog :
			public QDialog
	{
	public:
		RotationSetupDialog(
				const detected_plate_map_type &detected_plates,
				const parent_map_type &suggested_parents,
				plate_id_type root_plate,
				double current_time,
				QWidget *parent) :
			QDialog(parent),
			d_start_time(new QDoubleSpinBox(this)),
			d_table(new QTableWidget(static_cast<int>(detected_plates.size()), 3, this))
		{
			setWindowTitle(QObject::tr("Worldbuilding Pasta - Create + Load Rotation File"));
			setModal(true);
			QVBoxLayout *layout = new QVBoxLayout(this);
			QLabel *instructions = new QLabel(QObject::tr(
					"Worldbuilding Pasta starts each plate with an identity Euler pole at 0 Ma and at the simulation start. "
					"Plate 0 is the unmoving rotation axis. Continental plates are independently fixed to Plate 0; only separately numbered cratons may follow their containing continent. "
					"Review the suggested tree before saving."), this);
			instructions->setWordWrap(true);
			layout->addWidget(instructions);

			d_start_time->setRange(0.1, 4600.0);
			d_start_time->setDecimals(1);
			d_start_time->setSingleStep(50.0);
			d_start_time->setValue(std::max(0.1, current_time));
			d_start_time->setSuffix(QObject::tr(" Ma"));
			layout->addWidget(new QLabel(QObject::tr("Simulation start time:"), this));
			layout->addWidget(d_start_time);

			d_table->setHorizontalHeaderLabels(QStringList()
					<< QObject::tr("Plate ID") << QObject::tr("Detected use")
					<< QObject::tr("Fixed / conjugate plate"));
			d_table->verticalHeader()->setVisible(false);
			d_table->setSelectionMode(QAbstractItemView::NoSelection);
			d_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
			int row = 0;
			for (detected_plate_map_type::const_iterator plate_iter = detected_plates.begin();
				plate_iter != detected_plates.end(); ++plate_iter, ++row)
			{
				const plate_id_type plate_id = plate_iter->first;
				d_plate_ids.push_back(plate_id);
				d_table->setItem(row, 0, new QTableWidgetItem(QString::number(plate_id)));
				d_table->setItem(row, 1, new QTableWidgetItem(
						plate_summary(plate_id, plate_iter->second, root_plate)));
				QComboBox *parent_combo = new QComboBox(d_table);
				parent_combo->addItem(QObject::tr("0 - rotation axis / independent"), 0u);
				if (!plate_iter->second.has_continent)
				{
					for (detected_plate_map_type::const_iterator parent_iter = detected_plates.begin();
							parent_iter != detected_plates.end(); ++parent_iter)
					{
						if (parent_iter->first != plate_id)
						{
							parent_combo->addItem(QObject::tr("%1 - follows this plate")
									.arg(parent_iter->first), static_cast<uint>(parent_iter->first));
						}
					}
				}
				else
				{
					parent_combo->setEnabled(false);
					parent_combo->setToolTip(QObject::tr(
							"Continental plates must move independently relative to Plate 0."));
				}
				const parent_map_type::const_iterator suggested_parent = suggested_parents.find(plate_id);
				const plate_id_type parent_id = suggested_parent == suggested_parents.end()
						? 0 : suggested_parent->second;
				parent_combo->setCurrentIndex(parent_combo->findData(static_cast<uint>(parent_id)));
				d_parent_combos.push_back(parent_combo);
				d_table->setCellWidget(row, 2, parent_combo);
			}
			d_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
			d_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
			d_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
			d_table->resizeRowsToContents();
			layout->addWidget(d_table);

			QLabel *help = new QLabel(QObject::tr(
					"Suggested structure: every continental plate is independently fixed to Plate 0, while a separately numbered craton follows the continent containing it. "
					"Change any parent here if your intended plate hierarchy differs."), this);
			help->setWordWrap(true);
			layout->addWidget(help);
			QDialogButtonBox *buttons = new QDialogButtonBox(
					QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
			buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Choose File..."));
			QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this]()
			{
				QString error_message;
				if (!GPlatesViewOperations::InitialRotationFile::validate_plate_circuit(
						error_message, entries()))
				{
					QMessageBox::warning(this, QObject::tr("Invalid Plate Circuit"), error_message);
					return;
				}
				accept();
			});
			QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
			layout->addWidget(buttons);
			resize(760, std::min(700, 260 + static_cast<int>(detected_plates.size()) * 34));
		}

		double start_time() const { return d_start_time->value(); }

		GPlatesViewOperations::InitialRotationFile::plate_circuit_seq_type entries() const
		{
			GPlatesViewOperations::InitialRotationFile::plate_circuit_seq_type result;
			for (std::size_t index = 0; index < d_plate_ids.size(); ++index)
			{
				result.push_back(GPlatesViewOperations::InitialRotationFile::PlateCircuitEntry(
						static_cast<unsigned long>(d_plate_ids[index]),
						static_cast<unsigned long>(d_parent_combos[index]->currentData().toUInt())));
			}
			return result;
		}

	private:
		QDoubleSpinBox *d_start_time;
		QTableWidget *d_table;
		std::vector<plate_id_type> d_plate_ids;
		std::vector<QComboBox *> d_parent_combos;
	};

	QString
	default_rotation_path(
			GPlatesAppLogic::FeatureCollectionFileState &file_state)
	{
		const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
				file_state.get_loaded_files();
		for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
				file_iter = files.begin(); file_iter != files.end(); ++file_iter)
		{
			const QFileInfo info = file_iter->get_file().get_file_info().get_qfileinfo();
			if (!info.absolutePath().isEmpty())
			{
				return info.absolutePath() + QLatin1String("/rotation.rot");
			}
		}
		return QStringLiteral("rotation.rot");
	}

	bool
	is_file_loaded(
			const QString &filename,
			GPlatesAppLogic::FeatureCollectionFileState &file_state)
	{
		const QString absolute_path = QFileInfo(filename).absoluteFilePath();
		const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
				file_state.get_loaded_files();
		for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
				file_iter = files.begin(); file_iter != files.end(); ++file_iter)
		{
			if (QFileInfo(file_iter->get_file().get_file_info().get_qfileinfo()).absoluteFilePath() == absolute_path)
			{
				return true;
			}
		}
		return false;
	}
}


GPlatesViewOperations::CreateInitialRotationFileOperation::CreateInitialRotationFileOperation(
		GPlatesAppLogic::ApplicationState &application_state) :
	d_application_state(application_state)
{ }


GPlatesViewOperations::CreateInitialRotationFileOperation::Result
GPlatesViewOperations::CreateInitialRotationFileOperation::trigger(
		QWidget *parent_widget)
{
	try
	{
		detected_plate_map_type detected_plates;
		polygon_plate_seq_type continents;
		polygon_plate_seq_type cratons;
		gather_detected_plates(d_application_state, detected_plates, continents, cratons);
		if (detected_plates.empty())
		{
			return Result(OPERATION_ERROR, QObject::tr(
					"No active reconstructed features with non-zero plate IDs were found at the current time."));
		}

		const plate_id_type root_plate = choose_root_plate(detected_plates);
		const parent_map_type suggested_parents = suggest_parents(
				detected_plates, continents, cratons, root_plate);
		RotationSetupDialog dialog(
				detected_plates,
				suggested_parents,
				root_plate,
				d_application_state.get_current_reconstruction().get_reconstruction_time(),
				parent_widget);
		if (dialog.exec() != QDialog::Accepted)
		{
			return Result(OPERATION_CANCELLED, QObject::tr("Rotation-file creation cancelled."));
		}

		GPlatesAppLogic::FeatureCollectionFileState &file_state =
				d_application_state.get_feature_collection_file_state();
		QString filename = QFileDialog::getSaveFileName(
				parent_widget,
				QObject::tr("Create Worldbuilding Rotation File"),
				default_rotation_path(file_state),
				QObject::tr("GPlates rotation files (*.rot);;All files (*)"));
		if (filename.isEmpty())
		{
			return Result(OPERATION_CANCELLED, QObject::tr("Rotation-file creation cancelled."));
		}
		if (QFileInfo(filename).suffix().isEmpty())
		{
			filename += QStringLiteral(".rot");
		}
		if (is_file_loaded(filename, file_state))
		{
			return Result(OPERATION_ERROR, QObject::tr(
					"'%1' is already loaded. Choose another filename, or edit/reload the existing rotation collection.")
					.arg(QFileInfo(filename).fileName()));
		}

		const InitialRotationFile::plate_circuit_seq_type entries = dialog.entries();
		const QString rotation_text = InitialRotationFile::create_legacy_rotation_text(
				entries, dialog.start_time());
		QSaveFile output_file(filename);
		if (!output_file.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			return Result(OPERATION_ERROR, QObject::tr("Could not create '%1': %2")
					.arg(filename, output_file.errorString()));
		}
		QTextStream stream(&output_file);
		stream << rotation_text;
		if (!output_file.commit())
		{
			return Result(OPERATION_ERROR, QObject::tr("Could not finish writing '%1': %2")
					.arg(filename, output_file.errorString()));
		}

		d_application_state.get_feature_collection_file_io().load_file(filename);
		return Result(OPERATION_COMPLETED, QObject::tr(
				"Created and loaded '%1' with %2 plate sequences and identity poles at 0.0 and %3 Ma. "
				"The file is now the editable rotation history used by Advance Plate Motion.")
				.arg(QFileInfo(filename).fileName()).arg(entries.size()).arg(dialog.start_time(), 0, 'f', 1));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR, QObject::tr("Could not create or load the rotation file: %1")
				.arg(exception.what()));
	}
}
