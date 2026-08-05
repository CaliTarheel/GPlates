/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_VIEWOPERATIONS_PLATEEVENTTRANSACTION_H
#define GPLATES_VIEWOPERATIONS_PLATEEVENTTRANSACTION_H

#include <memory>
#include <vector>

#include <QString>
#include <QStringList>

class QUndoCommand;


namespace GPlatesViewOperations
{
	/** Groups heterogeneous plate-event changes into one undo-stack entry. */
	class PlateEventTransaction
	{
	public:
		enum Participant
		{
			FEATURE_GEOMETRY,
			FEATURE_PROPERTIES,
			ROTATION_MODEL,
			TOPOLOGY_NETWORK
		};

		explicit PlateEventTransaction(const QString &text);
		~PlateEventTransaction();

		void add_command(std::unique_ptr<QUndoCommand> command, Participant participant);
		bool is_empty() const;
		QStringList participant_names() const;
		void commit();

		static void commit_command(
				std::unique_ptr<QUndoCommand> command,
				const QString &text,
				Participant participant);

	private:
		QString d_text;
		std::vector<std::unique_ptr<QUndoCommand> > d_commands;
		std::vector<Participant> d_participants;
		bool d_committed;
	};
}

#endif // GPLATES_VIEWOPERATIONS_PLATEEVENTTRANSACTION_H
