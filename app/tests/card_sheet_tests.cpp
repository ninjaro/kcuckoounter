// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/card_sheet_tests.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"

#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSet>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest/QtTest>

static QString alternate_svg_id_for_test(const QString& logical_id) {
    static const QStringList suits = {
        str_label("club"),
        str_label("diamond"),
        str_label("heart"),
        str_label("spade"),
    };

    if (logical_id == str_label("back")) {
        return str_label("card_back");
    }
    if (logical_id == str_label("joker_black")) {
        return str_label("black_joker");
    }
    if (logical_id == str_label("joker_red")) {
        return str_label("red_joker");
    }

    const QStringList parts = logical_id.split(QChar('_'), Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return logical_id;
    }

    if (suits.contains(parts.at(0))) {
        return str_label("%1_%2").arg(parts.at(1), parts.at(0));
    }
    if (suits.contains(parts.at(1))) {
        return str_label("%1_%2").arg(parts.at(1), parts.at(0));
    }
    return logical_id;
}

static bool write_alternate_id_card_svg(const QString& source_path) {
    QFile file(source_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
              "width=\"80\" height=\"120\" viewBox=\"0 0 80 120\">\n";
    stream << "<g id=\"card_base\"><rect x=\"0\" y=\"0\" width=\"63\" "
              "height=\"88\" fill=\"#f9f9f9\"/></g>\n";

    QSet<QString> written_ids;
    for (const QString& logical_id : required_card_ids_with_back()) {
        const QString svg_id = alternate_svg_id_for_test(logical_id);
        if (svg_id.isEmpty() || written_ids.contains(svg_id)) {
            continue;
        }
        written_ids.insert(svg_id);
        stream << "<g id=\"" << svg_id
               << "\"><rect x=\"0\" y=\"0\" width=\"63\" height=\"88\" "
                  "fill=\"#ffffff\" stroke=\"#111111\"/></g>\n";
    }

    stream << "</svg>\n";
    return stream.status() == QTextStream::Ok;
}

void card_sheet_tests::loads_svg() {
    const QString source = card_sheet_source_path();
    QFileInfo info(source);
    QVERIFY2(info.exists(), "card sheet svg missing");
    QVERIFY2(info.isFile(), "card sheet path is not a file");

    QSvgRenderer renderer(source);
    QVERIFY2(renderer.isValid(), "card sheet svg failed to load");
}

void card_sheet_tests::contains_expected_elements() {
    const QString source = card_sheet_source_path();
    QSvgRenderer renderer(source);
    QVERIFY2(renderer.isValid(), "card sheet svg failed to load");

    const auto& element_ids = card_element_ids();
    QVERIFY2(!element_ids.isEmpty(), "card sheet element id list is empty");

    QSet<QString> seen;
    for (const QString& element_id : element_ids) {
        QVERIFY2(!element_id.isEmpty(), "element id is empty");
        QVERIFY2(renderer.elementExists(element_id), "element id missing");
        QVERIFY2(!seen.contains(element_id), "duplicate element id");
        seen.insert(element_id);
    }

    QCOMPARE(card_label_from_index(0), str_label("A of clubs"));
    QCOMPARE(
        card_label_from_index(static_cast<int>(element_ids.size()) - 1),
        str_label("Joker")
    );
}

void card_sheet_tests::
    available_themes_include_bundled_and_installed_when_present() {
    const QVector<card_theme_option>& themes = available_card_themes();
    QVERIFY(!themes.isEmpty());

    bool found_default_theme = false;
    bool found_installed_theme = false;
    bool installed_section_started = false;
    QSet<QString> seen_sources;
    for (const card_theme_option& theme : themes) {
        QVERIFY(!theme.label.isEmpty());
        QVERIFY(!theme.source_path.isEmpty());
        QVERIFY(!seen_sources.contains(theme.source_path));
        seen_sources.insert(theme.source_path);

        QFileInfo info(theme.source_path);
        QVERIFY(info.exists());
        QVERIFY(info.isFile());

        QSvgRenderer renderer(theme.source_path);
        QVERIFY(renderer.isValid());

        if (theme.source_path == default_card_sheet_source_path()) {
            found_default_theme = true;
        }

        if (theme.installed) {
            installed_section_started = true;
            found_installed_theme = true;
            QVERIFY(info.isAbsolute());
        } else if (installed_section_started) {
            QFAIL("bundled themes must stay before installed themes");
        }
    }

    QVERIFY(found_default_theme);

    const QString installed_standard_theme
        = QStringLiteral("/usr/share/carddecks/svg-standard/standard.svgz");
    if (QFileInfo::exists(installed_standard_theme)) {
        QVERIFY(found_installed_theme);
        QVERIFY(seen_sources.contains(installed_standard_theme));
    }
}

void card_sheet_tests::source_path_switches_between_themes() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    const QString theme_one = str_label("assets/cards_1.svg");
    const QString theme_two = str_label("assets/cards_2.svg");

    set_card_sheet_source_path(theme_one);
    QCOMPARE(card_sheet_source_path(), theme_one);
    QVERIFY(preload_card_sheet());
    const auto ratio_one = card_sheet_ratio();
    QVERIFY(ratio_one.first > 0);
    QVERIFY(ratio_one.second > 0);

    set_card_sheet_source_path(theme_two);
    QCOMPARE(card_sheet_source_path(), theme_two);
    QVERIFY(preload_card_sheet());
    const auto ratio_two = card_sheet_ratio();
    QVERIFY(ratio_two.first > 0);
    QVERIFY(ratio_two.second > 0);
}

void card_sheet_tests::
    required_ids_and_fallback_resolution_are_deterministic() {
    const QStringList required_ids = required_card_ids_with_back();
    QVERIFY(!required_ids.isEmpty());
    QCOMPARE(
        required_ids.size(), static_cast<int>(card_element_ids().size()) + 1
    );
    QCOMPARE(required_ids.constLast(), card_back_element_id());

    const card_sheet_fallback_resolution active_resolution
        = resolve_required_card_face_sources(str_label("assets/cards_1.svg"));
    QCOMPARE(active_resolution.active_theme_keys, required_ids.size());
    QCOMPARE(active_resolution.default_theme_keys, 0);
    QCOMPARE(active_resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution active_raster_resolution;
    const QVector<QImage> active_images = rasterize_card_faces_with_fallback(
        str_label("assets/cards_1.svg"), QSize(72, 72),
        &active_raster_resolution
    );
    QCOMPARE(active_images.size(), required_ids.size());
    QCOMPARE(active_raster_resolution.active_theme_keys, required_ids.size());
    QCOMPARE(active_raster_resolution.default_theme_keys, 0);
    QCOMPARE(active_raster_resolution.placeholder_keys, 0);
    for (const QImage& image : active_images) {
        QVERIFY(!image.isNull());
    }

    const card_sheet_fallback_resolution default_resolution
        = resolve_required_card_face_sources(
            str_label("assets/non_existent_cards.svg")
        );
    QCOMPARE(default_resolution.active_theme_keys, 0);
    QCOMPARE(default_resolution.default_theme_keys, required_ids.size());
    QCOMPARE(default_resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution fallback_raster_resolution;
    const QVector<QImage> fallback_images = rasterize_card_faces_with_fallback(
        str_label("assets/non_existent_cards.svg"), QSize(72, 72),
        &fallback_raster_resolution
    );
    QCOMPARE(fallback_images.size(), required_ids.size());
    QCOMPARE(fallback_raster_resolution.active_theme_keys, 0);
    QCOMPARE(
        fallback_raster_resolution.default_theme_keys, required_ids.size()
    );
    QCOMPARE(fallback_raster_resolution.placeholder_keys, 0);
    for (const QImage& image : fallback_images) {
        QVERIFY(!image.isNull());
    }
}

void card_sheet_tests::alternate_svg_id_conventions_are_resolved() {
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString source_path = temp_dir.filePath(str_label("adaptive.svg"));
    QVERIFY(write_alternate_id_card_svg(source_path));

    QSvgRenderer renderer(source_path);
    QVERIFY(renderer.isValid());
    QVERIFY(renderer.elementExists(str_label("1_club")));
    QVERIFY(!renderer.elementExists(str_label("club_1")));
    QVERIFY(renderer.elementExists(str_label("club_jack")));
    QVERIFY(!renderer.elementExists(str_label("jack_club")));
    QVERIFY(renderer.elementExists(str_label("card_back")));
    QVERIFY(!renderer.elementExists(str_label("back")));

    const int required_keys
        = static_cast<int>(required_card_ids_with_back().size());
    const card_sheet_fallback_resolution resolution
        = resolve_required_card_face_sources(source_path);
    QCOMPARE(resolution.active_theme_keys, required_keys);
    QCOMPARE(resolution.default_theme_keys, 0);
    QCOMPARE(resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution raster_resolution;
    const QVector<QImage> images = rasterize_card_faces_with_fallback(
        source_path, QSize(72, 72), &raster_resolution
    );
    QCOMPARE(images.size(), required_keys);
    QCOMPARE(raster_resolution.active_theme_keys, required_keys);
    QCOMPARE(raster_resolution.default_theme_keys, 0);
    QCOMPARE(raster_resolution.placeholder_keys, 0);
    for (const QImage& image : images) {
        QVERIFY(!image.isNull());
    }

    card_sheet_fallback_resolution preview_resolution;
    const QImage preview = rasterize_card_face_with_fallback(
        source_path, str_label("club_1"), QSize(72, 72), &preview_resolution
    );
    QVERIFY(!preview.isNull());
    QCOMPARE(preview_resolution.active_theme_keys, 1);
    QCOMPARE(preview_resolution.default_theme_keys, 0);
    QCOMPARE(preview_resolution.placeholder_keys, 0);
}

void card_sheet_tests::context_reuses_parsed_sources_after_file_removal() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cards.svg"));
    QVERIFY(write_alternate_id_card_svg(path));

    card_sheet_render_context context(path);
    const auto sources = context.resolve_required_sources();
    QCOMPARE(sources.active_theme_keys, required_card_ids_with_back().size());
    QVERIFY(QFile::remove(path));

    const QSize size(36, 48);
    const auto images = context.rasterize_faces(size);
    QCOMPARE(images.size(), required_card_ids_with_back().size());
    for (int index = 0; index < images.size(); ++index) {
        QVERIFY(!images.at(index).isNull());
        QCOMPARE(images.at(index).size(), size);
        QCOMPARE(
            context.rasterize_face(
                required_card_ids_with_back().at(index), size
            ),
            images.at(index)
        );
    }
    QCOMPARE(
        context.rasterize_face(card_back_element_id(), QSize(20, 30)).size(),
        QSize(20, 30)
    );
}

void card_sheet_tests::context_loads_fallback_on_first_missing_face() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString active = directory.filePath(QStringLiteral("partial.svg"));
    const QString fallback = directory.filePath(QStringLiteral("fallback.svg"));
    QFile file(active);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray svg
        = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"63\" "
          "height=\"88\"><rect id=\"club_1\" width=\"63\" height=\"88\" "
          "fill=\"red\"/></svg>";
    QCOMPARE(file.write(svg), svg.size());
    file.close();

    card_sheet_render_context context(active, fallback);
    card_sheet_fallback_resolution resolution;
    const QSize size(24, 32);
    const QImage selected
        = context.rasterize_face(QStringLiteral("club_1"), size, &resolution);
    QCOMPARE(selected.pixelColor(12, 16), QColor(Qt::red));
    QCOMPARE(resolution.active_theme_keys, 1);
    QCOMPARE(resolution.default_theme_keys, 0);

    // The fallback did not exist at construction or during the active hit.
    QVERIFY(write_alternate_id_card_svg(fallback));
    const QImage back
        = context.rasterize_face(card_back_element_id(), size, &resolution);
    QVERIFY(!back.isNull());
    QCOMPARE(resolution.default_theme_keys, 1);
    QVERIFY(QFile::remove(fallback));
    QVERIFY(QFile::remove(active));
    const auto images = context.rasterize_faces(size, &resolution);
    QCOMPARE(resolution.active_theme_keys, 1);
    QCOMPARE(resolution.default_theme_keys, images.size() - 1);
    QCOMPARE(resolution.placeholder_keys, 0);
    QCOMPARE(images.first(), selected);
    QCOMPARE(images.last(), back);
}

void card_sheet_tests::context_preserves_placeholders_and_invalid_input() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("empty.svg"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                           "width=\"63\" height=\"88\"/>";
    QCOMPARE(file.write(svg), svg.size());
    file.close();

    card_sheet_render_context context(path, path);
    card_sheet_fallback_resolution resolution;
    const auto images = context.rasterize_faces(QSize(16, 24), &resolution);
    QCOMPARE(images.size(), required_card_ids_with_back().size());
    QCOMPARE(resolution.placeholder_keys, images.size());
    for (const auto& image : images) {
        QVERIFY(image.isNull());
    }
    QVERIFY(
        context.rasterize_face(QString(), QSize(16, 24), &resolution).isNull()
    );
    QCOMPARE(resolution.placeholder_keys, 0);
    resolution.active_theme_keys = 7;
    QVERIFY(context.rasterize_faces(QSize(), &resolution).isEmpty());
    QCOMPARE(resolution.active_theme_keys, 0);
    resolution.default_theme_keys = 5;
    QVERIFY(
        rasterize_card_faces_with_fallback(path, QSize(), &resolution).isEmpty()
    );
    QCOMPARE(resolution.default_theme_keys, 0);
}

void card_sheet_tests::context_normalizes_each_sources_base_bounds() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString active = directory.filePath(QStringLiteral("active.svg"));
    const QString fallback = directory.filePath(QStringLiteral("fallback.svg"));
    const auto write_svg = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly)
            && file.write(data) == data.size();
    };
    QVERIFY(write_svg(
        active,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"34\">"
        "<rect id=\"base\" width=\"20\" height=\"30\"/>"
        "<g id=\"club_1\"><rect x=\"-2\" y=\"-2\" width=\"24\" height=\"34\" "
        "fill=\"red\"/>"
        "<rect width=\"20\" height=\"30\" fill=\"blue\"/></g></svg>"
    ));
    QVERIFY(write_svg(
        fallback,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"14\" height=\"14\">"
        "<rect id=\"card_base\" width=\"10\" height=\"10\"/>"
        "<g id=\"card_back\"><rect x=\"-2\" y=\"-2\" width=\"14\" "
        "height=\"14\" fill=\"red\"/>"
        "<rect width=\"10\" height=\"10\" fill=\"lime\"/></g></svg>"
    ));

    card_sheet_render_context context(active, fallback);
    QImage expected_active(QSize(24, 34), QImage::Format_ARGB32_Premultiplied);
    expected_active.fill(Qt::blue);
    QCOMPARE(
        context.rasterize_face(
            QStringLiteral("club_1"), expected_active.size()
        ),
        expected_active
    );
    QImage expected_fallback(
        QSize(28, 28), QImage::Format_ARGB32_Premultiplied
    );
    expected_fallback.fill(Qt::green);
    QCOMPARE(
        context.rasterize_face(
            card_back_element_id(), expected_fallback.size()
        ),
        expected_fallback
    );
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
