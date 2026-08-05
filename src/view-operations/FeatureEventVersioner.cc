/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#include "FeatureEventVersioner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "feature-visitors/PropertyValueFinder.h"

#include "model/ModelUtils.h"
#include "model/PropertyName.h"
#include "model/TopLevelPropertyInline.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	GPlatesModel::TopLevelProperty::non_null_ptr_type
	make_property(
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		const boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> property =
				GPlatesModel::ModelUtils::create_top_level_property(name, value);
		if (property)
		{
			return *property;
		}
		return GPlatesModel::TopLevelPropertyInline::create(name, value);
	}

	QJsonArray
	to_json_array(
			const QStringList &values)
	{
		QJsonArray array;
		for (QStringList::const_iterator value = values.begin(); value != values.end(); ++value)
		{
			array.append(*value);
		}
		return array;
	}

	QStringList
	from_json_array(
			const QJsonArray &array)
	{
		QStringList values;
		for (QJsonArray::const_iterator value = array.begin(); value != array.end(); ++value)
		{
			values.append(value->toString());
		}
		return values;
	}
}


GPlatesViewOperations::FeatureEventVersioner::EventRecord
GPlatesViewOperations::FeatureEventVersioner::make_event(
		const QString &event_type,
		double event_time,
		const QString &operation_version,
		const QStringList &source_feature_ids,
		const QStringList &output_feature_ids,
		const QString &relation,
		boost::optional<qulonglong> seed)
{
	EventRecord event;
	event.event_type = event_type;
	event.event_time = event_time;
	event.operation_version = operation_version;
	event.source_feature_ids = source_feature_ids;
	event.output_feature_ids = output_feature_ids;
	event.relation = relation;
	event.seed = seed;
	return event;
}


GPlatesViewOperations::FeatureEventVersioner::property_seq_type
GPlatesViewOperations::FeatureEventVersioner::clone_properties(
		const GPlatesModel::FeatureHandle &feature)
{
	property_seq_type properties;
	for (GPlatesModel::FeatureHandle::const_iterator property = feature.begin();
		 property != feature.end(); ++property)
	{
		properties.push_back((*property)->clone());
	}
	return properties;
}


QString
GPlatesViewOperations::FeatureEventVersioner::event_prefix()
{
	return QString::fromLatin1("[WorldbuildingEvent] ");
}


QString
GPlatesViewOperations::FeatureEventVersioner::encode_event(
		const EventRecord &event)
{
	QJsonObject object;
	object.insert(QString::fromLatin1("event_type"), event.event_type);
	object.insert(QString::fromLatin1("event_time_ma"), event.event_time);
	object.insert(QString::fromLatin1("operation_version"), event.operation_version);
	object.insert(QString::fromLatin1("relation"), event.relation);
	object.insert(QString::fromLatin1("source_feature_ids"), to_json_array(event.source_feature_ids));
	object.insert(QString::fromLatin1("output_feature_ids"), to_json_array(event.output_feature_ids));
	if (event.seed)
	{
		object.insert(QString::fromLatin1("seed"), QString::number(*event.seed));
	}
	return event_prefix() + QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}


boost::optional<GPlatesViewOperations::FeatureEventVersioner::EventRecord>
GPlatesViewOperations::FeatureEventVersioner::decode_event(
		const QString &line)
{
	const int prefix_index = line.indexOf(event_prefix());
	if (prefix_index < 0)
	{
		return boost::none;
	}
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(
			line.mid(prefix_index + event_prefix().size()).trimmed().toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
	{
		return boost::none;
	}
	const QJsonObject object = document.object();
	EventRecord event;
	event.event_type = object.value(QString::fromLatin1("event_type")).toString();
	event.event_time = object.value(QString::fromLatin1("event_time_ma")).toDouble();
	event.operation_version = object.value(QString::fromLatin1("operation_version")).toString();
	event.relation = object.value(QString::fromLatin1("relation")).toString();
	event.source_feature_ids = from_json_array(object.value(QString::fromLatin1("source_feature_ids")).toArray());
	event.output_feature_ids = from_json_array(object.value(QString::fromLatin1("output_feature_ids")).toArray());
	const QString seed = object.value(QString::fromLatin1("seed")).toString();
	if (!seed.isEmpty())
	{
		bool ok = false;
		const qulonglong value = seed.toULongLong(&ok);
		if (ok)
		{
			event.seed = value;
		}
	}
	return event;
}


QString
GPlatesViewOperations::FeatureEventVersioner::description_text(
		const GPlatesModel::FeatureHandle::weak_ref &feature)
{
	if (!feature.is_valid())
	{
		return QString();
	}
	const GPlatesModel::PropertyName description_name =
			GPlatesModel::PropertyName::create_gml("description");
	const boost::optional<GPlatesPropertyValues::XsString::non_null_ptr_to_const_type> description =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::XsString>(
					feature, description_name);
	return description
			? GPlatesUtils::make_qstring_from_icu_string((*description)->get_value().get())
			: QString();
}


GPlatesViewOperations::FeatureEventVersioner::property_seq_type
GPlatesViewOperations::FeatureEventVersioner::properties_for_event(
		const GPlatesModel::FeatureHandle::weak_ref &feature,
		TimeBoundary boundary,
		const EventRecord &event)
{
	property_seq_type properties;
	if (!feature.is_valid())
	{
		return properties;
	}

	const GPlatesModel::PropertyName valid_time_name =
			GPlatesModel::PropertyName::create_gml("validTime");
	const GPlatesModel::PropertyName description_name =
			GPlatesModel::PropertyName::create_gml("description");
	GPlatesPropertyValues::GeoTimeInstant begin =
			GPlatesPropertyValues::GeoTimeInstant::create_distant_past();
	GPlatesPropertyValues::GeoTimeInstant end =
			GPlatesPropertyValues::GeoTimeInstant::create_distant_future();
	const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type> valid_time =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GmlTimePeriod>(
					feature, valid_time_name);
	if (valid_time)
	{
		begin = (*valid_time)->begin()->get_time_position();
		end = (*valid_time)->end()->get_time_position();
	}
	if (boundary == START_AT_EVENT)
	{
		begin = GPlatesPropertyValues::GeoTimeInstant(event.event_time);
	}
	else if (boundary == END_AT_EVENT)
	{
		end = GPlatesPropertyValues::GeoTimeInstant(event.event_time);
	}

	for (GPlatesModel::FeatureHandle::const_iterator property = feature->begin();
		 property != feature->end(); ++property)
	{
		if ((*property)->get_property_name() != valid_time_name &&
			(*property)->get_property_name() != description_name)
		{
			properties.push_back((*property)->clone());
		}
	}
	if (boundary == KEEP_VALID_TIME)
	{
		for (GPlatesModel::FeatureHandle::const_iterator property = feature->begin();
			 property != feature->end(); ++property)
		{
			if ((*property)->get_property_name() == valid_time_name)
			{
				properties.push_back((*property)->clone());
			}
		}
	}
	else
	{
		properties.push_back(make_property(
				valid_time_name,
				GPlatesModel::ModelUtils::create_gml_time_period(begin, end)));
	}

	QString description = description_text(feature).trimmed();
	if (!description.isEmpty())
	{
		description.append(QString::fromLatin1("\n"));
	}
	description.append(encode_event(event));
	properties.push_back(make_property(
			description_name,
			GPlatesPropertyValues::XsString::create(
					GPlatesUtils::make_icu_string_from_qstring(description))));
	return properties;
}


void
GPlatesViewOperations::FeatureEventVersioner::set_properties(
		const GPlatesModel::FeatureHandle::weak_ref &feature,
		const property_seq_type &properties)
{
	if (!feature.is_valid())
	{
		return;
	}
	while (feature->begin() != feature->end())
	{
		feature->remove(feature->begin());
	}
	for (property_seq_type::const_iterator property = properties.begin();
		 property != properties.end(); ++property)
	{
		feature->add((*property)->clone());
	}
}


std::vector<GPlatesViewOperations::FeatureEventVersioner::EventRecord>
GPlatesViewOperations::FeatureEventVersioner::events(
		const GPlatesModel::FeatureHandle::weak_ref &feature)
{
	std::vector<EventRecord> records;
	const QStringList lines = description_text(feature).split(QChar('\n'));
	for (QStringList::const_iterator line = lines.begin(); line != lines.end(); ++line)
	{
		const boost::optional<EventRecord> event = decode_event(*line);
		if (event)
		{
			records.push_back(*event);
		}
	}
	return records;
}
