#include "card_helpers/card_sheet.hpp"

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"
#include "image/image_sources.hpp"
#include <QCoreApplication>
#include <QTimer>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <optional>

static constexpr int k_ranks_per_suit = 13;
static constexpr int k_suits_count = 4;
static constexpr int k_standard_deck_count = k_ranks_per_suit * k_suits_count;
static constexpr int k_joker_count = 2;

static const QString& default_card_sheet_source_value() {
    static const QString source
        = bundled_asset_path(QStringLiteral("sets/cards/cards_0.svg"));
    return source;
}

static bool card_theme_source_is_reachable(const QString& source_path) {
    const QFileInfo info(source_path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    // Discovery is I/O-only. Validate the chosen immutable snapshot through
    // Rasterflow rather than parsing every installed theme on the GUI thread.
    return info.isReadable();
}

static QString installed_card_theme_root_name(const QFileInfo& directory_info) {
    QString base_name = directory_info.fileName();
    if (!base_name.startsWith(QStringLiteral("svg-"))) {
        return base_name;
    }
    return base_name.mid(4);
}

static QString installed_card_theme_label(
    const QString& index_path, const QString& fallback_label
) {
    QSettings settings(index_path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("KDE Backdeck"));
    const QString name
        = settings.value(QStringLiteral("Name")).toString().trimmed();
    settings.endGroup();
    return name.isEmpty() ? fallback_label : name;
}

static QString installed_card_theme_svg_path(const QFileInfo& directory_info) {
    const QDir deck_dir(directory_info.absoluteFilePath());
    const QString index_path
        = deck_dir.filePath(QStringLiteral("index.desktop"));
    if (QFileInfo::exists(index_path)) {
        QSettings settings(index_path, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("KDE Backdeck"));
        const QString svg_name
            = settings.value(QStringLiteral("SVG")).toString().trimmed();
        settings.endGroup();
        if (!svg_name.isEmpty()) {
            const QString svg_path = deck_dir.filePath(svg_name);
            if (card_theme_source_is_reachable(svg_path)) {
                return svg_path;
            }
        }
    }

    const QFileInfoList svg_files = deck_dir.entryInfoList(
        QStringList() << QStringLiteral("*.svg") << QStringLiteral("*.svgz"),
        QDir::Files, QDir::Name
    );
    for (const QFileInfo& file_info : svg_files) {
        if (card_theme_source_is_reachable(file_info.absoluteFilePath())) {
            return file_info.absoluteFilePath();
        }
    }
    return {};
}

static bool card_theme_option_label_less(
    const card_theme_option& lhs, const card_theme_option& rhs
) {
    return lhs.label < rhs.label;
}

static QVector<card_theme_option> build_bundled_card_themes() {
    QVector<card_theme_option> themes;
    const QString& default_source = default_card_sheet_source_value();
    if (card_theme_source_is_reachable(default_source)) {
        themes.push_back(card_theme_option {
            .label = str_label("Bundled: Base card theme"),
            .source_path = default_source,
            .installed = false,
        });
    }

    QFile catalog(bundled_asset_path(QStringLiteral("sets/cards/cards.json")));
    QJsonArray bundled_sets;
    if (catalog.open(QIODevice::ReadOnly)) {
        bundled_sets = QJsonDocument::fromJson(catalog.readAll())
                           .object()
                           .value(QStringLiteral("sets"))
                           .toArray();
    }
    for (const QJsonValue value : bundled_sets) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString svg = entry.value(QStringLiteral("svg")).toString();
        bool numeric_id = false;
        const int number = id.mid(6).toInt(&numeric_id);
        if (!id.startsWith(QStringLiteral("cards_")) || !numeric_id
            || number <= 0 || id != QStringLiteral("cards_%1").arg(number)
            || svg != id + QStringLiteral(".svg")) {
            continue;
        }
        const QString source_path
            = bundled_asset_path(QStringLiteral("sets/cards/%1").arg(svg));
        if (!card_theme_source_is_reachable(source_path)
            || std::any_of(
                themes.cbegin(), themes.cend(),
                [&source_path](const card_theme_option& theme) {
                    return theme.source_path == source_path;
                }
            )) {
            continue;
        }
        const QString name = entry.value(QStringLiteral("name")).toString();
        themes.push_back(card_theme_option {
            .label = str_label("Bundled: %1").arg(name.isEmpty() ? id : name),
            .source_path = source_path,
            .installed = false,
        });
    }

    return themes;
}

static const QVector<card_theme_option>& bundled_card_themes() {
    static const QVector<card_theme_option> themes
        = build_bundled_card_themes();
    return themes;
}

static QVector<card_theme_option> build_available_card_themes() {
    QVector<card_theme_option> themes = bundled_card_themes();
    QVector<card_theme_option> installed_themes;
    const QStringList search_roots = {
        QStringLiteral("/usr/share/carddecks"),
        QStringLiteral("/usr/local/share/carddecks"),
    };
    QStringList seen_sources;
    seen_sources.reserve(themes.size());
    for (const card_theme_option& theme : themes) {
        seen_sources.push_back(theme.source_path);
    }

    for (const QString& root_path : search_roots) {
        const QDir root_dir(root_path);
        if (!root_dir.exists()) {
            continue;
        }

        const QFileInfoList deck_dirs = root_dir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name
        );
        for (const QFileInfo& deck_dir : deck_dirs) {
            const QString source_path = installed_card_theme_svg_path(deck_dir);
            if (source_path.isEmpty() || seen_sources.contains(source_path)) {
                continue;
            }

            const QString index_path
                = QDir(deck_dir.absoluteFilePath())
                      .filePath(QStringLiteral("index.desktop"));
            const QString label = installed_card_theme_label(
                index_path, installed_card_theme_root_name(deck_dir)
            );
            installed_themes.push_back(card_theme_option {
                .label = str_label("KDE Games: %1").arg(label),
                .source_path = source_path,
                .installed = true,
            });
            seen_sources.push_back(source_path);
        }
    }

    std::sort(
        installed_themes.begin(), installed_themes.end(),
        card_theme_option_label_less
    );
    for (const card_theme_option& theme : installed_themes) {
        themes.push_back(theme);
    }
    return themes;
}

static const QStringList& rank_labels() {
    static const QStringList labels
        = { str_label("A"), str_label("2"),  str_label("3"), str_label("4"),
            str_label("5"), str_label("6"),  str_label("7"), str_label("8"),
            str_label("9"), str_label("10"), str_label("J"), str_label("Q"),
            str_label("K") };
    return labels;
}

static const QStringList& suit_labels() {
    static const QStringList labels
        = { str_label("clubs"), str_label("diamonds"), str_label("hearts"),
            str_label("spades") };
    return labels;
}

static const QStringList& suit_ids() {
    // SVG keys and aliases are data, not user-visible/translatable labels.
    static const QStringList ids
        = { QStringLiteral("club"), QStringLiteral("diamond"),
            QStringLiteral("heart"), QStringLiteral("spade") };
    return ids;
}

static void append_unique(QStringList& values, const QString& value) {
    if (value.isEmpty() || values.contains(value)) {
        return;
    }
    values.append(value);
}

static int suit_index_from_token(const QString& token) {
    const QString normalized = token.toLower();
    const QStringList& suits = suit_ids();
    for (int index = 0; index < suits.size(); ++index) {
        if (normalized == suits.at(index)) {
            return index;
        }
    }
    return -1;
}

static std::optional<int> rank_index_from_token(const QString& token) {
    const QString normalized = token.toLower();
    bool ok = false;
    const int numeric_rank = normalized.toInt(&ok);
    if (ok && numeric_rank >= 1 && numeric_rank <= 10) {
        return numeric_rank - 1;
    }
    if (normalized == QStringLiteral("a")
        || normalized == QStringLiteral("ace")) {
        return 0;
    }
    if (normalized == QStringLiteral("j")
        || normalized == QStringLiteral("jack")) {
        return 10;
    }
    if (normalized == QStringLiteral("q")
        || normalized == QStringLiteral("queen")) {
        return 11;
    }
    if (normalized == QStringLiteral("k")
        || normalized == QStringLiteral("king")) {
        return 12;
    }
    return std::nullopt;
}

static QString canonical_rank_token(int rank_index) {
    if (rank_index >= 0 && rank_index <= 9) {
        return QString::number(rank_index + 1);
    }
    if (rank_index == 10) {
        return QStringLiteral("jack");
    }
    if (rank_index == 11) {
        return QStringLiteral("queen");
    }
    if (rank_index == 12) {
        return QStringLiteral("king");
    }
    return {};
}

static QStringList rank_aliases(int rank_index) {
    QStringList aliases;
    append_unique(aliases, canonical_rank_token(rank_index));
    if (rank_index == 0) {
        append_unique(aliases, QStringLiteral("a"));
        append_unique(aliases, QStringLiteral("ace"));
    } else if (rank_index == 10) {
        append_unique(aliases, QStringLiteral("j"));
    } else if (rank_index == 11) {
        append_unique(aliases, QStringLiteral("q"));
    } else if (rank_index == 12) {
        append_unique(aliases, QStringLiteral("k"));
    }
    return aliases;
}

struct parsed_card_element_id {
    bool valid = false;
    int rank_index = -1;
    int suit_index = -1;
};

static parsed_card_element_id parse_card_element_id(const QString& element_id) {
    const QStringList parts
        = element_id.toLower().split(QChar('_'), Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return {};
    }

    const int first_suit = suit_index_from_token(parts.at(0));
    const std::optional<int> second_rank = rank_index_from_token(parts.at(1));
    if (first_suit >= 0 && second_rank.has_value()) {
        return {
            .valid = true,
            .rank_index = *second_rank,
            .suit_index = first_suit,
        };
    }

    const std::optional<int> first_rank = rank_index_from_token(parts.at(0));
    const int second_suit = suit_index_from_token(parts.at(1));
    if (first_rank.has_value() && second_suit >= 0) {
        return {
            .valid = true,
            .rank_index = *first_rank,
            .suit_index = second_suit,
        };
    }

    return {};
}

QStringList candidate_card_element_ids(const QString& logical_element_id) {
    QStringList candidates;
    append_unique(candidates, logical_element_id);

    const QString normalized = logical_element_id.toLower();
    if (normalized == QStringLiteral("back")) {
        append_unique(candidates, QStringLiteral("card_back"));
        append_unique(candidates, QStringLiteral("cardback"));
        return candidates;
    }
    if (normalized == QStringLiteral("base")) {
        append_unique(candidates, QStringLiteral("card_base"));
        return candidates;
    }
    if (normalized == QStringLiteral("joker_black")) {
        append_unique(candidates, QStringLiteral("black_joker"));
        append_unique(candidates, QStringLiteral("joker_1"));
        append_unique(candidates, QStringLiteral("joker1"));
        return candidates;
    }
    if (normalized == QStringLiteral("joker_red")) {
        append_unique(candidates, QStringLiteral("red_joker"));
        append_unique(candidates, QStringLiteral("joker_2"));
        append_unique(candidates, QStringLiteral("joker2"));
        return candidates;
    }

    const parsed_card_element_id parsed
        = parse_card_element_id(logical_element_id);
    if (!parsed.valid) {
        return candidates;
    }

    const QString& suit = suit_ids().at(parsed.suit_index);
    const QStringList aliases = rank_aliases(parsed.rank_index);
    for (const QString& rank : aliases) {
        append_unique(candidates, QStringLiteral("%1_%2").arg(suit, rank));
        append_unique(candidates, QStringLiteral("%1_%2").arg(rank, suit));
    }
    return candidates;
}

static QString rank_suit_element_id(int rank_index, int suit_index) {
    const auto& suit_list = suit_ids();
    if (rank_index < 0 || rank_index >= k_ranks_per_suit) {
        return {};
    }
    if (suit_index < 0 || suit_index >= suit_list.size()) {
        return {};
    }

    const QString& suit_id = suit_list.at(suit_index);
    if (rank_index <= 9) {
        return QStringLiteral("%1_%2").arg(suit_id).arg(rank_index + 1);
    }
    if (rank_index == 10) {
        return QStringLiteral("jack_%1").arg(suit_id);
    }
    if (rank_index == 11) {
        return QStringLiteral("queen_%1").arg(suit_id);
    }
    if (rank_index == 12) {
        return QStringLiteral("king_%1").arg(suit_id);
    }
    return {};
}

static QString& active_card_sheet_source() {
    static QString source = default_card_sheet_source_value();
    return source;
}

card_sheet_metadata::card_sheet_metadata(QObject* parent)
    : QObject(parent)
    , consumer(application_image_service().create_consumer(this)) {
    connect(
        &acquisition, &image_source_request::finished, this,
        &card_sheet_metadata::publish_metadata
    );
    connect(
        consumer, &rasterflow::qt::image_consumer::images_changed, this,
        &card_sheet_metadata::publish_metadata
    );
    auto& service = application_image_service();
    const auto changed = [this](const QString& id) {
        if (acquisition.source().content_id() == id.toStdString())
            publish_metadata();
    };
    connect(
        &service, &rasterflow::qt::image_service::source_ready, this, changed
    );
    connect(
        &service, &rasterflow::qt::image_service::source_failed, this,
        [changed](const QString& id, const QString&) { changed(id); }
    );
    refresh();
}

void card_sheet_metadata::refresh() {
    const auto path = card_sheet_source_path();
    if (selected_path == path)
        return;
    selected_path = path;
    metadata_published = false;
    inspection_requests.clear();
    pending_element.clear();
    consumer->replace_desired({});
    acquisition.load(path);
    // Warm sources and I/O failures must notify through the same queued path.
    QTimer::singleShot(0, this, &card_sheet_metadata::publish_metadata);
}

bool card_sheet_metadata::is_pending() const { return !metadata_published; }

bool card_sheet_metadata::is_valid() const {
    return metadata_published && acquisition.source().is_ready();
}

std::pair<int, int> card_sheet_metadata::ratio() const { return current_ratio; }

void card_sheet_metadata::publish_metadata() {
    const auto source = acquisition.source();
    if (metadata_published || acquisition.is_pending()
        || (source.is_valid() && !source.is_ready() && source.error().isEmpty()
        ))
        return;
    std::pair<int, int> resolved_ratio { 88, 63 };
    for (const auto& logical : required_card_ids_with_back()) {
        bool found = false;
        for (const auto& id : candidate_card_element_ids(logical)) {
            if (source.is_ready() && !source.is_inspected(id)) {
                const rasterflow::qt::metadata_request query { source, id };
                if (consumer->status_for(query).state
                    == rasterflow::qt::image_state::failed)
                    continue;
                if (pending_element != id) {
                    pending_element = id;
                    inspection_requests.push_back(query);
                    consumer->replace_desired({}, inspection_requests, false);
                }
                return;
            }
            const auto bounds = source.bounds(id);
            if (bounds.isEmpty())
                continue;
            const auto width = static_cast<int>(std::lround(bounds.width()));
            const auto height = static_cast<int>(std::lround(bounds.height()));
            if (width <= 0 || height <= 0)
                continue;
            resolved_ratio
                = { std::max(width, height), std::min(width, height) };
            found = true;
            break;
        }
        if (found)
            break;
    }
    metadata_published = true;
    current_ratio = resolved_ratio;
    inspection_requests.clear();
    pending_element.clear();
    consumer->replace_desired({}, {}, false);
    emit geometry_changed(); // Observers read only the current selection.
}

card_sheet_metadata& current_card_sheet_metadata() {
    static QPointer<card_sheet_metadata> metadata;
    if (!metadata)
        metadata = new card_sheet_metadata(QCoreApplication::instance());
    return *metadata;
}

static QStringList build_card_element_ids() {
    QStringList list;
    list.reserve(k_standard_deck_count + k_joker_count);
    for (int suit_index = 0; suit_index < k_suits_count; ++suit_index) {
        for (int rank_index = 0; rank_index < k_ranks_per_suit; ++rank_index) {
            list.append(rank_suit_element_id(rank_index, suit_index));
        }
    }
    list.append(QStringLiteral("joker_black"));
    list.append(QStringLiteral("joker_red"));
    return list;
}

QString card_sheet_source_path() { return active_card_sheet_source(); }

QString default_card_sheet_source_path() {
    return default_card_sheet_source_value();
}

const QVector<card_theme_option>& available_card_themes() {
    static const QVector<card_theme_option> themes
        = build_available_card_themes();
    return themes;
}

static QString resolve_card_sheet_source_path(const QString& source_path) {
    if (source_path.isEmpty()) {
        return default_card_sheet_source_value();
    }
    // Rebind stored bundled paths through the current catalog. Old staged or
    // installed copies must not keep a removed bundle selectable.
    const QString normalized = QDir::fromNativeSeparators(source_path);
    const QFileInfo info(source_path);
    if (info.fileName().startsWith(QStringLiteral("cards_"))
        && info.suffix() == QStringLiteral("svg")
        && (normalized.contains(QStringLiteral("/sets/cards/"))
            || normalized.startsWith(QStringLiteral("sets/cards/"))
            || normalized.startsWith(QStringLiteral(":/kcuckoounter/"))
            || normalized.startsWith(QStringLiteral("assets/cards_")))) {
        for (const card_theme_option& theme : bundled_card_themes()) {
            if (QFileInfo(theme.source_path).fileName() == info.fileName()) {
                return theme.source_path;
            }
        }
        return default_card_sheet_source_value();
    }
    if (!info.exists() || !info.isFile()) {
        return default_card_sheet_source_value();
    }
    return source_path;
}

void set_card_sheet_source_path(const QString& source_path) {
    active_card_sheet_source() = resolve_card_sheet_source_path(source_path);
    current_card_sheet_metadata().refresh();
}

std::pair<int, int> card_sheet_ratio() {
    auto& metadata = current_card_sheet_metadata();
    metadata.refresh();
    return metadata.ratio();
}

QString card_label_from_index(int index) {
    if (index < 0) {
        return {};
    }
    if (index >= k_standard_deck_count) {
        return str_label("Joker");
    }

    const int rank_index = index % k_ranks_per_suit;
    const int suit_index = index / k_ranks_per_suit;
    const auto& ranks = rank_labels();
    const auto& suits = suit_labels();
    if (rank_index < 0 || rank_index >= ranks.size() || suit_index < 0
        || suit_index >= suits.size()) {
        return {};
    }

    return ranks.at(rank_index) + str_label(" of ") + suits.at(suit_index);
}

QString card_element_id_from_index(int index) {
    const auto& ids = card_element_ids();
    if (index < 0 || index >= ids.size()) {
        return {};
    }
    return ids.at(index);
}

const QStringList& card_element_ids() {
    static const QStringList ids = build_card_element_ids();
    return ids;
}

QString card_back_element_id() { return QStringLiteral("back"); }

QStringList required_card_ids_with_back() {
    QStringList ids = card_element_ids();
    const QString back_id = card_back_element_id();
    if (!back_id.isEmpty()) {
        ids.append(back_id);
    }
    return ids;
}
