/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#include "unit-test/WorldbuildingProjectManifestTest.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "app-logic/WorldbuildingProjectManifest.h"


GPlatesUnitTest::WorldbuildingProjectManifestTestSuite::WorldbuildingProjectManifestTestSuite(
		unsigned depth) :
	GPlatesTestSuite("WorldbuildingProjectManifestTestSuite")
{
	init(depth);
}


void
GPlatesUnitTest::WorldbuildingProjectManifestTestSuite::construct_maps()
{
	boost::shared_ptr<WorldbuildingProjectManifestTest> instance(
			new WorldbuildingProjectManifestTest());
	ADD_TESTCASE(WorldbuildingProjectManifestTest, test_default_manifest_round_trip);
	ADD_TESTCASE(WorldbuildingProjectManifestTest, test_missing_files_and_renames);
	ADD_TESTCASE(WorldbuildingProjectManifestTest, test_duplicate_roles_and_newer_versions_block);
}


void
GPlatesUnitTest::WorldbuildingProjectManifestTest::test_default_manifest_round_trip()
{
	typedef GPlatesAppLogic::WorldbuildingProjectManifest ManifestType;
	QTemporaryDir directory;
	BOOST_REQUIRE(directory.isValid());
	const QString path = QDir(directory.path()).filePath(ManifestType::default_file_name());
	const ManifestType::Manifest source = ManifestType::default_manifest();
	QString error;
	BOOST_REQUIRE_MESSAGE(ManifestType::save(path, source, &error), error.toStdString());

	ManifestType::Manifest loaded;
	BOOST_REQUIRE_MESSAGE(ManifestType::load(path, loaded, &error), error.toStdString());
	BOOST_CHECK_EQUAL(loaded.format_version, ManifestType::CURRENT_FORMAT_VERSION);
	BOOST_CHECK_EQUAL(loaded.collections.size(), 21);
	BOOST_CHECK_EQUAL(loaded.layers.size(), loaded.collections.size());
	BOOST_CHECK_EQUAL(loaded.apply_policy.toStdString(), "missing-only");
	ManifestType::activate(directory.path(), loaded);
	BOOST_CHECK(ManifestType::has_active_manifest());
	BOOST_CHECK_EQUAL(ManifestType::active_collection_roles().size(), 21);
	BOOST_CHECK_EQUAL(
			QDir::cleanPath(ManifestType::managed_file_path_for_role(QString::fromLatin1("mors"))).toStdString(),
			QDir::cleanPath(QDir(directory.path()).filePath(QString::fromLatin1("mid ocean ridges.gpml"))).toStdString());
}


void
GPlatesUnitTest::WorldbuildingProjectManifestTest::test_missing_files_and_renames()
{
	typedef GPlatesAppLogic::WorldbuildingProjectManifest ManifestType;
	QTemporaryDir directory;
	BOOST_REQUIRE(directory.isValid());
	ManifestType::Manifest manifest = ManifestType::default_manifest();
	manifest.collections.front().file_name = QString::fromLatin1("my stable cores.gpml");

	QFile existing(QDir(directory.path()).filePath(manifest.collections.back().file_name));
	BOOST_REQUIRE(existing.open(QIODevice::WriteOnly));
	existing.close();

	const ManifestType::Audit audit = ManifestType::audit(directory.path(), manifest);
	BOOST_CHECK(!audit.has_blocking_issues());
	BOOST_CHECK_EQUAL(audit.missing_files.size(), 20);
	BOOST_CHECK(audit.to_plain_text().contains(QString::fromLatin1("renamed-collection")));
	BOOST_CHECK(audit.missing_files.contains(QString::fromLatin1("my stable cores.gpml")));
}


void
GPlatesUnitTest::WorldbuildingProjectManifestTest::test_duplicate_roles_and_newer_versions_block()
{
	typedef GPlatesAppLogic::WorldbuildingProjectManifest ManifestType;
	QTemporaryDir directory;
	BOOST_REQUIRE(directory.isValid());
	ManifestType::Manifest manifest = ManifestType::default_manifest();
	manifest.format_version = ManifestType::CURRENT_FORMAT_VERSION + 1;
	manifest.collections[1].role = manifest.collections[0].role;

	const ManifestType::Audit audit = ManifestType::audit(directory.path(), manifest);
	BOOST_CHECK(audit.has_blocking_issues());
	BOOST_CHECK(audit.to_plain_text().contains(QString::fromLatin1("newer-manifest")));
	BOOST_CHECK(audit.to_plain_text().contains(QString::fromLatin1("duplicate-role")));
}
