/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#include "PlateEventTransaction.h"

#include <stdexcept>
#include <utility>

#include <QUndoCommand>

#include "RenderedGeometryCollection.h"
#include "UndoRedo.h"


namespace
{
	class CompositePlateEventCommand : public QUndoCommand
	{
	public:
		CompositePlateEventCommand(
				const QString &text,
				std::vector<std::unique_ptr<QUndoCommand> > commands) :
			QUndoCommand(text),
			d_commands(std::move(commands))
		{ }

		void redo()
		{
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard guard;
			for (std::vector<std::unique_ptr<QUndoCommand> >::iterator command = d_commands.begin();
				 command != d_commands.end(); ++command)
			{
				(*command)->redo();
			}
		}

		void undo()
		{
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard guard;
			for (std::vector<std::unique_ptr<QUndoCommand> >::reverse_iterator command = d_commands.rbegin();
				 command != d_commands.rend(); ++command)
			{
				(*command)->undo();
			}
		}

	private:
		std::vector<std::unique_ptr<QUndoCommand> > d_commands;
	};

	QString participant_name(GPlatesViewOperations::PlateEventTransaction::Participant participant)
	{
		switch (participant)
		{
		case GPlatesViewOperations::PlateEventTransaction::FEATURE_GEOMETRY:
			return QString::fromLatin1("feature geometry");
		case GPlatesViewOperations::PlateEventTransaction::FEATURE_PROPERTIES:
			return QString::fromLatin1("feature properties");
		case GPlatesViewOperations::PlateEventTransaction::ROTATION_MODEL:
			return QString::fromLatin1("rotation model");
		case GPlatesViewOperations::PlateEventTransaction::TOPOLOGY_NETWORK:
			return QString::fromLatin1("topology network");
		}
		return QString::fromLatin1("unknown");
	}
}


GPlatesViewOperations::PlateEventTransaction::PlateEventTransaction(
		const QString &text) :
	d_text(text),
	d_committed(false)
{ }


GPlatesViewOperations::PlateEventTransaction::~PlateEventTransaction()
{ }


void
GPlatesViewOperations::PlateEventTransaction::add_command(
		std::unique_ptr<QUndoCommand> command,
		Participant participant)
{
	if (d_committed || !command)
	{
		throw std::logic_error("Cannot add a command to a committed plate-event transaction.");
	}
	d_commands.push_back(std::move(command));
	d_participants.push_back(participant);
}


bool
GPlatesViewOperations::PlateEventTransaction::is_empty() const
{
	return d_commands.empty();
}


QStringList
GPlatesViewOperations::PlateEventTransaction::participant_names() const
{
	QStringList names;
	for (std::vector<Participant>::const_iterator participant = d_participants.begin();
		 participant != d_participants.end(); ++participant)
	{
		const QString name = participant_name(*participant);
		if (!names.contains(name))
		{
			names.append(name);
		}
	}
	return names;
}


void
GPlatesViewOperations::PlateEventTransaction::commit()
{
	if (d_committed || d_commands.empty())
	{
		return;
	}
	d_committed = true;
	std::unique_ptr<QUndoCommand> composite(
			new CompositePlateEventCommand(d_text, std::move(d_commands)));
	UndoRedo::instance().get_active_undo_stack().push(composite.release());
}


void
GPlatesViewOperations::PlateEventTransaction::commit_command(
		std::unique_ptr<QUndoCommand> command,
		const QString &text,
		Participant participant)
{
	PlateEventTransaction transaction(text);
	transaction.add_command(std::move(command), participant);
	transaction.commit();
}
