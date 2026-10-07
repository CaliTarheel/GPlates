/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The GPlates developers
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <gtest/gtest.h>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "app-logic/PlanetaryParameters.h"
#include "app-logic/ProjectDocumentRegistry.h"
#include "app-logic/ProjectMetadata.h"

#include "maths/CalculateVelocity.h"
#include "maths/FiniteRotation.h"
#include "maths/LatLonPoint.h"
#include "maths/MathsUtils.h"
#include "maths/PointOnSphere.h"

#include "utils/Earth.h"


namespace
{
	void
	write_utf8_file(
			const QString &file_path,
			const QString &contents)
	{
		QFile file(file_path);
		ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << file.errorString().toStdString();
		const QByteArray utf8_contents = contents.toUtf8();
		ASSERT_EQ(file.write(utf8_contents), utf8_contents.size())
				<< file.errorString().toStdString();
	}
}


TEST(ProjectMetadataTest, front_matter_parsing)
{
	const GPlatesAppLogic::ProjectMetadata no_front_matter =
			GPlatesAppLogic::ProjectMetadataParser::parse("# Notes\n");
	EXPECT_FALSE(no_front_matter.has_front_matter);
	EXPECT_TRUE(no_front_matter.is_valid);
	EXPECT_FALSE(no_front_matter.planet_radius_metres);

	const GPlatesAppLogic::ProjectMetadata delimiter_not_on_first_line =
			GPlatesAppLogic::ProjectMetadataParser::parse("# Notes\n---\ngplates:\n  planet:\n    radius_m: 1\n---\n");
	EXPECT_FALSE(delimiter_not_on_first_line.has_front_matter);

	const GPlatesAppLogic::ProjectMetadata integer_radius =
			GPlatesAppLogic::ProjectMetadataParser::parse(
					"---\n"
					"gplates:\n"
					"  planet:\n"
					"    radius_m: 6900000 # metres\n"
					"  unknown_key: true\n"
					"---\n");
	ASSERT_TRUE(integer_radius.planet_radius_metres);
	EXPECT_TRUE(integer_radius.is_valid);
	EXPECT_DOUBLE_EQ(integer_radius.planet_radius_metres.get(), 6900000.0);

	const GPlatesAppLogic::ProjectMetadata floating_radius =
			GPlatesAppLogic::ProjectMetadataParser::parse(
					"---\n"
					"gplates:\n"
					"  schema_version: 1\n"
					"  planet:\n"
					"    radius_m: 6900000.25\n"
					"---\n");
	ASSERT_TRUE(floating_radius.planet_radius_metres);
	EXPECT_DOUBLE_EQ(floating_radius.planet_radius_metres.get(), 6900000.25);
}


TEST(ProjectMetadataTest, front_matter_mapping_keys)
{
	const QString markdown =
			"---\n"
			"gplates:\n"
			"  planet:\n"
			"    radius_m: 6900000\n"
			"%1: ignored\n"
			"---\n";
	const QStringList valid_keys = QStringList() << "a" << "Z" << "_" << "Key_2-value";
	for (const QString &key : valid_keys)
	{
		SCOPED_TRACE(key.toStdString());
		const GPlatesAppLogic::ProjectMetadata metadata =
				GPlatesAppLogic::ProjectMetadataParser::parse(markdown.arg(key));
		EXPECT_TRUE(metadata.has_front_matter);
		ASSERT_TRUE(metadata.is_valid) << metadata.diagnostic.toStdString();
		ASSERT_TRUE(metadata.planet_radius_metres);
		EXPECT_DOUBLE_EQ(metadata.planet_radius_metres.get(), 6900000.0);
	}

	const QStringList invalid_keys = QStringList()
			<< "" << "1key" << "-key" << "key!" << "bad.key" << "key name"
			<< QString("cl") + QChar(0x00e9);
	for (const QString &key : invalid_keys)
	{
		SCOPED_TRACE(key.toStdString());
		const GPlatesAppLogic::ProjectMetadata metadata =
				GPlatesAppLogic::ProjectMetadataParser::parse(markdown.arg(key));
		EXPECT_TRUE(metadata.has_front_matter);
		EXPECT_FALSE(metadata.is_valid);
		EXPECT_FALSE(metadata.planet_radius_metres);
		EXPECT_FALSE(metadata.diagnostic.isEmpty());
	}
}


TEST(ProjectMetadataTest, front_matter_line_endings_and_spacing)
{
	const QString markdown = QString(QChar(0xfeff)) +
			"  ---  \r\n"
			"# Project metadata\r\n"
			"gplates :  \r\n"
			"  schema_version : 1  # Supported schema\r\n"
			"  \r\n"
			"  planet : \r\n"
			"    radius_m : 6900000.25  # Metres\r\n"
			"  ---  \r\n"
			"# Notes\r\n";
	const GPlatesAppLogic::ProjectMetadata metadata =
			GPlatesAppLogic::ProjectMetadataParser::parse(markdown);
	EXPECT_TRUE(metadata.has_front_matter);
	ASSERT_TRUE(metadata.is_valid) << metadata.diagnostic.toStdString();
	ASSERT_TRUE(metadata.planet_radius_metres);
	EXPECT_DOUBLE_EQ(metadata.planet_radius_metres.get(), 6900000.25);
}


TEST(ProjectMetadataTest, invalid_front_matter)
{
	const QStringList invalid_documents = QStringList()
			<< "---\ngplates:\n  planet:\n    radius_m: 0\n---\n"
			<< "---\ngplates:\n  planet:\n    radius_m: -1\n---\n"
			<< "---\ngplates:\n  planet:\n    radius_m: not-a-number\n---\n"
			<< "---\ngplates:\n  schema_version: 2\n  planet:\n    radius_m: 1\n---\n"
			<< "---\ngplates:\n planet:\n    radius_m: 1\n---\n"
			<< "---\ngplates: { planet: { radius_m: 1 } }\n---\n"
			<< "---\ngplates:\n  planet:\n    radius_m: [1]\n---\n"
			<< "---\ngplates:\n  planet:\n    radius_m: .inf\n---\n"
			<< "---\ngplates:\n  planet:\n    radius_m: nan\n---\n"
			<< "---\nradius: 6900000\n---\n"
			<< "---\ngplates:\n  planet:\n    radius_m: 1\n    radius_m: 2\n---\n"
			<< "---\ngplates:\n  planet:\n    radius_m: 1\n";

	for (int index = 0; index < invalid_documents.size(); ++index)
	{
		SCOPED_TRACE(invalid_documents[index].toStdString());
		const GPlatesAppLogic::ProjectMetadata metadata =
				GPlatesAppLogic::ProjectMetadataParser::parse(invalid_documents[index]);
		EXPECT_TRUE(metadata.has_front_matter);
		EXPECT_FALSE(metadata.is_valid);
		EXPECT_FALSE(metadata.diagnostic.isEmpty());
	}
}


TEST(ProjectMetadataTest, document_registry_restoration)
{
	QTemporaryDir temporary_directory;
	ASSERT_TRUE(temporary_directory.isValid());
	const QString project_path = QDir(temporary_directory.path()).filePath("project.md");
	const QString notes_path = QDir(temporary_directory.path()).filePath("notes.md");
	const QString missing_path = QDir(temporary_directory.path()).filePath("missing.md");
	ASSERT_NO_FATAL_FAILURE(write_utf8_file(project_path, "# Project\n"));
	ASSERT_NO_FATAL_FAILURE(write_utf8_file(notes_path, "# Notes\n"));

	GPlatesAppLogic::ProjectDocumentRegistry source_registry;
	EXPECT_EQ(source_registry.add_document(project_path, "Project overview"), 0);
	EXPECT_EQ(source_registry.add_document(notes_path), 1);
	EXPECT_EQ(source_registry.add_document(missing_path), 2);
	// Adding an existing path returns its existing record instead of creating
	// an ambiguous duplicate/second primary document.
	EXPECT_EQ(source_registry.add_document(project_path), 0);
	EXPECT_EQ(source_registry.document_count(), 3);
	ASSERT_TRUE(source_registry.set_primary_document(1));
	ASSERT_TRUE(source_registry.move_document(2, 0));
	ASSERT_TRUE(source_registry.primary_document_index());
	EXPECT_EQ(source_registry.primary_document_index().get(), 2);

	GPlatesAppLogic::ProjectDocumentRegistry restored_registry;
	restored_registry.restore_documents(
			source_registry.file_paths(),
			source_registry.display_names(),
			source_registry.primary_document_index());
	ASSERT_EQ(restored_registry.document_count(), 3);
	EXPECT_EQ(
			restored_registry.document_at(0).file_path,
			QDir::cleanPath(QFileInfo(missing_path).absoluteFilePath()));
	EXPECT_FALSE(QFileInfo::exists(restored_registry.document_at(0).file_path));
	EXPECT_EQ(restored_registry.document_at(1).display_name, QString("Project overview"));
	ASSERT_TRUE(restored_registry.primary_document_index());
	EXPECT_EQ(restored_registry.primary_document_index().get(), 2);

	ASSERT_TRUE(restored_registry.remove_document(1));
	ASSERT_TRUE(restored_registry.primary_document_index());
	EXPECT_EQ(restored_registry.primary_document_index().get(), 1);
	ASSERT_TRUE(restored_registry.remove_document(1));
	EXPECT_FALSE(restored_registry.primary_document_index());
}


TEST(ProjectMetadataTest, invalid_metadata_fallback)
{
	QTemporaryDir temporary_directory;
	ASSERT_TRUE(temporary_directory.isValid());
	const QString invalid_path = QDir(temporary_directory.path()).filePath("invalid.md");
	const QString valid_path = QDir(temporary_directory.path()).filePath("valid.md");
	ASSERT_NO_FATAL_FAILURE(write_utf8_file(
			invalid_path,
			"---\ngplates:\n  planet:\n    radius_m: -1\n---\n"));
	ASSERT_NO_FATAL_FAILURE(write_utf8_file(
			valid_path,
			"---\ngplates:\n  planet:\n    radius_m: 8000000\n---\n"));

	GPlatesAppLogic::ProjectDocumentRegistry registry;
	GPlatesAppLogic::PlanetaryParameters parameters(registry);
	EXPECT_EQ(parameters.radius_source(), GPlatesAppLogic::PlanetaryParameters::EARTH_DEFAULT);
	EXPECT_TRUE(parameters.radius_diagnostic().isEmpty());
	EXPECT_DOUBLE_EQ(
			parameters.effective_radius_metres(),
			GPlatesUtils::Earth::EQUATORIAL_RADIUS_KMS * 1000.0);

	const int invalid_index = registry.add_document(invalid_path);
	const int valid_index = registry.add_document(valid_path);
	EXPECT_EQ(
			parameters.radius_source(),
			GPlatesAppLogic::PlanetaryParameters::INVALID_PROJECT_METADATA_USING_EARTH_DEFAULT);
	EXPECT_FALSE(parameters.radius_diagnostic().isEmpty());
	EXPECT_DOUBLE_EQ(
			parameters.effective_radius_metres(),
			GPlatesUtils::Earth::EQUATORIAL_RADIUS_KMS * 1000.0);

	ASSERT_TRUE(registry.set_primary_document(valid_index));
	EXPECT_EQ(parameters.radius_source(), GPlatesAppLogic::PlanetaryParameters::PROJECT_MARKDOWN);
	EXPECT_DOUBLE_EQ(parameters.effective_radius_metres(), 8000000.0);
	ASSERT_TRUE(registry.set_primary_document(invalid_index));
	EXPECT_EQ(
			parameters.radius_source(),
			GPlatesAppLogic::PlanetaryParameters::INVALID_PROJECT_METADATA_USING_EARTH_DEFAULT);
}


TEST(ProjectMetadataTest, document_registry_and_planetary_parameters)
{
	QTemporaryDir temporary_directory;
	ASSERT_TRUE(temporary_directory.isValid());
	const QString first_path = QDir(temporary_directory.path()).filePath("project.md");
	const QString second_path = QDir(temporary_directory.path()).filePath("notes.md");
	ASSERT_NO_FATAL_FAILURE(write_utf8_file(
			first_path,
			"---\ngplates:\n  planet:\n    radius_m: 7000000\n---\n"));
	ASSERT_NO_FATAL_FAILURE(write_utf8_file(second_path, "# Notes\n"));

	GPlatesAppLogic::ProjectDocumentRegistry registry;
	GPlatesAppLogic::PlanetaryParameters parameters(registry);
	const int first_index = registry.add_document(first_path);
	const int second_index = registry.add_document(second_path);
	EXPECT_EQ(registry.document_count(), 2);
	ASSERT_TRUE(registry.primary_document_index());
	EXPECT_EQ(registry.primary_document_index().get(), first_index);
	EXPECT_EQ(parameters.radius_source(), GPlatesAppLogic::PlanetaryParameters::PROJECT_MARKDOWN);
	EXPECT_DOUBLE_EQ(parameters.effective_radius_metres(), 7000000.0);

	ASSERT_TRUE(registry.load_document(first_index));
	registry.set_document_text(first_index, "---\ngplates:\n  planet:\n    radius_m: 7100000\n---\n");
	EXPECT_TRUE(registry.has_dirty_documents());
	ASSERT_TRUE(registry.save_document(first_index));
	EXPECT_FALSE(registry.has_dirty_documents());
	EXPECT_DOUBLE_EQ(parameters.effective_radius_metres(), 7100000.0);

	registry.set_primary_document(second_index);
	EXPECT_EQ(parameters.radius_source(), GPlatesAppLogic::PlanetaryParameters::EARTH_DEFAULT);
	registry.remove_document(second_index);
	EXPECT_FALSE(registry.primary_document_index());
	EXPECT_DOUBLE_EQ(
			parameters.effective_radius_metres(),
			GPlatesUtils::Earth::EQUATORIAL_RADIUS_KMS * 1000.0);
}


TEST(ProjectMetadataTest, radius_scaling)
{
	const GPlatesMaths::PointOnSphere point_a =
			GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(0, 0));
	const GPlatesMaths::PointOnSphere point_b =
			GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(0, 90));
	const double distance_at_radius_1 =
			GPlatesMaths::calculate_distance_on_surface_of_sphere(point_a, point_b, 1.0).dval();
	const double distance_at_radius_2 =
			GPlatesMaths::calculate_distance_on_surface_of_sphere(point_a, point_b, 2.0).dval();
	EXPECT_NEAR(distance_at_radius_2 / distance_at_radius_1, 2.0, 2e-12);

	const GPlatesMaths::FiniteRotation stage_rotation = GPlatesMaths::FiniteRotation::create(
			GPlatesMaths::PointOnSphere::north_pole,
			GPlatesMaths::convert_deg_to_rad(1.0));
	const double velocity_at_radius_1 =
			GPlatesMaths::calculate_velocity_vector(point_a, stage_rotation, 1.0, 1000.0).magnitude().dval();
	const double velocity_at_radius_2 =
			GPlatesMaths::calculate_velocity_vector(point_a, stage_rotation, 1.0, 2000.0).magnitude().dval();
	EXPECT_NEAR(velocity_at_radius_2 / velocity_at_radius_1, 2.0, 2e-12);
}
