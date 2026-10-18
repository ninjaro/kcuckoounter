// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "include/card_sheet_tests.hpp"
#include "arch/asset_locator.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "image/card_images.hpp"
#include <stdexcept>

#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QScopeGuard>
#include <QSet>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest/QtTest>

namespace {
// Tests drive the actual installed service and process its completion events.
// No reference renderer, fake future or alternate pixel cache lives here.
class card_images_probe {
public:
    explicit card_images_probe(
        const QString& preferred,
        const QString& fallback = default_card_sheet_source_path()
    ) {
        images.set_source(preferred, fallback);
    }

    QVector<QImage> faces(
        const QSize& size, card_sheet_fallback_resolution* resolution = nullptr
    ) {
        std::vector<card_image_demand> desired;
        for (const auto& id : required_card_ids_with_back())
            desired.push_back({ id, size });
        images.replace_desired(std::move(desired));
        await_completion();
        if (resolution)
            *resolution = images.source_resolution();
        QVector<QImage> result;
        for (const auto& id : required_card_ids_with_back())
            result.push_back(images.image_for(id));
        return result;
    }

    QImage face(
        const QString& id, const QSize& size,
        card_sheet_fallback_resolution* resolution = nullptr
    ) {
        images.replace_desired({ { id, size } });
        await_completion();
        if (resolution)
            *resolution = images.source_resolution();
        return images.image_for(id);
    }

    card_sheet_fallback_resolution sources() {
        faces(QSize(1, 1));
        return images.source_resolution();
    }

private:
    void await_completion() const {
        if (!QTest::qWaitFor([this] { return !images.is_pending(); }, 15000))
            QTest::qFail(
                "Rasterflow did not complete the authored card demand",
                __FILE__, __LINE__
            );
    }

    card_image_consumer images;
};

QVector<QImage> test_faces(
    const QString& path, const QSize& size,
    card_sheet_fallback_resolution* resolution = nullptr
) {
    card_images_probe consumer(path);
    return consumer.faces(size, resolution);
}

QImage test_face(
    const QString& path, const QString& id, const QSize& size,
    card_sheet_fallback_resolution* resolution = nullptr
) {
    card_images_probe consumer(path);
    return consumer.face(id, size, resolution);
}

card_sheet_fallback_resolution test_sources(const QString& path) {
    card_images_probe consumer(path);
    return consumer.sources();
}
}

static QString alternate_svg_id_for_test(const QString& logical_id) {
    static const QStringList suits = {
        QStringLiteral("club"),
        QStringLiteral("diamond"),
        QStringLiteral("heart"),
        QStringLiteral("spade"),
    };

    if (logical_id == QStringLiteral("back")) {
        return QStringLiteral("card_back");
    }
    if (logical_id == QStringLiteral("joker_black")) {
        return QStringLiteral("black_joker");
    }
    if (logical_id == QStringLiteral("joker_red")) {
        return QStringLiteral("red_joker");
    }

    const QStringList parts = logical_id.split(QChar('_'), Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return logical_id;
    }

    if (suits.contains(parts.at(0))) {
        return QStringLiteral("%1_%2").arg(parts.at(1), parts.at(0));
    }
    if (suits.contains(parts.at(1))) {
        return QStringLiteral("%1_%2").arg(parts.at(1), parts.at(0));
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

void card_sheet_tests::metadata_selection_rejects_stale_completion() {
    const auto original = card_sheet_source_path();
    const auto restore
        = qScopeGuard([&] { set_card_sheet_source_path(original); });
    QTemporaryDir directory;
    const auto old_path = directory.filePath("old.svg");
    const auto current_path = directory.filePath("current.svg");
    const auto write = [](const QString& path, const QByteArray& elements) {
        QFile file(path);
        const auto svg = QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                                    "width=\"80\" height=\"120\">")
            + elements + "</svg>";
        return file.open(QIODevice::WriteOnly) && file.write(svg) == svg.size();
    };
    QVERIFY(write(old_path, "<rect id=\"club_1\" width=\"10\" height=\"30\"/>")
    );
    QVERIFY(
        write(current_path, "<rect id=\"club_1\" width=\"20\" height=\"40\"/>")
    );
    auto& metadata = current_card_sheet_metadata();
    QSignalSpy changed(&metadata, &card_sheet_metadata::geometry_changed);
    set_card_sheet_source_path(old_path);
    QVERIFY(metadata.is_pending());
    set_card_sheet_source_path(current_path);
    QTRY_VERIFY(!metadata.is_pending());
    QVERIFY(metadata.is_valid());
    QCOMPARE(card_sheet_ratio(), (std::pair<int, int> { 40, 20 }));
    QTRY_COMPARE(
        application_image_service().observations().scheduling.running_jobs, 0
    );
    QCOMPARE(card_sheet_source_path(), current_path);
    QCOMPARE(card_sheet_ratio(), (std::pair<int, int> { 40, 20 }));
    QCOMPARE(changed.size(), 1);
    // A malformed readable file is rejected asynchronously, with base geometry.
    QFile invalid(old_path);
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    invalid.write("invalid svg");
    invalid.close();
    set_card_sheet_source_path(old_path);
    QTRY_VERIFY(!metadata.is_pending());
    QVERIFY(!metadata.is_valid());
    QCOMPARE(card_sheet_ratio(), (std::pair<int, int> { 88, 63 }));
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

void card_sheet_tests::logical_resource_ids_are_literal() {
    QStringList expected;
    for (const auto& suit :
         { QStringLiteral("club"), QStringLiteral("diamond"),
           QStringLiteral("heart"), QStringLiteral("spade") }) {
        for (int rank = 1; rank <= 10; ++rank)
            expected.append(QStringLiteral("%1_%2").arg(suit).arg(rank));
        for (const auto& rank :
             { QStringLiteral("jack"), QStringLiteral("queen"),
               QStringLiteral("king") })
            expected.append(QStringLiteral("%1_%2").arg(rank, suit));
    }
    expected.append(QStringLiteral("joker_black"));
    expected.append(QStringLiteral("joker_red"));
    QCOMPARE(card_element_ids(), expected);
    for (qsizetype index = 0; index < expected.size(); ++index)
        QCOMPARE(
            card_element_id_from_index(static_cast<int>(index)), expected[index]
        );
    expected.append(QStringLiteral("back"));
    QCOMPARE(required_card_ids_with_back(), expected);
    QCOMPARE(card_back_element_id(), QStringLiteral("back"));
}

void card_sheet_tests::bundled_selection_is_independent_of_picker() {
    const QString original_source = card_sheet_source_path();
    const auto restore_source = qScopeGuard([&original_source] {
        set_card_sheet_source_path(original_source);
    });
    const QList<int> retained = { 0, 4, 9, 11, 23, 28, 40, 42 };
    QStringList selected_sources;
    for (const int number : retained) {
        const QString relative
            = QStringLiteral("sets/cards/cards_%1.svg").arg(number);
        const QString expected = bundled_asset_path(relative);
        QVERIFY(!expected.isEmpty());
        set_card_sheet_source_path(QStringLiteral("assets/%1").arg(relative));
        QCOMPARE(card_sheet_source_path(), expected);
        selected_sources.append(expected);
    }

    const QString selected_source = card_sheet_source_path();
    const auto& themes = available_card_themes();
    QCOMPARE(card_sheet_source_path(), selected_source);
    QStringList bundled_sources;
    for (const auto& theme : themes) {
        if (!theme.installed) {
            bundled_sources.append(theme.source_path);
        }
    }
    QCOMPARE(bundled_sources, selected_sources);

    for (const auto& theme : themes) {
        set_card_sheet_source_path(theme.source_path);
        QCOMPARE(card_sheet_source_path(), theme.source_path);
        QTRY_VERIFY(!current_card_sheet_metadata().is_pending());
        QVERIFY(current_card_sheet_metadata().is_valid());
        const auto ratio = card_sheet_ratio();
        QVERIFY(ratio.first > 0);
        QVERIFY(ratio.second > 0);
    }
}

void card_sheet_tests::themes_include_bundled_and_available_installed() {
    const QVector<card_theme_option>& themes = available_card_themes();
    QVERIFY(!themes.isEmpty());

    bool found_default_theme = false;
    bool found_installed_theme = false;
    bool installed_section_started = false;
    QSet<QString> seen_sources;
    QStringList bundled_names;
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
        } else {
            QVERIFY(!installed_section_started);
            bundled_names.append(info.fileName());
        }
    }

    QVERIFY(found_default_theme);
    QCOMPARE(themes.first().source_path, default_card_sheet_source_path());
    QCOMPARE(
        bundled_names,
        QStringList({
            QStringLiteral("cards_0.svg"),
            QStringLiteral("cards_4.svg"),
            QStringLiteral("cards_9.svg"),
            QStringLiteral("cards_11.svg"),
            QStringLiteral("cards_23.svg"),
            QStringLiteral("cards_28.svg"),
            QStringLiteral("cards_40.svg"),
            QStringLiteral("cards_42.svg"),
        })
    );

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

    const QString theme_one
        = bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"));
    const QString theme_two
        = bundled_asset_path(QStringLiteral("sets/cards/cards_40.svg"));

    set_card_sheet_source_path(theme_one);
    QCOMPARE(card_sheet_source_path(), theme_one);
    QTRY_VERIFY(!current_card_sheet_metadata().is_pending());
    QVERIFY(current_card_sheet_metadata().is_valid());
    const auto ratio_one = card_sheet_ratio();
    QVERIFY(ratio_one.first > 0);
    QVERIFY(ratio_one.second > 0);

    set_card_sheet_source_path(theme_two);
    QCOMPARE(card_sheet_source_path(), theme_two);
    QTRY_VERIFY(!current_card_sheet_metadata().is_pending());
    QVERIFY(current_card_sheet_metadata().is_valid());
    const auto ratio_two = card_sheet_ratio();
    QVERIFY(ratio_two.first > 0);
    QVERIFY(ratio_two.second > 0);
}

void card_sheet_tests::required_ids_and_fallback_resolution_are_deterministic(
) {
    const QStringList required_ids = required_card_ids_with_back();
    QVERIFY(!required_ids.isEmpty());
    QCOMPARE(
        required_ids.size(), static_cast<int>(card_element_ids().size()) + 1
    );
    QCOMPARE(required_ids.constLast(), card_back_element_id());

    const card_sheet_fallback_resolution active_resolution = test_sources(
        bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg"))
    );
    QCOMPARE(active_resolution.active_theme_keys, required_ids.size());
    QCOMPARE(active_resolution.default_theme_keys, 0);
    QCOMPARE(active_resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution active_raster_resolution;
    const QVector<QImage> active_images = test_faces(
        bundled_asset_path(QStringLiteral("sets/cards/cards_4.svg")),
        QSize(72, 72), &active_raster_resolution
    );
    QCOMPARE(active_images.size(), required_ids.size());
    QCOMPARE(active_raster_resolution.active_theme_keys, required_ids.size());
    QCOMPARE(active_raster_resolution.default_theme_keys, 0);
    QCOMPARE(active_raster_resolution.placeholder_keys, 0);
    for (const QImage& image : active_images) {
        QVERIFY(!image.isNull());
        bool has_ink = false;
        for (int y = 0; y < image.height() && !has_ink; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.alpha() > 64
                    && (color.red() < 200 || color.green() < 200
                        || color.blue() < 200)) {
                    has_ink = true;
                    break;
                }
            }
        }
        QVERIFY(has_ink);
    }

    // A valid SVG with no card IDs uses the default without changing the deck.
    // This isolated fixture replaces the retired laid-out sheet dependency.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sheet_path = directory.filePath(QStringLiteral("poster.svg"));
    QFile sheet_file(sheet_path);
    QVERIFY(sheet_file.open(QIODevice::WriteOnly));
    const QByteArray sheet_svg
        = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"63\" "
          "height=\"88\">"
          "<rect width=\"63\" height=\"88\" fill=\"white\"/></svg>";
    QCOMPARE(sheet_file.write(sheet_svg), sheet_svg.size());
    sheet_file.close();
    QVERIFY(QSvgRenderer(sheet_path).isValid());
    const card_sheet_fallback_resolution sheet_resolution
        = test_sources(sheet_path);
    QCOMPARE(sheet_resolution.active_theme_keys, 0);
    QCOMPARE(sheet_resolution.default_theme_keys, required_ids.size());
    QCOMPARE(sheet_resolution.placeholder_keys, 0);

    // The four-color sprite has all 52 faces but no jokers or back. Only those
    // optional graphics should come from the default deck.
    card_sheet_fallback_resolution optional_resolution;
    const QVector<QImage> optional_images = test_faces(
        bundled_asset_path(QStringLiteral("sets/cards/cards_40.svg")),
        QSize(72, 72), &optional_resolution
    );
    QCOMPARE(optional_images.size(), required_ids.size());
    QCOMPARE(optional_resolution.active_theme_keys, required_ids.size() - 3);
    QCOMPARE(optional_resolution.default_theme_keys, 3);
    QCOMPARE(optional_resolution.placeholder_keys, 0);
    for (const QImage& image : optional_images) {
        QVERIFY(!image.isNull());
    }

    const card_sheet_fallback_resolution default_resolution
        = test_sources(str_label("assets/non_existent_cards.svg"));
    QCOMPARE(default_resolution.active_theme_keys, 0);
    QCOMPARE(default_resolution.default_theme_keys, required_ids.size());
    QCOMPARE(default_resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution fallback_raster_resolution;
    const QVector<QImage> fallback_images = test_faces(
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

void card_sheet_tests::retained_themes_render_all_logical_cards_data() {
    QTest::addColumn<int>("number");
    QTest::addColumn<int>("active_count");
    for (const auto& [number, active_count] : { std::pair { 0, 55 },
                                                { 4, 55 },
                                                { 9, 53 },
                                                { 11, 55 },
                                                { 23, 55 },
                                                { 28, 54 },
                                                { 40, 52 },
                                                { 42, 55 } }) {
        QTest::newRow(qPrintable(QStringLiteral("set-%1").arg(number)))
            << number << active_count;
    }
}

void card_sheet_tests::retained_themes_render_all_logical_cards() {
    QFETCH(int, number);
    QFETCH(int, active_count);
    const QString source = bundled_asset_path(
        QStringLiteral("sets/cards/cards_%1.svg").arg(number)
    );
    QVERIFY(!source.isEmpty());
    card_sheet_fallback_resolution resolution;
    const auto images = test_faces(source, QSize(72, 108), &resolution);
    QCOMPARE(resolution.active_theme_keys, active_count);
    QCOMPARE(resolution.default_theme_keys, 55 - active_count);
    QCOMPARE(resolution.placeholder_keys, 0);
    QCOMPARE(images.size(), required_card_ids_with_back().size());
    for (const QImage& image : images) {
        QVERIFY(!image.isNull());
        bool has_ink = false;
        for (int y = 0; y < image.height() && !has_ink; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.alpha() > 64
                    && (color.red() < 200 || color.green() < 200
                        || color.blue() < 200)) {
                    has_ink = true;
                    break;
                }
            }
        }
        QVERIFY(has_ink);
    }
}

void card_sheet_tests::removed_bundled_sources_fall_back_to_default() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    const QList<int> retained = { 0, 4, 9, 11, 23, 28, 40, 42 };
    for (int number = 0; number < 47; ++number) {
        if (retained.contains(number)) {
            continue;
        }
        const QString relative
            = QStringLiteral("sets/cards/cards_%1.svg").arg(number);
        QVERIFY(bundled_asset_path(relative).isEmpty());
        set_card_sheet_source_path(
            QStringLiteral(":/kcuckoounter/%1").arg(relative)
        );
        QCOMPARE(card_sheet_source_path(), default_card_sheet_source_path());
        set_card_sheet_source_path(QStringLiteral("assets/%1").arg(relative));
        QCOMPARE(card_sheet_source_path(), default_card_sheet_source_path());
    }

    // A stale installed/staged file must not resurrect a retired bundled ID.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("sets/cards")));
    const QString stale
        = directory.filePath(QStringLiteral("sets/cards/cards_1.svg"));
    QVERIFY(write_alternate_id_card_svg(stale));
    set_card_sheet_source_path(stale);
    QCOMPARE(card_sheet_source_path(), default_card_sheet_source_path());
    set_card_sheet_source_path(QStringLiteral("assets/cards_2.svg"));
    QCOMPARE(card_sheet_source_path(), default_card_sheet_source_path());
    set_card_sheet_source_path(QString());
    QCOMPARE(card_sheet_source_path(), default_card_sheet_source_path());
    QTRY_VERIFY(!current_card_sheet_metadata().is_pending());
    QVERIFY(current_card_sheet_metadata().is_valid());

    // Stored surviving paths rebind to today's resource/installation location.
    set_card_sheet_source_path(
        QStringLiteral("/retired/install/sets/cards/cards_11.svg")
    );
    QCOMPARE(
        card_sheet_source_path(),
        bundled_asset_path(QStringLiteral("sets/cards/cards_11.svg"))
    );
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
    const card_sheet_fallback_resolution resolution = test_sources(source_path);
    QCOMPARE(resolution.active_theme_keys, required_keys);
    QCOMPARE(resolution.default_theme_keys, 0);
    QCOMPARE(resolution.placeholder_keys, 0);

    card_sheet_fallback_resolution raster_resolution;
    const QVector<QImage> images
        = test_faces(source_path, QSize(72, 72), &raster_resolution);
    QCOMPARE(images.size(), required_keys);
    QCOMPARE(raster_resolution.active_theme_keys, required_keys);
    QCOMPARE(raster_resolution.default_theme_keys, 0);
    QCOMPARE(raster_resolution.placeholder_keys, 0);
    for (const QImage& image : images) {
        QVERIFY(!image.isNull());
    }

    card_sheet_fallback_resolution preview_resolution;
    const QImage preview = test_face(
        source_path, str_label("club_1"), QSize(72, 72), &preview_resolution
    );
    QVERIFY(!preview.isNull());
    QCOMPARE(preview_resolution.active_theme_keys, 1);
    QCOMPARE(preview_resolution.default_theme_keys, 0);
    QCOMPARE(preview_resolution.placeholder_keys, 0);
}

void card_sheet_tests::registered_sources_survive_file_removal() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cards.svg"));
    QVERIFY(write_alternate_id_card_svg(path));

    card_images_probe context(path);
    const auto sources = context.sources();
    QCOMPARE(sources.active_theme_keys, required_card_ids_with_back().size());
    QVERIFY(QFile::remove(path));

    const QSize size(36, 48);
    const auto images = context.faces(size);
    QCOMPARE(images.size(), required_card_ids_with_back().size());
    for (int index = 0; index < images.size(); ++index) {
        QVERIFY(!images.at(index).isNull());
        QVERIFY(images.at(index).width() > size.width());
        QVERIFY(images.at(index).height() > size.height());
        QCOMPARE(
            context.face(required_card_ids_with_back().at(index), size),
            images.at(index)
        );
    }
    const auto back = context.face(card_back_element_id(), QSize(20, 30));
    QVERIFY(back.width() > 20 && back.height() > 30);
}

void card_sheet_tests::fallback_loads_on_first_missing_face() {
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

    card_images_probe context(active, fallback);
    card_sheet_fallback_resolution resolution;
    const QSize size(24, 32);
    const QImage selected
        = context.face(QStringLiteral("club_1"), size, &resolution);
    QCOMPARE(selected.pixelColor(12, 16), QColor(Qt::red));
    QCOMPARE(resolution.active_theme_keys, 1);
    QCOMPARE(resolution.default_theme_keys, 0);

    // The fallback did not exist at construction or during the active hit.
    QVERIFY(write_alternate_id_card_svg(fallback));
    const QImage back = context.face(card_back_element_id(), size, &resolution);
    QVERIFY(!back.isNull());
    QCOMPARE(resolution.default_theme_keys, 1);
    QVERIFY(QFile::remove(fallback));
    QVERIFY(QFile::remove(active));
    const auto images = context.faces(size, &resolution);
    QCOMPARE(resolution.active_theme_keys, 1);
    QCOMPARE(resolution.default_theme_keys, images.size() - 1);
    QCOMPARE(resolution.placeholder_keys, 0);
    QCOMPARE(images.first(), selected);
    QCOMPARE(images.last(), back);
}

void card_sheet_tests::missing_faces_and_invalid_demand() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("empty.svg"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                           "width=\"63\" height=\"88\"/>";
    QCOMPARE(file.write(svg), svg.size());
    file.close();

    card_images_probe context(path, path);
    card_sheet_fallback_resolution resolution;
    const auto images = context.faces(QSize(16, 24), &resolution);
    QCOMPARE(images.size(), required_card_ids_with_back().size());
    QCOMPARE(resolution.placeholder_keys, images.size());
    for (const auto& image : images) {
        QVERIFY(image.isNull());
    }
    card_image_consumer consumer;
    consumer.set_source(path, path);
    consumer.replace_desired({ { QStringLiteral("club_1"), QSize(16, 24) } });
    QTRY_VERIFY(!consumer.is_pending());
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        consumer.replace_desired({ { QString(), QSize(16, 24) } })
    );
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        consumer.replace_desired({ { QStringLiteral("club_1"), QSize() } })
    );
    QCOMPARE(consumer.source_resolution().placeholder_keys, 1);
}

void card_sheet_tests::base_frames_follow_each_selected_source() {
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

    card_images_probe context(active, fallback);
    const auto active_image
        = context.face(QStringLiteral("club_1"), QSize(24, 34));
    QVERIFY(active_image.width() > 24 && active_image.height() > 34);
    QImage expected_active(
        active_image.size(), QImage::Format_ARGB32_Premultiplied
    );
    expected_active.fill(Qt::blue);
    QCOMPARE(active_image, expected_active);
    const auto fallback_image
        = context.face(card_back_element_id(), QSize(28, 28));
    QVERIFY(fallback_image.width() > 28 && fallback_image.height() > 28);
    QImage expected_fallback(
        fallback_image.size(), QImage::Format_ARGB32_Premultiplied
    );
    expected_fallback.fill(Qt::green);
    QCOMPARE(fallback_image, expected_fallback);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
