#include "card_helpers/card_sheet.hpp"

#include "arch/asset_locator.hpp"
#include "arch/str_label.hpp"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRectF>
#include <QSettings>
#include <QSvgRenderer>
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
        = bundled_asset_path(QStringLiteral("cards_0.svg"));
    return source;
}

static QString bundled_card_theme_path(int theme_index) {
    return bundled_asset_path(QStringLiteral("cards_%1.svg").arg(theme_index));
}

static QString bundled_card_theme_label(int theme_index) {
    if (theme_index == 0) {
        return str_label("Bundled: Base card theme");
    }
    if (theme_index == 1) {
        return str_label("Bundled: Alternative card theme 1");
    }
    if (theme_index == 2) {
        return str_label("Bundled: Alternative card theme 2");
    }
    return str_label("Bundled: Theme %1").arg(theme_index);
}

static bool card_theme_source_is_reachable(const QString& source_path) {
    const QFileInfo info(source_path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    QSvgRenderer renderer(source_path);
    return renderer.isValid();
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

static QVector<card_theme_option> build_available_card_themes() {
    QVector<card_theme_option> themes;
    for (int theme_index = 0; theme_index <= 2; ++theme_index) {
        const QString source_path = bundled_card_theme_path(theme_index);
        if (!card_theme_source_is_reachable(source_path)) {
            continue;
        }

        themes.push_back(
            card_theme_option {
                .label = bundled_card_theme_label(theme_index),
                .source_path = source_path,
                .installed = false,
            }
        );
    }

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
            installed_themes.push_back(
                card_theme_option {
                    .label = str_label("KDE Games: %1").arg(label),
                    .source_path = source_path,
                    .installed = true,
                }
            );
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
    static const QStringList ids = { str_label("club"), str_label("diamond"),
                                     str_label("heart"), str_label("spade") };
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
        return str_label("jack");
    }
    if (rank_index == 11) {
        return str_label("queen");
    }
    if (rank_index == 12) {
        return str_label("king");
    }
    return {};
}

static QStringList rank_aliases(int rank_index) {
    QStringList aliases;
    append_unique(aliases, canonical_rank_token(rank_index));
    if (rank_index == 0) {
        append_unique(aliases, str_label("a"));
        append_unique(aliases, str_label("ace"));
    } else if (rank_index == 10) {
        append_unique(aliases, str_label("j"));
    } else if (rank_index == 11) {
        append_unique(aliases, str_label("q"));
    } else if (rank_index == 12) {
        append_unique(aliases, str_label("k"));
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

static QStringList
candidate_card_element_ids(const QString& logical_element_id) {
    QStringList candidates;
    append_unique(candidates, logical_element_id);

    const QString normalized = logical_element_id.toLower();
    if (normalized == QStringLiteral("back")) {
        append_unique(candidates, str_label("card_back"));
        append_unique(candidates, str_label("cardback"));
        return candidates;
    }
    if (normalized == QStringLiteral("base")) {
        append_unique(candidates, str_label("card_base"));
        return candidates;
    }
    if (normalized == QStringLiteral("joker_black")) {
        append_unique(candidates, str_label("black_joker"));
        append_unique(candidates, str_label("joker_1"));
        append_unique(candidates, str_label("joker1"));
        return candidates;
    }
    if (normalized == QStringLiteral("joker_red")) {
        append_unique(candidates, str_label("red_joker"));
        append_unique(candidates, str_label("joker_2"));
        append_unique(candidates, str_label("joker2"));
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
        append_unique(candidates, str_label("%1_%2").arg(suit, rank));
        append_unique(candidates, str_label("%1_%2").arg(rank, suit));
    }
    return candidates;
}

static QString resolve_element_id_for_renderer(
    const QSvgRenderer& renderer, const QString& logical_element_id
) {
    if (!renderer.isValid()) {
        return {};
    }

    const QStringList candidates
        = candidate_card_element_ids(logical_element_id);
    for (const QString& candidate : candidates) {
        if (renderer.elementExists(candidate)) {
            return candidate;
        }
    }
    return {};
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
        return str_label("%1_%2").arg(suit_id).arg(rank_index + 1);
    }
    if (rank_index == 10) {
        return str_label("jack_%1").arg(suit_id);
    }
    if (rank_index == 11) {
        return str_label("queen_%1").arg(suit_id);
    }
    if (rank_index == 12) {
        return str_label("king_%1").arg(suit_id);
    }
    return {};
}

static QImage normalize_card_to_base_frame(
    const QImage& image, const QRectF& element_bounds, const QRectF& base_bounds
) {
    if (image.isNull() || !element_bounds.isValid() || !base_bounds.isValid()
        || element_bounds.width() <= 0.0 || element_bounds.height() <= 0.0
        || base_bounds.width() <= 0.0 || base_bounds.height() <= 0.0) {
        return image;
    }

    const qreal extra_width
        = std::max(0.0, element_bounds.width() - base_bounds.width());
    const qreal extra_height
        = std::max(0.0, element_bounds.height() - base_bounds.height());
    if (extra_width <= 0.0 && extra_height <= 0.0) {
        return image;
    }

    const qreal trim_x
        = image.width() * (extra_width / element_bounds.width()) * 0.5;
    const qreal trim_y
        = image.height() * (extra_height / element_bounds.height()) * 0.5;

    const int max_trim_x = std::max(0, (image.width() - 1) / 2);
    const int max_trim_y = std::max(0, (image.height() - 1) / 2);

    const int trim_left
        = std::clamp(static_cast<int>(std::lround(trim_x)), 0, max_trim_x);
    const int trim_right = trim_left;
    const int trim_top
        = std::clamp(static_cast<int>(std::lround(trim_y)), 0, max_trim_y);
    const int trim_bottom = trim_top;

    const int cropped_width = image.width() - trim_left - trim_right;
    const int cropped_height = image.height() - trim_top - trim_bottom;
    if (cropped_width <= 0 || cropped_height <= 0) {
        return image;
    }
    if (cropped_width == image.width() && cropped_height == image.height()) {
        return image;
    }

    const QRect crop_rect(trim_left, trim_top, cropped_width, cropped_height);
    const QImage cropped = image.copy(crop_rect);
    return cropped.isNull()
        ? image
        : cropped.scaled(
              image.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation
          );
}

struct card_sheet_cache {
    bool valid = false;
    std::pair<int, int> ratio { 88, 63 };
};

enum class resolved_source_kind {
    active_theme,
    default_theme,
    placeholder,
};

struct resolved_required_element {
    QString render_element_id;
    resolved_source_kind source_kind = resolved_source_kind::placeholder;
};

static QString& active_card_sheet_source() {
    static QString source = default_card_sheet_source_value();
    return source;
}

struct cached_sheet_state {
    QString source;
    card_sheet_cache cache;
    bool has_value = false;
};

static card_sheet_cache
build_sheet_cache_for_source(const QString& source_path) {
    const std::pair<int, int> fallback_ratio { 88, 63 };
    QSvgRenderer renderer(source_path);
    if (!renderer.isValid()) {
        return { false, fallback_ratio };
    }

    const QStringList ids = required_card_ids_with_back();
    for (const QString& id : ids) {
        const QString render_id = resolve_element_id_for_renderer(renderer, id);
        if (render_id.isEmpty()) {
            continue;
        }
        const QRectF bounds = renderer.boundsOnElement(render_id);
        if (!bounds.isValid() || bounds.width() <= 0.0
            || bounds.height() <= 0.0) {
            continue;
        }

        const int width = static_cast<int>(std::lround(bounds.width()));
        const int height = static_cast<int>(std::lround(bounds.height()));
        if (width > 0 && height > 0) {
            const int long_side = std::max(width, height);
            const int short_side = std::min(width, height);
            return { true, { long_side, short_side } };
        }
    }

    return { true, fallback_ratio };
}

static card_sheet_cache
cached_card_sheet_for_source(const QString& source_path) {
    static cached_sheet_state state;
    if (state.has_value && state.source == source_path) {
        return state.cache;
    }

    state.source = source_path;
    state.cache = build_sheet_cache_for_source(source_path);
    state.has_value = true;
    return state.cache;
}

static QVector<resolved_required_element> resolve_required_elements(
    const QString& preferred_source_path,
    card_sheet_fallback_resolution* resolution
) {
    const QString active_source = preferred_source_path.isEmpty()
        ? default_card_sheet_source_path()
        : preferred_source_path;
    const QString fallback_source = default_card_sheet_source_path();

    QSvgRenderer active_renderer(active_source);
    QSvgRenderer fallback_renderer;
    const bool fallback_enabled = active_source != fallback_source;
    if (fallback_enabled) {
        fallback_renderer.load(fallback_source);
    }

    card_sheet_fallback_resolution local_resolution;
    QVector<resolved_required_element> resolved_elements;
    const QStringList required_ids = required_card_ids_with_back();
    resolved_elements.reserve(required_ids.size());
    for (const QString& element_id : required_ids) {
        resolved_required_element resolved {
            .render_element_id = {},
            .source_kind = resolved_source_kind::placeholder,
        };

        const QString active_render_id
            = resolve_element_id_for_renderer(active_renderer, element_id);
        if (!active_render_id.isEmpty()) {
            resolved.render_element_id = active_render_id;
            resolved.source_kind = resolved_source_kind::active_theme;
            local_resolution.active_theme_keys += 1;
        } else if (fallback_enabled) {
            const QString fallback_render_id = resolve_element_id_for_renderer(
                fallback_renderer, element_id
            );
            if (fallback_render_id.isEmpty()) {
                local_resolution.placeholder_keys += 1;
            } else {
                resolved.render_element_id = fallback_render_id;
                resolved.source_kind = resolved_source_kind::default_theme;
                local_resolution.default_theme_keys += 1;
            }
        } else {
            local_resolution.placeholder_keys += 1;
        }

        resolved_elements.push_back(resolved);
    }

    if (resolution != nullptr) {
        *resolution = local_resolution;
    }
    return resolved_elements;
}

static resolved_required_element resolve_single_required_element(
    const QString& preferred_source_path, const QString& logical_element_id,
    QSvgRenderer& active_renderer, QSvgRenderer& fallback_renderer,
    card_sheet_fallback_resolution* resolution
) {
    const QString active_source = preferred_source_path.isEmpty()
        ? default_card_sheet_source_path()
        : preferred_source_path;
    const QString fallback_source = default_card_sheet_source_path();
    const bool fallback_enabled = active_source != fallback_source;

    card_sheet_fallback_resolution local_resolution;
    resolved_required_element resolved {
        .render_element_id = {},
        .source_kind = resolved_source_kind::placeholder,
    };

    const QString active_render_id
        = resolve_element_id_for_renderer(active_renderer, logical_element_id);
    if (!active_render_id.isEmpty()) {
        resolved.render_element_id = active_render_id;
        resolved.source_kind = resolved_source_kind::active_theme;
        local_resolution.active_theme_keys = 1;
    } else if (fallback_enabled) {
        const QString fallback_render_id = resolve_element_id_for_renderer(
            fallback_renderer, logical_element_id
        );
        if (!fallback_render_id.isEmpty()) {
            resolved.render_element_id = fallback_render_id;
            resolved.source_kind = resolved_source_kind::default_theme;
            local_resolution.default_theme_keys = 1;
        } else {
            local_resolution.placeholder_keys = 1;
        }
    } else {
        local_resolution.placeholder_keys = 1;
    }

    if (resolution != nullptr) {
        *resolution = local_resolution;
    }
    return resolved;
}

static QImage render_resolved_card_face(
    QSvgRenderer& renderer, const QString& render_element_id,
    const QSize& raster_size
) {
    if (raster_size.isEmpty() || render_element_id.isEmpty()
        || !renderer.isValid() || !renderer.elementExists(render_element_id)) {
        return {};
    }

    const QString base_id
        = resolve_element_id_for_renderer(renderer, str_label("base"));
    const QRectF element_bounds = renderer.boundsOnElement(render_element_id);
    const QRectF base_bounds
        = base_id.isEmpty() ? QRectF() : renderer.boundsOnElement(base_id);

    QImage image(raster_size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(
        &painter, render_element_id,
        QRectF(QPointF(0.0, 0.0), QSizeF(raster_size))
    );
    painter.end();

    return normalize_card_to_base_frame(image, element_bounds, base_bounds);
}

QImage rasterize_card_face_with_fallback(
    const QString& preferred_source_path, const QString& logical_element_id,
    const QSize& raster_size, card_sheet_fallback_resolution* resolution
) {
    if (logical_element_id.isEmpty() || raster_size.isEmpty()) {
        if (resolution != nullptr) {
            *resolution = card_sheet_fallback_resolution {};
        }
        return {};
    }

    const QString active_source = preferred_source_path.isEmpty()
        ? default_card_sheet_source_path()
        : preferred_source_path;
    const QString fallback_source = default_card_sheet_source_path();
    QSvgRenderer active_renderer(active_source);
    QSvgRenderer fallback_renderer;
    const bool fallback_enabled = active_source != fallback_source;
    if (fallback_enabled) {
        fallback_renderer.load(fallback_source);
    }

    const resolved_required_element resolved = resolve_single_required_element(
        preferred_source_path, logical_element_id, active_renderer,
        fallback_renderer, resolution
    );
    QSvgRenderer* renderer = nullptr;
    if (resolved.source_kind == resolved_source_kind::active_theme) {
        renderer = &active_renderer;
    } else if (
        resolved.source_kind == resolved_source_kind::default_theme
        && fallback_enabled
    ) {
        renderer = &fallback_renderer;
    }

    if (renderer == nullptr) {
        return {};
    }
    return render_resolved_card_face(
        *renderer, resolved.render_element_id, raster_size
    );
}

static QStringList build_card_element_ids() {
    QStringList list;
    list.reserve(k_standard_deck_count + k_joker_count);
    for (int suit_index = 0; suit_index < k_suits_count; ++suit_index) {
        for (int rank_index = 0; rank_index < k_ranks_per_suit; ++rank_index) {
            list.append(rank_suit_element_id(rank_index, suit_index));
        }
    }
    list.append(str_label("joker_black"));
    list.append(str_label("joker_red"));
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

void set_card_sheet_source_path(const QString& source_path) {
    if (source_path.isEmpty()) {
        active_card_sheet_source() = default_card_sheet_source_value();
        return;
    }
    active_card_sheet_source() = source_path;
}

bool preload_card_sheet() {
    const card_sheet_cache cache
        = cached_card_sheet_for_source(card_sheet_source_path());
    return cache.valid;
}

std::pair<int, int> card_sheet_ratio() {
    const card_sheet_cache cache
        = cached_card_sheet_for_source(card_sheet_source_path());
    return cache.ratio;
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

QString card_back_element_id() { return str_label("back"); }

QStringList required_card_ids_with_back() {
    QStringList ids = card_element_ids();
    const QString back_id = card_back_element_id();
    if (!back_id.isEmpty()) {
        ids.append(back_id);
    }
    return ids;
}

card_sheet_fallback_resolution
resolve_required_card_face_sources(const QString& preferred_source_path) {
    card_sheet_fallback_resolution resolution;
    resolve_required_elements(preferred_source_path, &resolution);
    return resolution;
}

QVector<QImage> rasterize_card_faces_with_fallback(
    const QString& preferred_source_path, const QSize& raster_size,
    card_sheet_fallback_resolution* resolution
) {
    QVector<QImage> images;
    if (raster_size.isEmpty()) {
        return images;
    }

    card_sheet_fallback_resolution local_resolution;
    const QVector<resolved_required_element> resolved_elements
        = resolve_required_elements(preferred_source_path, &local_resolution);
    if (resolution != nullptr) {
        *resolution = local_resolution;
    }

    const QString active_source = preferred_source_path.isEmpty()
        ? default_card_sheet_source_path()
        : preferred_source_path;
    const QString fallback_source = default_card_sheet_source_path();
    QSvgRenderer active_renderer(active_source);
    QSvgRenderer fallback_renderer;
    const bool fallback_enabled = active_source != fallback_source;
    if (fallback_enabled) {
        fallback_renderer.load(fallback_source);
    }

    images.reserve(resolved_elements.size());
    for (const resolved_required_element& resolved : resolved_elements) {
        QSvgRenderer* renderer = nullptr;
        if (resolved.source_kind == resolved_source_kind::active_theme
            && active_renderer.isValid()) {
            renderer = &active_renderer;
        } else if (
            resolved.source_kind == resolved_source_kind::default_theme
            && fallback_enabled && fallback_renderer.isValid()
        ) {
            renderer = &fallback_renderer;
        }

        if (renderer == nullptr || resolved.render_element_id.isEmpty()) {
            images.push_back(QImage());
            continue;
        }

        images.push_back(render_resolved_card_face(
            *renderer, resolved.render_element_id, raster_size
        ));
    }

    return images;
}
