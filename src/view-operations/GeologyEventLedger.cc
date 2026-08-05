/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "GeologyEventLedger.h"
#include <algorithm>
#include <map>
#include "app-logic/FeatureCollectionFileState.h"
#include "model/FeatureCollectionHandle.h"

namespace
{
	GPlatesViewOperations::GeologyEventLedger::Category category_for(
			const GPlatesViewOperations::FeatureEventVersioner::EventRecord &event)
	{
		const QString relation = event.relation.toLower();
		if (relation.contains("observation")) return GPlatesViewOperations::GeologyEventLedger::OBSERVATION;
		if (relation.contains("derived-geometry")) return GPlatesViewOperations::GeologyEventLedger::DERIVED_GEOMETRY;
		if (relation.contains("interpretation")) return GPlatesViewOperations::GeologyEventLedger::INTERPRETATION;
		return GPlatesViewOperations::GeologyEventLedger::MODEL_CHANGE;
	}
}

std::vector<GPlatesViewOperations::GeologyEventLedger::Entry>
GPlatesViewOperations::GeologyEventLedger::build(
		GPlatesAppLogic::FeatureCollectionFileState &file_state)
{
	std::vector<Entry> entries;
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
			file_state.get_loaded_files();
	for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
			file = files.begin(); file != files.end(); ++file)
	{
		const QString collection_name = file->get_file().get_file_info().get_display_name(false);
		const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
				file->get_file().get_feature_collection();
		if (!collection.is_valid()) continue;
		for (GPlatesModel::FeatureCollectionHandle::iterator feature = collection->begin();
				feature != collection->end(); ++feature)
		{
			const std::vector<FeatureEventVersioner::EventRecord> events =
					FeatureEventVersioner::events((*feature)->reference());
			for (std::vector<FeatureEventVersioner::EventRecord>::const_iterator event = events.begin();
					event != events.end(); ++event)
			{
				Entry entry;
				entry.category = category_for(*event);
				entry.feature_id = (*feature)->feature_id().get().qstring();
				entry.feature_type = (*feature)->feature_type().get_name().qstring();
				entry.collection = collection_name;
				entry.event = *event;
				entries.push_back(entry);
			}
		}
	}
	std::stable_sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right)
	{
		if (left.event.event_time != right.event.event_time)
			return left.event.event_time > right.event.event_time;
		return left.feature_id < right.feature_id;
	});
	return entries;
}

QString GPlatesViewOperations::GeologyEventLedger::category_name(Category category)
{
	switch (category)
	{
	case OBSERVATION: return "observation";
	case DERIVED_GEOMETRY: return "derived geometry";
	case INTERPRETATION: return "interpretation";
	case MODEL_CHANGE: return "model change";
	}
	return "unknown";
}

QStringList GPlatesViewOperations::GeologyEventLedger::transition_warnings(
		const std::vector<Entry> &entries)
{
	QStringList warnings;
	std::map<QString, double> youngest_by_feature;
	for (std::vector<Entry>::const_iterator entry = entries.begin(); entry != entries.end(); ++entry)
	{
		std::map<QString, double>::iterator previous = youngest_by_feature.find(entry->feature_id);
		if (previous != youngest_by_feature.end() && entry->event.event_time > previous->second + 1e-9)
			warnings.append(QString("%1 has a non-chronological event record at %2 Ma.")
					.arg(entry->feature_id).arg(entry->event.event_time, 0, 'g', 12));
		youngest_by_feature[entry->feature_id] = entry->event.event_time;
		const QString type = entry->event.event_type.toLower();
		if (type.contains("former") && entry->category == OBSERVATION)
			warnings.append(QString("%1 marks a lifecycle transition as an observation; review its category.")
					.arg(entry->feature_id));
	}
	return warnings;
}
