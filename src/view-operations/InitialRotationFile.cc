/* $Id$ */

/**
 * \file
 * Builds and validates the initial Worldbuilding Pasta plate circuit.
 */

#include "InitialRotationFile.h"

#include <algorithm>
#include <map>
#include <set>

#include <QObject>


namespace
{
	bool
	entry_less_than(
			const GPlatesViewOperations::InitialRotationFile::PlateCircuitEntry &lhs,
			const GPlatesViewOperations::InitialRotationFile::PlateCircuitEntry &rhs)
	{
		return lhs.moving_plate < rhs.moving_plate;
	}

	QString
	format_plate_id(
			unsigned long plate_id)
	{
		return QString("%1").arg(static_cast<qulonglong>(plate_id), 3, 10, QLatin1Char('0'));
	}
}


bool
GPlatesViewOperations::InitialRotationFile::validate_plate_circuit(
		QString &error_message,
		const plate_circuit_seq_type &entries)
{
	error_message.clear();
	if (entries.empty())
	{
		error_message = QObject::tr("No moving plate IDs were found.");
		return false;
	}

	std::map<unsigned long, unsigned long> parents;
	for (plate_circuit_seq_type::const_iterator entry_iter = entries.begin();
		entry_iter != entries.end(); ++entry_iter)
	{
		if (entry_iter->moving_plate == 0)
		{
			error_message = QObject::tr("Plate 0 is the rotation axis and must not have its own sequence.");
			return false;
		}
		if (entry_iter->moving_plate == entry_iter->fixed_plate)
		{
			error_message = QObject::tr("Plate %1 cannot use itself as its fixed plate.")
					.arg(entry_iter->moving_plate);
			return false;
		}
		if (!parents.insert(std::make_pair(
				entry_iter->moving_plate, entry_iter->fixed_plate)).second)
		{
			error_message = QObject::tr("Plate %1 appears more than once.")
					.arg(entry_iter->moving_plate);
			return false;
		}
	}

	for (std::map<unsigned long, unsigned long>::const_iterator parent_iter = parents.begin();
		parent_iter != parents.end(); ++parent_iter)
	{
		if (parent_iter->second != 0 && parents.find(parent_iter->second) == parents.end())
		{
			error_message = QObject::tr("Plate %1 refers to missing fixed Plate %2.")
					.arg(parent_iter->first).arg(parent_iter->second);
			return false;
		}

		std::set<unsigned long> path;
		unsigned long plate_id = parent_iter->first;
		while (plate_id != 0)
		{
			if (!path.insert(plate_id).second)
			{
				error_message = QObject::tr("The fixed-plate choices contain a cycle through Plate %1.")
						.arg(plate_id);
				return false;
			}
			plate_id = parents.find(plate_id)->second;
		}
	}
	return true;
}


QString
GPlatesViewOperations::InitialRotationFile::create_legacy_rotation_text(
		const plate_circuit_seq_type &entries,
		double start_time_ma)
{
	plate_circuit_seq_type sorted_entries(entries);
	std::sort(sorted_entries.begin(), sorted_entries.end(), entry_less_than);

	QString text;
	for (plate_circuit_seq_type::const_iterator entry_iter = sorted_entries.begin();
		entry_iter != sorted_entries.end(); ++entry_iter)
	{
		const QString moving_plate = format_plate_id(entry_iter->moving_plate);
		const QString fixed_plate = format_plate_id(entry_iter->fixed_plate);
		text += QString("%1  0.0  90.0  0.0  0.0  %2  ! Worldbuilding Pasta initial Plate %3\n")
				.arg(moving_plate, fixed_plate).arg(entry_iter->moving_plate);
		text += QString("%1  %2  90.0  0.0  0.0  %3  ! Worldbuilding Pasta start Plate %4\n")
				.arg(moving_plate, QString::number(start_time_ma, 'f', 1), fixed_plate)
				.arg(entry_iter->moving_plate);
	}
	return text;
}
