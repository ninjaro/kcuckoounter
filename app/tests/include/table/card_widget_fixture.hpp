#ifndef KCUCKOOUNTER_TESTS_CARD_WIDGET_FIXTURE_HPP
#define KCUCKOOUNTER_TESTS_CARD_WIDGET_FIXTURE_HPP

#include "table/card_widget.hpp"

namespace card_test {

gameplay::session renderer_session(bool training = true);
void supply_renderer_faces(card_widget& widget);
QImage render_card(card_widget& widget);
qint64 pixel_area(const QSize& size);

} // namespace card_test

#endif // KCUCKOOUNTER_TESTS_CARD_WIDGET_FIXTURE_HPP
