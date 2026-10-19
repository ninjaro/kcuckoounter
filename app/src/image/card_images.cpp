#include "image/card_images.hpp"

#include <QSet>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
QRectF card_viewport(
    const rasterflow::qt::source_handle& source, const QString& element,
    const QString& base_id
) {
    const auto bounds = source.bounds(element);
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
    const auto acquired = [this] {
        if (!demands.empty())
            resolve_requests();
    };
    connect(&preferred_source, &image_source_request::finished, this, acquired);
    connect(&fallback_source, &image_source_request::finished, this, acquired);
    connect(
        consumer, &rasterflow::qt::image_consumer::images_changed, this,
        [this] {
            if (unresolved)
                resolve_requests();
            else
                emit images_changed();
        }
    );
    const auto source_changed = [this](const QString& id) {
        if (!demands.empty()
            && (preferred_source.source().content_id() == id.toStdString()
                || fallback_source.source().content_id() == id.toStdString()))
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
    preferred_source.clear();
    fallback_source.clear();
    preferred_loaded = false;
    fallback_loaded = false;
    resolve_requests(true);
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
    resolve_requests(true);
}

void card_image_consumer::clear() {
    demands.clear();
    requests.clear();
    submitted_inspections.clear();
    resolution = {};
    unresolved = false;
    unresolved_required = false;
    preferred_source.clear();
    fallback_source.clear();
    preferred_loaded = false;
    fallback_loaded = false;
    consumer->replace_desired({});
}

void card_image_consumer::resolve_requests(bool retry_failed) {
    const auto previous = std::exchange(requests, {});
    resolution = {};
    unresolved = false;
    unresolved_required = false;
    std::vector<rasterflow::qt::image_request> desired;
    std::vector<rasterflow::qt::metadata_request> inspections;
    const auto resolve_element =
        [&](const rasterflow::qt::source_handle& source, const QString& logical,
            const card_image_demand& demand, bool& waiting) -> QString {
        if (!source.is_ready())
            return {};
        for (const auto& candidate : candidate_card_element_ids(logical)) {
            rasterflow::qt::metadata_request query {
                source,
                candidate,
                rasterflow::job_operation::inspect_element,
                demand.required,
                demand.priority,
                demand.deadline_us
            };
            inspections.push_back(query);
            if (!source.is_inspected(candidate)) {
                if (consumer->status_for(query).state
                    == rasterflow::qt::image_state::failed)
                    continue;
                waiting = true;
                return {};
            }
            if (source.contains(candidate))
                return candidate;
        }
        return {};
    };
    const auto wait_for = [this](const card_image_demand& demand) {
        unresolved = true;
        unresolved_required = unresolved_required || demand.required;
    };
    if (demands.empty()) {
        if (preferred_source.is_pending()) {
            preferred_source.clear();
            preferred_loaded = false;
        }
        if (fallback_source.is_pending()) {
            fallback_source.clear();
            fallback_loaded = false;
        }
    }
    if (!demands.empty() && !preferred_loaded) {
        preferred_source.load(preferred_path);
        preferred_loaded = true;
    }
    const auto prepared = preferred_source.source();
    const bool preferred_pending = preferred_source.is_pending()
        || (prepared.is_valid() && !prepared.is_ready()
            && prepared.error().isEmpty());
    for (const auto& demand : demands) {
        if (preferred_pending) {
            wait_for(demand);
            continue;
        }
        auto source = prepared;
        bool waiting = false;
        auto element
            = resolve_element(source, demand.logical_id, demand, waiting);
        if (waiting) {
            wait_for(demand);
            continue;
        }
        if (!element.isEmpty()) {
            ++resolution.active_theme_keys;
        } else if (preferred_path != fallback_path) {
            if (!fallback_loaded) {
                fallback_source.load(fallback_path);
                fallback_loaded = true;
            }
            const auto fallback = fallback_source.source();
            if (fallback_source.is_pending()
                || (fallback.is_valid() && !fallback.is_ready()
                    && fallback.error().isEmpty())) {
                wait_for(demand);
                continue;
            }
            source = fallback;
            element
                = resolve_element(source, demand.logical_id, demand, waiting);
            if (waiting) {
                wait_for(demand);
                continue;
            }
            if (!element.isEmpty())
                ++resolution.default_theme_keys;
        }
        if (element.isEmpty()) {
            ++resolution.placeholder_keys;
            continue;
        }
        const auto base_id
            = resolve_element(source, QStringLiteral("base"), demand, waiting);
        if (waiting) {
            wait_for(demand);
            continue;
        }
        rasterflow::qt::image_request request {
            .source = source,
            .element_id = element,
            .physical_size = demand.physical_size,
            .required = demand.required,
            .priority = demand.priority,
            .deadline_us = demand.deadline_us,
            .viewport = card_viewport(source, element, base_id)
        };
        requests.insert(demand.logical_id, request);
        desired.push_back(std::move(request));
    }
    if (retry_failed || previous != requests
        || submitted_inspections != inspections) {
        submitted_inspections = inspections;
        consumer->replace_desired(
            std::move(desired), std::move(inspections), retry_failed
        );
    }
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
    return preferred_loaded && !preferred_source.is_pending()
        && (!preferred_source.source().is_valid()
            || !preferred_source.error().isEmpty());
}

card_sheet_fallback_resolution card_image_consumer::source_resolution() const {
    return resolution;
}
