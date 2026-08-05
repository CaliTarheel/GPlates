/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_VIEWOPERATIONS_FEATUREEVENTVERSIONER_H
#define GPLATES_VIEWOPERATIONS_FEATUREEVENTVERSIONER_H

#include <vector>

#include <boost/optional.hpp>

#include <QString>
#include <QStringList>

#include "model/FeatureHandle.h"
#include "model/TopLevelProperty.h"


namespace GPlatesViewOperations
{
	/**
	 * Shared valid-time and lineage encoding for reviewed plate events.
	 *
	 * Records are stored in gml:description as compact JSON prefixed with a stable
	 * marker. This keeps them GPML-compatible and visible to older GPlates builds.
	 */
	class FeatureEventVersioner
	{
	public:
		typedef std::vector<GPlatesModel::TopLevelProperty::non_null_ptr_type> property_seq_type;

		enum TimeBoundary
		{
			START_AT_EVENT,
			END_AT_EVENT,
			KEEP_VALID_TIME
		};

		struct EventRecord
		{
			QString event_type;
			double event_time;
			QString operation_version;
			boost::optional<qulonglong> seed;
			QString relation;
			QStringList source_feature_ids;
			QStringList output_feature_ids;
		};

		static EventRecord
		make_event(
				const QString &event_type,
				double event_time,
				const QString &operation_version,
				const QStringList &source_feature_ids,
				const QStringList &output_feature_ids,
				const QString &relation,
				boost::optional<qulonglong> seed = boost::none);

		static property_seq_type
		clone_properties(
				const GPlatesModel::FeatureHandle &feature);

		static property_seq_type
		properties_for_event(
				const GPlatesModel::FeatureHandle::weak_ref &feature,
				TimeBoundary boundary,
				const EventRecord &event);

		static void
		set_properties(
				const GPlatesModel::FeatureHandle::weak_ref &feature,
				const property_seq_type &properties);

		static std::vector<EventRecord>
		events(
				const GPlatesModel::FeatureHandle::weak_ref &feature);

		static QString
		encode_event(
				const EventRecord &event);

		static boost::optional<EventRecord>
		decode_event(
				const QString &line);

		static QString
		description_text(
				const GPlatesModel::FeatureHandle::weak_ref &feature);

		static QString
		event_prefix();
	};
}

#endif // GPLATES_VIEWOPERATIONS_FEATUREEVENTVERSIONER_H
