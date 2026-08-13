/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_VIEWOPERATIONS_GEOLOGYEVENTLEDGER_H
#define GPLATES_VIEWOPERATIONS_GEOLOGYEVENTLEDGER_H

#include <vector>
#include <QString>
#include <QStringList>
#include "view-operations/FeatureEventVersioner.h"

namespace GPlatesAppLogic { class FeatureCollectionFileState; }

namespace GPlatesViewOperations
{
	/** Chronological, read-only geological observation and derivation ledger. */
	class GeologyEventLedger
	{
	public:
		enum Category { OBSERVATION, DERIVED_GEOMETRY, INTERPRETATION, MODEL_CHANGE };
		struct Entry
		{
			Category category;
			QString feature_id;
			QString feature_type;
			QString collection;
			FeatureEventVersioner::EventRecord event;
		};
		static std::vector<Entry> build(GPlatesAppLogic::FeatureCollectionFileState &file_state);
		static QString category_name(Category category);
		static QStringList transition_warnings(const std::vector<Entry> &entries);
	};
}
#endif
