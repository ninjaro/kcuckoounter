#include "settings/gameplay_recovery_codec.hpp"

#include "settings/gameplay_codec.hpp"
#include "settings/gameplay_progress_codec.hpp"

#include <QJsonDocument>
#include <utility>

namespace recovery_storage {

std::optional<QByteArray> encode_active(
    std::uint64_t sequence, const gameplay_checkpoint& checkpoint,
    gameplay::slot_limits limits, const gameplay::generation_policy& resources
) {
    if (sequence == 0)
        return std::nullopt;
    const auto payload
        = encode_gameplay_checkpoint(checkpoint, limits, resources);
    if (!payload)
        return std::nullopt;
    const QJsonObject root {
        { QStringLiteral("version"),
          gameplay_recovery_service::schema_version },
        { QStringLiteral("phase"), QStringLiteral("active") },
        { QStringLiteral("sequence"), gameplay_storage::number(sequence) },
        { QStringLiteral("payload"),
          QJsonDocument::fromJson(*payload).object() },
    };
    auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return bytes.size() <= gameplay_recovery_service::maximum_document_bytes
        ? std::make_optional(std::move(bytes))
        : std::nullopt;
}

} // namespace recovery_storage

std::optional<QByteArray> encode_gameplay_recovery(
    const gameplay_recovery& recovery, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources
) {
    if (!gameplay_storage::valid_limits(limits) || !resources.valid())
        return std::nullopt;
    if (const auto* active = std::get_if<gameplay_active_record>(&recovery)) {
        return recovery_storage::encode_active(
            active->sequence, active->checkpoint, limits, resources
        );
    }
    const auto& pending = std::get<gameplay_result_record>(recovery);
    if (!progress_storage::valid_record(pending, limits))
        return std::nullopt;
    const QJsonObject root {
        { QStringLiteral("version"),
          gameplay_recovery_service::schema_version },
        { QStringLiteral("phase"), QStringLiteral("pending") },
        { QStringLiteral("sequence"),
          gameplay_storage::number(pending.sequence) },
        { QStringLiteral("payload"),
          QJsonObject {
              { QStringLiteral("completed_at_utc_ms"),
                gameplay_storage::signed_number(pending.completed_at_utc_ms) },
              { QStringLiteral("result"),
                progress_storage::result(pending.result) },
          } },
    };
    auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return bytes.size() <= gameplay_recovery_service::maximum_document_bytes
        ? std::make_optional(std::move(bytes))
        : std::nullopt;
}

std::optional<gameplay_recovery> decode_gameplay_recovery(
    const QByteArray& bytes, gameplay::slot_limits limits,
    const gameplay::generation_policy& resources
) {
    if (!gameplay_storage::valid_limits(limits) || !resources.valid()
        || bytes.isEmpty()
        || bytes.size() > gameplay_recovery_service::maximum_document_bytes)
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject())
        return std::nullopt;
    const auto root = document.object();
    const auto sequence = gameplay_storage::read_unsigned(
        root.value(QStringLiteral("sequence"))
    );
    const auto phase = root.value(QStringLiteral("phase"));
    const auto payload = root.value(QStringLiteral("payload"));
    if (root.size() != 4 || !sequence || *sequence == 0 || !phase.isString()
        || !payload.isObject()
        || !gameplay_storage::read_integer(
            root.value(QStringLiteral("version")),
            gameplay_recovery_service::schema_version,
            gameplay_recovery_service::schema_version
        ))
        return std::nullopt;
    if (phase.toString() == QStringLiteral("active")) {
        auto checkpoint = decode_gameplay_checkpoint(
            QJsonDocument(payload.toObject()).toJson(QJsonDocument::Compact),
            limits, resources
        );
        return checkpoint ? std::make_optional(
                                gameplay_recovery { gameplay_active_record {
                                    *sequence, std::move(*checkpoint) } }
                            )
                          : std::nullopt;
    }
    if (phase.toString() != QStringLiteral("pending"))
        return std::nullopt;
    const auto object = payload.toObject();
    const auto timestamp = gameplay_storage::read_signed(
        object.value(QStringLiteral("completed_at_utc_ms"))
    );
    auto result = progress_storage::read_result(
        object.value(QStringLiteral("result")), limits
    );
    if (object.size() != 2 || !timestamp || *timestamp <= 0 || !result)
        return std::nullopt;
    return gameplay_recovery { gameplay_result_record { *sequence, *timestamp,
                                                        std::move(*result) } };
}
