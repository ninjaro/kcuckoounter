#include "image/card_images.hpp"

#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace {
QString resolve_element(
    const rasterflow::qt::source_handle& source, const QString& logical
) {
    if (!source.is_ready())
        return {};
    for (const auto& candidate : candidate_card_element_ids(logical))
        if (source.contains(candidate))
            return candidate;
    return {};
}

QRectF card_viewport(
    const rasterflow::qt::source_handle& source, const QString& element
) {
    const auto bounds = source.bounds(element);
    const auto base_id = resolve_element(source, QStringLiteral("base"));
    if (base_id.isEmpty())
        return {};
    const auto base = source.bounds(base_id);
    if (base.isEmpty())
        return {};
    const QSizeF size(
        std::min(bounds.width(), base.width()),
        std::min(bounds.height(), base.height())
    );
    if (size == bounds.size())
        return {};
    // Preserve the game's symmetric base-frame trim in source coordinates.
    // Rasterflow renders this viewport uniformly; no crop-and-stretch copy.
    return { bounds.center() - QPointF(size.width() / 2, size.height() / 2),
             size };
}
}

card_image_consumer::card_image_consumer(QObject* parent)
    : QObject(parent)
    , consumer(application_image_service().create_consumer(this))
    , preferred_path(card_sheet_source_path())
    , fallback_path(default_card_sheet_source_path()) {
    connect(
        consumer, &rasterflow::qt::image_consumer::images_changed, this,
        &card_image_consumer::images_changed
    );
    const auto source_changed = [this](const QString& id) {
        if (!demands.empty()
            && (preferred_source.content_id() == id.toStdString()
                || fallback_source.content_id() == id.toStdString()))
            resolve_requests();
    };
    auto& service = application_image_service();
    connect(
        &service, &rasterflow::qt::image_service::source_ready, this,
        source_changed
    );
    connect(
        &service, &rasterflow::qt::image_service::source_failed, this,
        [source_changed](const QString& id, const QString&) {
            source_changed(id);
        }
    );
}

void card_image_consumer::set_source(
    const QString& preferred, const QString& fallback
) {
    const auto selected = preferred.isEmpty() ? fallback : preferred;
    if (selected == preferred_path && fallback == fallback_path)
        return;
    preferred_path = selected;
    fallback_path = fallback;
    preferred_source = {};
    fallback_source = {};
    preferred_loaded = false;
    fallback_loaded = false;
    resolve_requests();
}

void card_image_consumer::replace_desired(std::vector<card_image_demand> desired
) {
    QSet<QString> ids;
    for (const auto& demand : desired) {
        if (demand.logical_id.isEmpty() || ids.contains(demand.logical_id))
            throw std::invalid_argument(
                "card demand requires unique logical IDs"
            );
        ids.insert(demand.logical_id);
        (void)rasterflow::normalized_size(
            demand.physical_size.width(), demand.physical_size.height(), 1
        );
    }
    if (demands == desired)
        return;
    demands = std::move(desired);
    resolve_requests();
}

void card_image_consumer::clear() {
    demands.clear();
    requests.clear();
    resolution = {};
    unresolved = false;
    unresolved_required = false;
    preferred_source = {};
    fallback_source = {};
    preferred_loaded = false;
    fallback_loaded = false;
    consumer->replace_desired({});
}

void card_image_consumer::resolve_requests() {
    requests.clear();
    resolution = {};
    unresolved = false;
    unresolved_required = false;
    std::vector<rasterflow::qt::image_request> desired;
    if (!demands.empty() && !preferred_loaded) {
        preferred_source = load_image_source(preferred_path);
        preferred_loaded = true;
    }
    const bool preferred_pending = preferred_source.is_valid()
        && !preferred_source.is_ready() && preferred_source.error().isEmpty();
    for (const auto& demand : demands) {
        if (preferred_pending) {
            unresolved = true;
            unresolved_required = unresolved_required || demand.required;
            continue;
        }
        auto source = preferred_source;
        auto element = resolve_element(source, demand.logical_id);
        if (!element.isEmpty()) {
            ++resolution.active_theme_keys;
        } else if (preferred_path != fallback_path) {
            if (!fallback_loaded) {
                fallback_source = load_image_source(fallback_path);
                fallback_loaded = true;
            }
            if (fallback_source.is_valid() && !fallback_source.is_ready()
                && fallback_source.error().isEmpty()) {
                unresolved = true;
                unresolved_required = unresolved_required || demand.required;
                continue;
            }
            source = fallback_source;
            element = resolve_element(source, demand.logical_id);
            if (!element.isEmpty())
                ++resolution.default_theme_keys;
        }
        if (element.isEmpty()) {
            ++resolution.placeholder_keys;
            continue;
        }
        rasterflow::qt::image_request request {
            .source = source,
            .element_id = element,
            .physical_size = demand.physical_size,
            .required = demand.required,
            .priority = demand.priority,
            .deadline_us = demand.deadline_us,
            .viewport = card_viewport(source, element)
        };
        requests.insert(demand.logical_id, request);
        desired.push_back(std::move(request));
    }
    consumer->replace_desired(std::move(desired));
    emit images_changed(
    ); // Clients may destroy this adapter; state is settled.
}

QImage card_image_consumer::image_for(const QString& logical_id) const {
    const auto found = requests.constFind(logical_id);
    return found == requests.cend() ? QImage()
                                    : consumer->image_for(found.value());
}

bool card_image_consumer::is_pending() const {
    return unresolved || consumer->is_pending();
}

bool card_image_consumer::is_pending_required() const {
    return unresolved_required || consumer->is_pending_required();
}

bool card_image_consumer::preferred_source_failed() const {
    return preferred_loaded
        && (!preferred_source.is_valid() || !preferred_source.error().isEmpty()
        );
}

card_sheet_fallback_resolution card_image_consumer::source_resolution() const {
    return resolution;
}
