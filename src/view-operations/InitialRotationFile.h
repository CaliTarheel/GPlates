/* $Id$ */

/**
 * \file
 * Builds and validates the initial Worldbuilding Pasta plate circuit.
 */

#ifndef GPLATES_VIEWOPERATIONS_INITIALROTATIONFILE_H
#define GPLATES_VIEWOPERATIONS_INITIALROTATIONFILE_H

#include <vector>

#include <QString>


namespace GPlatesViewOperations
{
	namespace InitialRotationFile
	{
		struct PlateCircuitEntry
		{
			PlateCircuitEntry(unsigned long moving_plate_, unsigned long fixed_plate_) :
				moving_plate(moving_plate_),
				fixed_plate(fixed_plate_)
			{ }

			unsigned long moving_plate;
			unsigned long fixed_plate;
		};

		typedef std::vector<PlateCircuitEntry> plate_circuit_seq_type;

		/**
		 * Ensures every non-zero fixed plate exists and the circuit reaches Plate 0
		 * without self-parenting or cycles.
		 */
		bool
		validate_plate_circuit(
				QString &error_message,
				const plate_circuit_seq_type &entries);

		/**
		 * Writes the two identity poles prescribed by Worldbuilding Pasta: one at
		 * 0 Ma and one at the simulation start time.
		 */
		QString
		create_legacy_rotation_text(
				const plate_circuit_seq_type &entries,
				double start_time_ma);
	}
}

#endif // GPLATES_VIEWOPERATIONS_INITIALROTATIONFILE_H
