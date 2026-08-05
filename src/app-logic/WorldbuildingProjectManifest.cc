/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#include "WorldbuildingProjectManifest.h"

#include <algorithm>
#include <map>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>


namespace
{
	typedef GPlatesAppLogic::WorldbuildingProjectManifest ManifestType;
	boost::optional<ManifestType::Manifest> active_manifest;
	QString active_project_directory_path;

	const ManifestType::Collection *
	find_collection_for_role(
			const ManifestType::Manifest &manifest,
			const QString &role)
	{
		for (std::vector<ManifestType::Collection>::const_iterator collection =
				manifest.collections.begin(); collection != manifest.collections.end(); ++collection)
		{
			if (collection->role.compare(role, Qt::CaseInsensitive) == 0)
			{
				return &*collection;
			}
		}
		return NULL;
	}

	ManifestType::Collection
	make_collection(
			const char *role,
			const char *file_name,
			const char *display_name,
			const char *feature_type)
	{
		ManifestType::Collection collection;
		collection.role = QString::fromLatin1(role);
		collection.file_name = QString::fromLatin1(file_name);
		collection.display_name = QString::fromLatin1(display_name);
		collection.default_feature_type = QString::fromLatin1(feature_type);
		collection.required = true;
		return collection;
	}

	ManifestType::LayerProfile
	make_layer(
			const char *role,
			int order,
			bool visible,
			const char *draw_style,
			const char *reconstruction_method = "ByPlateId")
	{
		ManifestType::LayerProfile layer;
		layer.collection_role = QString::fromLatin1(role);
		layer.order = order;
		layer.visible = visible;
		layer.draw_style = QString::fromLatin1(draw_style);
		layer.reconstruction_method = QString::fromLatin1(reconstruction_method);
		return layer;
	}

	void
	add_issue(
			ManifestType::Audit &audit,
			ManifestType::IssueSeverity severity,
			const QString &code,
			const QString &message)
	{
		ManifestType::Issue issue;
		issue.severity = severity;
		issue.code = code;
		issue.message = message;
		audit.issues.push_back(issue);
	}

	QString
	severity_name(
			ManifestType::IssueSeverity severity)
	{
		switch (severity)
		{
		case ManifestType::INFO:
			return QString::fromLatin1("INFO");
		case ManifestType::WARNING:
			return QString::fromLatin1("WARNING");
		case ManifestType::BLOCKING:
			return QString::fromLatin1("BLOCKING");
		}
		return QString::fromLatin1("UNKNOWN");
	}
}


bool
GPlatesAppLogic::WorldbuildingProjectManifest::Audit::has_blocking_issues() const
{
	for (std::vector<Issue>::const_iterator issue = issues.begin(); issue != issues.end(); ++issue)
	{
		if (issue->severity == BLOCKING)
		{
			return true;
		}
	}
	return false;
}


QString
GPlatesAppLogic::WorldbuildingProjectManifest::Audit::to_plain_text() const
{
	QStringList lines;
	for (std::vector<Issue>::const_iterator issue = issues.begin(); issue != issues.end(); ++issue)
	{
		lines.append(QString::fromLatin1("[%1] %2: %3")
				.arg(severity_name(issue->severity), issue->code, issue->message));
	}
	if (issues.empty())
	{
		lines.append(QString::fromLatin1("No conflicts found."));
	}
	lines.append(QString());
	lines.append(QString::fromLatin1("Missing managed files: %1")
			.arg(missing_files.isEmpty() ? QString::fromLatin1("none") : missing_files.join(QString::fromLatin1(", "))));
	return lines.join(QString::fromLatin1("\n"));
}


GPlatesAppLogic::WorldbuildingProjectManifest::Manifest
GPlatesAppLogic::WorldbuildingProjectManifest::default_manifest()
{
	Manifest manifest;
	manifest.format_version = CURRENT_FORMAT_VERSION;
	manifest.profile_revision = 1;
	manifest.profile_name = QString::fromLatin1("Worldbuilding Pasta");
	manifest.apply_policy = QString::fromLatin1("missing-only");

	manifest.collections.push_back(make_collection("cratons", "cratons.gpml", "Cratons", "gpml:Craton"));
	manifest.collections.push_back(make_collection("continental-crust", "continents.gpml", "Continental Crust", "gpml:ContinentalCrust"));
	manifest.collections.push_back(make_collection("rifts", "rifts.gpml", "Rifts", "gpml:ContinentalRift"));
	manifest.collections.push_back(make_collection("failed-rifts", "failed rifts.gpml", "Failed Rifts", "gpml:ContinentalRift"));
	manifest.collections.push_back(make_collection("oceanic-crust", "ocean crust.gpml", "Oceanic Crust", "gpml:OceanicCrust"));
	manifest.collections.push_back(make_collection("mors", "mid ocean ridges.gpml", "Mid-Ocean Ridges", "gpml:MidOceanRidge"));
	manifest.collections.push_back(make_collection("transforms", "Transform faults.gpml", "Transforms", "gpml:Transform"));
	manifest.collections.push_back(make_collection("trenches", "subduction zones.gpml", "Trenches", "gpml:SubductionZone"));
	manifest.collections.push_back(make_collection("island-arcs-terranes", "island arcs.gpml", "Island Arcs and Terranes", "gpml:IslandArc"));
	manifest.collections.push_back(make_collection("topologies", "Plates.gpml", "Plate Topologies", "gpml:TopologicalClosedPlateBoundary"));
	manifest.collections.push_back(make_collection("active-orogenies", "active orogenies.gpml", "Active Orogenies", "gpml:OrogenicBelt"));
	manifest.collections.push_back(make_collection("former-orogenies", "former orogenies.gpml", "Former Orogenies", "gpml:OrogenicBelt"));
	manifest.collections.push_back(make_collection("collision-sutures", "collision sutures.gpml", "Collision Sutures", "gpml:Suture"));
	manifest.collections.push_back(make_collection("old-orogenies", "old orogenies.gpml", "Old Orogenies", "gpml:OrogenicBelt"));
	manifest.collections.push_back(make_collection("active-lips", "active LIP.gpml", "Active Large Igneous Provinces", "gpml:LargeIgneousProvince"));
	manifest.collections.push_back(make_collection("former-lips", "former LIP.gpml", "Former Large Igneous Provinces", "gpml:LargeIgneousProvince"));
	manifest.collections.push_back(make_collection("hotspots", "Hotspots.gpml", "Hotspots", "gpml:HotSpot"));
	manifest.collections.push_back(make_collection("hotspot-trails", "Hotspot trails.gpml", "Hotspot Trails", "gpml:MotionPath"));
	manifest.collections.push_back(make_collection("provisional-mors", "provisional mid ocean ridges.gpml", "Provisional Mid-Ocean Ridges", "gpml:MidOceanRidge"));
	manifest.collections.push_back(make_collection("passive-margins", "passive margins.gpml", "Passive Margins", "gpml:PassiveContinentalBoundary"));
	manifest.collections.push_back(make_collection("audit-output", "worldbuilding audit.gpml", "Worldbuilding Audit Output", "gpml:UnclassifiedFeature"));

	for (std::size_t index = 0; index < manifest.collections.size(); ++index)
	{
		const Collection &collection = manifest.collections[index];
		QString style = QString::fromLatin1("Default");
		if (collection.role == QString::fromLatin1("mors"))
		{
			style = QString::fromLatin1("Purple");
		}
		else if (collection.role == QString::fromLatin1("trenches"))
		{
			style = QString::fromLatin1("black");
		}
		else if (collection.role == QString::fromLatin1("rifts"))
		{
			style = QString::fromLatin1("orange");
		}
		else if (collection.role == QString::fromLatin1("failed-rifts"))
		{
			style = QString::fromLatin1("silver");
		}
		else if (collection.role == QString::fromLatin1("active-orogenies"))
		{
			style = QString::fromLatin1("black");
		}
		else if (collection.role == QString::fromLatin1("former-orogenies") ||
				 collection.role == QString::fromLatin1("old-orogenies"))
		{
			style = QString::fromLatin1("Monochrome");
		}
		else if (collection.role == QString::fromLatin1("active-lips") ||
				 collection.role == QString::fromLatin1("former-lips"))
		{
			style = QString::fromLatin1("orange");
		}
		else if (collection.role == QString::fromLatin1("hotspots") ||
				 collection.role == QString::fromLatin1("hotspot-trails"))
		{
			style = QString::fromLatin1("Purple");
		}
		manifest.layers.push_back(make_layer(
				collection.role.toLatin1().constData(),
				static_cast<int>(index),
				collection.role != QString::fromLatin1("audit-output"),
				style.toLatin1().constData()));
	}

	return manifest;
}


QString
GPlatesAppLogic::WorldbuildingProjectManifest::default_file_name()
{
	return QString::fromLatin1("worldbuilding-project.json");
}


void
GPlatesAppLogic::WorldbuildingProjectManifest::activate(
		const QString &project_directory,
		const Manifest &manifest)
{
	active_project_directory_path = QDir(project_directory).absolutePath();
	active_manifest = manifest;
}


bool
GPlatesAppLogic::WorldbuildingProjectManifest::has_active_manifest()
{
	return static_cast<bool>(active_manifest);
}


QString
GPlatesAppLogic::WorldbuildingProjectManifest::active_project_directory()
{
	return active_project_directory_path;
}


QStringList
GPlatesAppLogic::WorldbuildingProjectManifest::active_collection_roles()
{
	QStringList roles;
	if (!active_manifest)
	{
		return roles;
	}
	for (std::vector<Collection>::const_iterator collection =
			active_manifest->collections.begin(); collection != active_manifest->collections.end(); ++collection)
	{
		roles.append(collection->role);
	}
	return roles;
}


QString
GPlatesAppLogic::WorldbuildingProjectManifest::managed_file_path_for_role(
		const QString &role)
{
	if (!active_manifest)
	{
		return QString();
	}
	const Collection *collection = find_collection_for_role(*active_manifest, role);
	if (!collection)
	{
		return QString();
	}
	return QDir(active_project_directory_path).absoluteFilePath(collection->file_name);
}


boost::optional<GPlatesAppLogic::FeatureCollectionFileState::file_reference>
GPlatesAppLogic::WorldbuildingProjectManifest::resolve_loaded_collection(
		const QString &role,
		FeatureCollectionFileState &file_state)
{
	const QString managed_path = managed_file_path_for_role(role);
	if (managed_path.isEmpty())
	{
		return boost::none;
	}
	const QString normalised_managed_path = QDir::cleanPath(
			QFileInfo(managed_path).absoluteFilePath());
	const std::vector<FeatureCollectionFileState::file_reference> loaded_files =
			file_state.get_loaded_files();
	for (std::vector<FeatureCollectionFileState::file_reference>::const_iterator file =
			loaded_files.begin(); file != loaded_files.end(); ++file)
	{
		const QString loaded_path = QDir::cleanPath(
				file->get_file().get_file_info().get_qfileinfo().absoluteFilePath());
		if (!loaded_path.isEmpty() &&
			loaded_path.compare(normalised_managed_path, Qt::CaseInsensitive) == 0)
		{
			return *file;
		}
	}
	return boost::none;
}


bool
GPlatesAppLogic::WorldbuildingProjectManifest::load(
		const QString &file_name,
		Manifest &manifest,
		QString *error_message)
{
	QFile file(file_name);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error_message)
		{
			*error_message = file.errorString();
		}
		return false;
	}

	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
	if (parse_error.error != QJsonParseError::NoError || !document.isObject())
	{
		if (error_message)
		{
			*error_message = parse_error.error != QJsonParseError::NoError
					? parse_error.errorString()
					: QString::fromLatin1("Manifest root must be a JSON object.");
		}
		return false;
	}

	const QJsonObject root = document.object();
	Manifest loaded;
	loaded.format_version = root.value(QString::fromLatin1("format_version")).toInt(0);
	loaded.profile_revision = root.value(QString::fromLatin1("profile_revision")).toInt(0);
	loaded.profile_name = root.value(QString::fromLatin1("profile_name")).toString();
	loaded.apply_policy = root.value(QString::fromLatin1("apply_policy")).toString(QString::fromLatin1("missing-only"));

	const QJsonArray collections = root.value(QString::fromLatin1("collections")).toArray();
	for (QJsonArray::const_iterator entry = collections.begin(); entry != collections.end(); ++entry)
	{
		const QJsonObject object = entry->toObject();
		Collection collection;
		collection.role = object.value(QString::fromLatin1("role")).toString().trimmed();
		collection.file_name = object.value(QString::fromLatin1("file")).toString().trimmed();
		collection.display_name = object.value(QString::fromLatin1("display_name")).toString().trimmed();
		collection.default_feature_type = object.value(QString::fromLatin1("default_feature_type")).toString().trimmed();
		collection.required = object.value(QString::fromLatin1("required")).toBool(true);
		loaded.collections.push_back(collection);
	}

	const QJsonArray layers = root.value(QString::fromLatin1("workspace_profile")).toArray();
	for (QJsonArray::const_iterator entry = layers.begin(); entry != layers.end(); ++entry)
	{
		const QJsonObject object = entry->toObject();
		LayerProfile layer;
		layer.collection_role = object.value(QString::fromLatin1("collection_role")).toString().trimmed();
		layer.order = object.value(QString::fromLatin1("order")).toInt();
		layer.visible = object.value(QString::fromLatin1("visible")).toBool(true);
		layer.draw_style = object.value(QString::fromLatin1("draw_style")).toString();
		layer.reconstruction_method = object.value(QString::fromLatin1("reconstruction_method")).toString();
		const QJsonArray connections = object.value(QString::fromLatin1("raster_connections")).toArray();
		for (QJsonArray::const_iterator connection = connections.begin(); connection != connections.end(); ++connection)
		{
			layer.raster_connections.append(connection->toString());
		}
		loaded.layers.push_back(layer);
	}

	manifest = loaded;
	return true;
}


bool
GPlatesAppLogic::WorldbuildingProjectManifest::save(
		const QString &file_name,
		const Manifest &manifest,
		QString *error_message)
{
	QJsonObject root;
	root.insert(QString::fromLatin1("schema"), QString::fromLatin1("org.gplates.worldbuilding-project"));
	root.insert(QString::fromLatin1("format_version"), manifest.format_version);
	root.insert(QString::fromLatin1("profile_revision"), manifest.profile_revision);
	root.insert(QString::fromLatin1("profile_name"), manifest.profile_name);
	root.insert(QString::fromLatin1("apply_policy"), manifest.apply_policy);

	QJsonArray collections;
	for (std::vector<Collection>::const_iterator collection = manifest.collections.begin();
		 collection != manifest.collections.end(); ++collection)
	{
		QJsonObject object;
		object.insert(QString::fromLatin1("role"), collection->role);
		object.insert(QString::fromLatin1("file"), collection->file_name);
		object.insert(QString::fromLatin1("display_name"), collection->display_name);
		object.insert(QString::fromLatin1("default_feature_type"), collection->default_feature_type);
		object.insert(QString::fromLatin1("required"), collection->required);
		collections.append(object);
	}
	root.insert(QString::fromLatin1("collections"), collections);

	QJsonArray layers;
	for (std::vector<LayerProfile>::const_iterator layer = manifest.layers.begin();
		 layer != manifest.layers.end(); ++layer)
	{
		QJsonObject object;
		object.insert(QString::fromLatin1("collection_role"), layer->collection_role);
		object.insert(QString::fromLatin1("order"), layer->order);
		object.insert(QString::fromLatin1("visible"), layer->visible);
		object.insert(QString::fromLatin1("draw_style"), layer->draw_style);
		object.insert(QString::fromLatin1("reconstruction_method"), layer->reconstruction_method);
		QJsonArray connections;
		for (QStringList::const_iterator connection = layer->raster_connections.begin();
			 connection != layer->raster_connections.end(); ++connection)
		{
			connections.append(*connection);
		}
		object.insert(QString::fromLatin1("raster_connections"), connections);
		layers.append(object);
	}
	root.insert(QString::fromLatin1("workspace_profile"), layers);
	root.insert(QString::fromLatin1("feature_defaults_policy"), QString::fromLatin1("use-only-for-new-features"));
	root.insert(QString::fromLatin1("profile_apply_policy"), QString::fromLatin1("preview-and-confirm; preserve-user-customisations"));

	QSaveFile file(file_name);
	if (!file.open(QIODevice::WriteOnly))
	{
		if (error_message)
		{
			*error_message = file.errorString();
		}
		return false;
	}
	const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
	if (file.write(json) != json.size() || !file.commit())
	{
		if (error_message)
		{
			*error_message = file.errorString();
		}
		return false;
	}
	return true;
}


QString
GPlatesAppLogic::WorldbuildingProjectManifest::default_file_name_for_role(
		const QString &role)
{
	const Manifest defaults = default_manifest();
	for (std::vector<Collection>::const_iterator collection = defaults.collections.begin();
		 collection != defaults.collections.end(); ++collection)
	{
		if (collection->role.compare(role, Qt::CaseInsensitive) == 0)
		{
			return collection->file_name;
		}
	}
	return QString();
}


GPlatesAppLogic::WorldbuildingProjectManifest::Audit
GPlatesAppLogic::WorldbuildingProjectManifest::audit(
		const QString &project_directory,
		const Manifest &manifest)
{
	Audit result;
	if (manifest.format_version > CURRENT_FORMAT_VERSION)
	{
		add_issue(result, BLOCKING, QString::fromLatin1("newer-manifest"),
				QString::fromLatin1("Manifest format %1 is newer than supported format %2.")
						.arg(manifest.format_version).arg(CURRENT_FORMAT_VERSION));
	}
	else if (manifest.format_version < 1)
	{
		add_issue(result, BLOCKING, QString::fromLatin1("invalid-version"),
				QString::fromLatin1("Manifest format_version must be at least 1."));
	}

	std::map<QString, int> role_counts;
	std::map<QString, int> file_counts;
	const QDir directory(project_directory);
	for (std::vector<Collection>::const_iterator collection = manifest.collections.begin();
		 collection != manifest.collections.end(); ++collection)
	{
		const QString normalised_role = collection->role.trimmed().toLower();
		const QString normalised_file = collection->file_name.trimmed().toLower();
		++role_counts[normalised_role];
		++file_counts[normalised_file];

		if (normalised_role.isEmpty() || normalised_file.isEmpty() || QFileInfo(collection->file_name).isAbsolute())
		{
			add_issue(result, BLOCKING, QString::fromLatin1("invalid-collection"),
					QString::fromLatin1("Every collection needs a non-empty role and project-relative file name."));
			continue;
		}

		const QString default_name = default_file_name_for_role(collection->role);
		if (!default_name.isEmpty() && default_name.compare(collection->file_name, Qt::CaseInsensitive) != 0)
		{
			add_issue(result, INFO, QString::fromLatin1("renamed-collection"),
					QString::fromLatin1("Role '%1' uses '%2' instead of the default '%3'.")
							.arg(collection->role, collection->file_name, default_name));
		}

		if (collection->required && !QFileInfo::exists(directory.filePath(collection->file_name)))
		{
			result.missing_files.append(collection->file_name);
		}
	}

	for (std::map<QString, int>::const_iterator role = role_counts.begin(); role != role_counts.end(); ++role)
	{
		if (!role->first.isEmpty() && role->second > 1)
		{
			add_issue(result, BLOCKING, QString::fromLatin1("duplicate-role"),
					QString::fromLatin1("Collection role '%1' is assigned %2 times.").arg(role->first).arg(role->second));
		}
	}
	for (std::map<QString, int>::const_iterator file = file_counts.begin(); file != file_counts.end(); ++file)
	{
		if (!file->first.isEmpty() && file->second > 1)
		{
			add_issue(result, BLOCKING, QString::fromLatin1("duplicate-file"),
					QString::fromLatin1("Collection file '%1' is assigned %2 roles.").arg(file->first).arg(file->second));
		}
	}

	std::map<QString, int> layer_roles;
	for (std::vector<LayerProfile>::const_iterator layer = manifest.layers.begin();
		 layer != manifest.layers.end(); ++layer)
	{
		++layer_roles[layer->collection_role.trimmed().toLower()];
		if (role_counts.find(layer->collection_role.trimmed().toLower()) == role_counts.end())
		{
			add_issue(result, WARNING, QString::fromLatin1("unknown-layer-role"),
					QString::fromLatin1("Workspace layer references unknown role '%1'.").arg(layer->collection_role));
		}
	}
	for (std::map<QString, int>::const_iterator role = layer_roles.begin(); role != layer_roles.end(); ++role)
	{
		if (!role->first.isEmpty() && role->second > 1)
		{
			add_issue(result, WARNING, QString::fromLatin1("duplicate-layer-role"),
					QString::fromLatin1("Workspace profile contains %1 entries for role '%2'.").arg(role->second).arg(role->first));
		}
	}

	return result;
}
