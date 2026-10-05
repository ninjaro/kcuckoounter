#include "table/card_widget_fixture.hpp"

#include "arch/str_label.hpp"
#include "card_helpers/card_sheet.hpp"
#include "table/card_widget.hpp"
#include "table/gameplay_generation.hpp"

#include <QtTest/QtTest>
#include <array>

namespace card_test {

gameplay::session renderer_session(bool training) {
    auto settings = gameplay::session_configuration {};
    settings.failure = gameplay::failure_policy::block;
    settings.global_quiz_pause = false;
    settings.dealing = gameplay::dealing_mode::simultaneous;
    auto owner
        = gameplay::session::create(
              settings,
              { { "test", 1, false, training }, { "test", 1, false, false } },
              { 2, 64 }
        )
              .value();
    gameplay::prepared_deck first;
    first.cards = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    first.rank_weights[0] = -1;
    first.rank_weights[1] = 2;
    first.initial_running_count = -(std::int64_t { 1 } << 40);
    gameplay::prepared_deck second;
    second.cards = { 12, 11, 10, 9, 8, 7, 6, 5, 4, 3 };
    if (!owner.prepare_decks(std::array { first, second })
        || !owner.set_traversal(std::array<std::size_t, 2> { 0, 1 }))
        qFatal("Invalid renderer fixture");
    return owner;
}

void supply_renderer_faces(card_widget& widget) {
    QVector<QImage> faces;
    for (qsizetype index = 0; index <= card_element_ids().size(); ++index) {
        QImage face(24, 36, QImage::Format_ARGB32_Premultiplied);
        face.fill(index == card_element_ids().size() ? Qt::blue : Qt::white);
        faces.push_back(face);
    }
    widget.set_shared_card_faces_mode(true);
    widget.set_shared_card_faces(faces, QSize(24, 36));
    widget.resize(260, 360);
    widget.set_slot_rotated(true);
}

QImage render_card(card_widget& widget) {
    QImage result(widget.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    widget.render(&result);
    return result;
}

qint64 pixel_area(const QSize& size) {
    return static_cast<qint64>(size.width())
        * static_cast<qint64>(size.height());
}

} // namespace card_test
