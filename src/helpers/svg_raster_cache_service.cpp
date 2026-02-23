#include "helpers/svg_raster_cache_service.hpp"

#include <QtGlobal>

#include <QStringList>

#include <algorithm>

namespace {

size_t combine_hash(size_t lhs, size_t rhs) {
    return lhs ^ (rhs + 0x9e3779b9 + (lhs << 6U) + (lhs >> 2U));
}

QString normalize_render_scope(const QString& raw_scope) {
    const QString simplified = raw_scope.simplified();
    if (!simplified.startsWith(QStringLiteral("subset:"))) {
        return simplified;
    }

    QStringList ids = simplified.sliced(QStringLiteral("subset:").size())
                          .split(',', Qt::SkipEmptyParts);
    for (QString& id : ids) {
        id = id.simplified();
    }

    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    return QStringLiteral("subset:%1").arg(ids.join(','));
}

} // namespace

bool svg_raster_cache_service::entry_key::operator==(
    const entry_key& other
) const {
    return name_space == other.name_space && kind == other.kind
        && source_id == other.source_id && render_scope == other.render_scope
        && target_bucket_px == other.target_bucket_px;
}

bool svg_raster_cache_service::family_key::operator==(
    const family_key& other
) const {
    return name_space == other.name_space && kind == other.kind
        && source_id == other.source_id && render_scope == other.render_scope;
}

bool svg_raster_cache_service::result::is_ready() const {
    return !single_image.isNull() || !face_images.isEmpty();
}

svg_raster_cache_service::svg_raster_cache_service(QObject* parent)
    : QObject(parent)
    , ready_results()
    , ready_entry_order()
    , namespace_entry_limits(
          {
              { cache_namespace::main, -1 },
              { cache_namespace::settings, 3 },
          }
      )
    , families() { }

std::optional<svg_raster_cache_service::result>
svg_raster_cache_service::get_if_ready(const entry_key& key) const {
    const auto it = ready_results.constFind(key);
    if (it == ready_results.cend()) {
        return std::nullopt;
    }

    if (!it.value().is_ready()) {
        return std::nullopt;
    }

    return it.value();
}

std::optional<svg_raster_cache_service::result>
svg_raster_cache_service::get_if_ready_with_namespace_fallback(
    const entry_key& key
) const {
    const std::optional<result> direct = get_if_ready(key);
    if (direct.has_value()) {
        return direct;
    }

    if (key.name_space != cache_namespace::settings) {
        return std::nullopt;
    }

    entry_key main_key = key;
    main_key.name_space = cache_namespace::main;
    return get_if_ready(main_key);
}

void svg_raster_cache_service::insert_or_update_result(
    const result& new_result
) {
    if (!ready_results.contains(new_result.key)) {
        ready_entry_order[new_result.key.name_space].enqueue(new_result.key);
    }
    ready_results.insert(new_result.key, new_result);
    enforce_namespace_limit(new_result.key.name_space);
    emit result_updated(new_result.key);
}

svg_raster_cache_service::submit_outcome
svg_raster_cache_service::submit_request(const request& req) {
    const entry_key entry = make_entry_key(req);
    const family_key family = make_family_key(req);

    const std::optional<result> ready = get_if_ready(entry);
    if (ready.has_value()) {
        return submit_outcome {
            .state = request_state::cache_hit,
            .key = entry,
            .ready_result = ready,
        };
    }

    family_state& state = families[family];
    if (!state.in_flight) {
        state.in_flight = true;
        state.active_entry = entry;
        return submit_outcome {
            .state = request_state::start_async,
            .key = entry,
            .ready_result = std::nullopt,
        };
    }

    if (state.active_entry == entry) {
        return submit_outcome {
            .state = request_state::already_in_flight,
            .key = entry,
            .ready_result = std::nullopt,
        };
    }

    state.has_pending = true;
    state.pending_entry = entry;
    return submit_outcome {
        .state = request_state::pending_coalesced,
        .key = entry,
        .ready_result = std::nullopt,
    };
}

svg_raster_cache_service::finish_outcome
svg_raster_cache_service::finish_active_request(
    const family_key& key, const entry_key& completed_entry
) {
    const auto it = families.find(key);
    if (it == families.end() || !it->in_flight) {
        return finish_outcome {
            .accepted_completion = false,
            .next_entry_to_start = std::nullopt,
        };
    }

    if (!(it->active_entry == completed_entry)) {
        return finish_outcome {
            .accepted_completion = false,
            .next_entry_to_start = std::nullopt,
        };
    }

    it->in_flight = false;
    if (!it->has_pending) {
        families.erase(it);
        return finish_outcome {
            .accepted_completion = true,
            .next_entry_to_start = std::nullopt,
        };
    }

    const entry_key next_entry = it->pending_entry;
    it->has_pending = false;
    it->active_entry = next_entry;
    it->in_flight = true;
    return finish_outcome {
        .accepted_completion = true,
        .next_entry_to_start = next_entry,
    };
}

bool svg_raster_cache_service::is_in_flight(const family_key& key) const {
    const auto it = families.constFind(key);
    if (it == families.cend()) {
        return false;
    }

    return it.value().in_flight;
}

void svg_raster_cache_service::mark_in_flight(
    const family_key& key, const entry_key& active_key
) {
    family_state& state = families[key];
    state.in_flight = true;
    state.active_entry = active_key;
}

void svg_raster_cache_service::clear_in_flight(const family_key& key) {
    const auto it = families.find(key);
    if (it == families.end()) {
        return;
    }

    it->in_flight = false;
    if (!it->has_pending) {
        families.erase(it);
    }
}

void svg_raster_cache_service::set_pending_latest(
    const family_key& key, const entry_key& pending_key
) {
    family_state& state = families[key];
    state.has_pending = true;
    state.pending_entry = pending_key;
}

std::optional<svg_raster_cache_service::entry_key>
svg_raster_cache_service::take_pending_latest(const family_key& key) {
    const auto it = families.find(key);
    if (it == families.end() || !it->has_pending) {
        return std::nullopt;
    }

    const entry_key pending = it->pending_entry;
    it->has_pending = false;
    if (!it->in_flight) {
        families.erase(it);
    }

    return pending;
}

int svg_raster_cache_service::ready_entry_count() const {
    return ready_results.size();
}

int svg_raster_cache_service::ready_entry_count(
    cache_namespace name_space
) const {
    int count = 0;
    for (auto it = ready_results.cbegin(); it != ready_results.cend(); ++it) {
        if (it.key().name_space == name_space) {
            ++count;
        }
    }
    return count;
}

svg_raster_cache_service::entry_key
svg_raster_cache_service::make_entry_key(const request& req) {
    return entry_key {
        .name_space = req.name_space,
        .kind = req.kind,
        .source_id = req.source_id,
        .render_scope = normalize_render_scope(req.render_scope),
        .target_bucket_px = req.target_bucket_px,
    };
}

svg_raster_cache_service::family_key
svg_raster_cache_service::make_family_key(const request& req) {
    return family_key {
        .name_space = req.name_space,
        .kind = req.kind,
        .source_id = req.source_id,
        .render_scope = normalize_render_scope(req.render_scope),
    };
}

int svg_raster_cache_service::in_flight_count() const {
    int count = 0;
    for (auto it = families.cbegin(); it != families.cend(); ++it) {
        if (it->in_flight) {
            ++count;
        }
    }
    return count;
}

void svg_raster_cache_service::enforce_namespace_limit(
    cache_namespace name_space
) {
    const int limit = namespace_entry_limits.value(name_space, -1);
    if (limit < 0) {
        return;
    }

    QQueue<entry_key>& queue = ready_entry_order[name_space];
    while (ready_entry_count(name_space) > limit && !queue.isEmpty()) {
        const entry_key oldest = queue.dequeue();
        ready_results.remove(oldest);
    }
}

size_t qHash(const svg_raster_cache_service::entry_key& key, size_t seed) {
    size_t value = seed;
    value = combine_hash(value, qHash(static_cast<int>(key.name_space)));
    value = combine_hash(value, qHash(static_cast<int>(key.kind)));
    value = combine_hash(value, qHash(key.source_id));
    value = combine_hash(value, qHash(key.render_scope));
    value = combine_hash(value, qHash(key.target_bucket_px));
    return value;
}

size_t qHash(const svg_raster_cache_service::family_key& key, size_t seed) {
    size_t value = seed;
    value = combine_hash(value, qHash(static_cast<int>(key.name_space)));
    value = combine_hash(value, qHash(static_cast<int>(key.kind)));
    value = combine_hash(value, qHash(key.source_id));
    value = combine_hash(value, qHash(key.render_scope));
    return value;
}
