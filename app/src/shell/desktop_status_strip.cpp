#include "shell/desktop_status_strip.hpp"

#include "arch/str_label.hpp"
#include "settings/preferences.hpp"
#include "settings/theme_settings.hpp"

#include <QEvent>
#include <QFontMetricsF>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QProgressBar>
#include <QSlider>
#include <QStyle>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace status_strip_support {

// D-HUD-1 typography over the unchanged plain text. The shell keeps writing
// "Caption: value" entries separated by two spaces; the instrument strip only
// paints them as spaced captions and tabular figures, so text() (and every
// consumer of it) stays exactly what Classic shows. Text that does not follow
// that shape is painted as-is.
class instrument_label final : public QLabel {
public:
    // headline: first entry is the state (dot + upper-case value).
    explicit instrument_label(bool headline, QWidget* parent)
        : QLabel(parent)
        , headline(headline) { }

    // An entry repeated elsewhere in the strip (the Qt shell's clock).
    void set_omitted_caption(const QString& caption) {
        omitted_caption = caption;
        updateGeometry();
    }

    // The kit's shorter caption for an entry; text() keeps the full one.
    void set_caption_alias(const QString& caption, const QString& shown) {
        alias_caption = caption;
        alias_shown = shown;
        updateGeometry();
    }

    void set_instruments(bool enabled) {
        instruments = enabled;
        setProperty("instrument_paint", enabled);
        updateGeometry();
        update();
    }

    // Measured at the tightest divider gap, so the strip stays one row until
    // even that cannot fit; painting widens the gaps back when there is room.
    QSize sizeHint() const override {
        if (!painting())
            return QLabel::sizeHint();
        return { static_cast<int>(std::ceil(layout_entries(nullptr, tight_gap)))
                     + 2,
                 row_height() };
    }

    QSize minimumSizeHint() const override {
        if (!painting())
            return QLabel::minimumSizeHint();
        return { std::min(sizeHint().width(), 96), row_height() };
    }

    bool hasHeightForWidth() const override {
        return painting() ? false : QLabel::hasHeightForWidth();
    }

    int heightForWidth(int width) const override {
        return painting() ? row_height() : QLabel::heightForWidth(width);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (!painting()) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        layout_entries(&painter, divider_gap());
    }

    void changeEvent(QEvent* event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange)
            updateGeometry();
    }

private:
    struct entry {
        QString caption;
        QString value;
    };

    bool headline = false;
    bool instruments = false;
    QString omitted_caption;
    QString alias_caption;
    QString alias_shown;

    bool painting() const { return instruments && !entries().isEmpty(); }

    int row_height() const { return 30; }

    // Space around a headline divider: the kit's 16px padding on each side,
    // down to 8px before the strip wraps.
    static constexpr qreal natural_gap = 31.0;
    static constexpr qreal tight_gap = 17.0;

    qreal divider_gap() const {
        const qsizetype dividers = headline ? entries().size() - 1 : 0;
        if (dividers <= 0)
            return natural_gap;
        const qreal natural = layout_entries(nullptr, natural_gap);
        if (natural <= width())
            return natural_gap;
        return std::max(
            tight_gap,
            natural_gap - (natural - width()) / static_cast<qreal>(dividers)
        );
    }

    QList<entry> entries() const {
        QList<entry> result;
        for (const QString& part :
             text().split(QStringLiteral("  "), Qt::SkipEmptyParts)) {
            const auto colon = part.indexOf(QStringLiteral(": "));
            if (colon <= 0)
                return {};
            const QString caption = part.left(colon).trimmed();
            if (!result.isEmpty() && caption == omitted_caption)
                continue;
            result.append({ caption, part.mid(colon + 2).trimmed() });
        }
        return result;
    }

    // Lays the entries out left to right; paints when a painter is given and
    // returns the consumed width either way, so measuring matches painting.
    qreal layout_entries(QPainter* painter, qreal gap) const {
        const felt_material m = theme_settings::felt();
        QFont caption_font(m.mono_family);
        caption_font.setPixelSize(10);
        caption_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.7);
        // Headline figures are 17px; a secondary readout ("PICKUP 300ms")
        // is set entirely in the caption style.
        QFont value_font(headline ? QFont(m.mono_family) : caption_font);
        if (headline)
            value_font.setPixelSize(17);
        QFont state_font(m.mono_family);
        state_font.setPixelSize(11);
        state_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.8);
        const QFontMetricsF caption_metrics(caption_font);
        const QFontMetricsF value_metrics(value_font);
        const QFontMetricsF state_metrics(state_font);
        const qreal baseline = height() / 2.0 + value_metrics.capHeight() / 2.0;
        QColor divider = m.gold;
        divider.setAlphaF(0.22f);
        const auto entry_list = entries();
        qreal x = 0.0;
        for (qsizetype i = 0; i < entry_list.size(); ++i) {
            const auto& [caption, value] = entry_list.at(i);
            if (i > 0 && headline) {
                if (painter) {
                    painter->fillRect(
                        QRectF(
                            x + (gap - 1.0) / 2.0, height() / 2.0 - 10.0, 1.0,
                            20.0
                        ),
                        divider
                    );
                }
                x += gap;
            }
            if (headline && i == 0) {
                // State: a lit dot and the spaced value.
                const QString state = value.toUpper();
                if (painter) {
                    QColor glow = m.right;
                    glow.setAlphaF(0.35f);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(glow);
                    painter->drawEllipse(
                        QPointF(x + 3.5, height() / 2.0), 6.0, 6.0
                    );
                    painter->setBrush(m.right);
                    painter->drawEllipse(
                        QPointF(x + 3.5, height() / 2.0), 3.5, 3.5
                    );
                    painter->setFont(state_font);
                    painter->setPen(m.text);
                    painter->drawText(
                        QPointF(
                            x + 15.0,
                            height() / 2.0 + state_metrics.capHeight() / 2.0
                        ),
                        state
                    );
                }
                x += 15.0 + state_metrics.horizontalAdvance(state);
                continue;
            }
            const QString shown_caption
                = (!alias_caption.isEmpty() && caption == alias_caption
                       ? alias_shown
                       : caption)
                      .toUpper();
            if (painter) {
                painter->setFont(caption_font);
                painter->setPen(m.dim);
                painter->drawText(QPointF(x, baseline), shown_caption);
            }
            x += caption_metrics.horizontalAdvance(shown_caption) + 7.0;
            // "14/16": the denominator recedes, as in the kit.
            const auto slash = value.indexOf(QLatin1Char('/'));
            const QString lead = slash > 0 ? value.left(slash) : value;
            const QString tail = slash > 0 ? value.mid(slash) : QString();
            if (painter) {
                painter->setFont(value_font);
                painter->setPen(headline ? m.paper : m.dim);
                painter->drawText(QPointF(x, baseline), lead);
            }
            x += value_metrics.horizontalAdvance(lead);
            if (!tail.isEmpty()) {
                if (painter) {
                    painter->setPen(m.dim);
                    painter->drawText(QPointF(x, baseline), tail);
                }
                x += value_metrics.horizontalAdvance(tail);
            }
        }
        return x;
    }
};

// D-HUD-1's ticked speed tape: the same slider, painted as a bar row.
class tape_slider final : public QSlider {
public:
    explicit tape_slider(QWidget* parent)
        : QSlider(Qt::Horizontal, parent) { }

    void set_tape(bool enabled) {
        tape = enabled;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override {
        return tape ? QSize(96, 26) : QSlider::sizeHint();
    }

    QSize minimumSizeHint() const override {
        return tape ? QSize(minimumWidth(), 26) : QSlider::minimumSizeHint();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (!tape) {
            QSlider::paintEvent(event);
            return;
        }
        const felt_material m = theme_settings::felt();
        QPainter painter(this);
        constexpr int bars = 10;
        constexpr qreal gap = 3.0;
        const QRectF area = QRectF(rect()).adjusted(1.0, 2.0, -1.0, -2.0);
        const qreal bar_width = (area.width() - gap * (bars - 1)) / bars;
        const qreal span = std::max(1, maximum() - minimum());
        const qreal fraction = (value() - minimum()) / span;
        const int lit = static_cast<int>(std::round(fraction * (bars - 1)));
        QColor unlit = m.paper;
        unlit.setAlphaF(isEnabled() ? 0.14f : 0.07f);
        QColor trail = m.gold;
        trail.setAlphaF(isEnabled() ? 0.75f : 0.3f);
        static constexpr std::array<qreal, 4> heights { 6.0, 9.0, 6.0, 12.0 };
        for (int i = 0; i < bars; ++i) {
            const qreal h = heights.at(static_cast<std::size_t>(i % 4))
                * area.height() / 14.0;
            const QRectF bar(
                area.left() + i * (bar_width + gap), area.bottom() - h,
                bar_width, h
            );
            painter.fillRect(
                bar,
                i == lit ? (isEnabled() ? m.gold : trail)
                         : (i < lit ? trail : unlit)
            );
        }
        if (hasFocus()) {
            painter.setPen(QPen(m.gold, 1.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }

private:
    bool tape = false;
};

// Measure and place using the same width, including before the native status
// bar assigns geometry. Changing a box/grid direction in resizeEvent is too
// late: its old height-for-width can already have enlarged the main window.
class desktop_status_layout final : public QLayout {
public:
    explicit desktop_status_layout(QWidget* parent)
        : QLayout(parent) {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(8);
    }

    ~desktop_status_layout() override {
        while (auto* item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem* item) override { fields.append(item); }

    int count() const override { return static_cast<int>(fields.size()); }

    QLayoutItem* itemAt(int index) const override {
        return fields.value(index);
    }

    QLayoutItem* takeAt(int index) override {
        return index >= 0 && index < count() ? fields.takeAt(index) : nullptr;
    }

    Qt::Orientations expandingDirections() const override {
        return Qt::Horizontal;
    }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override {
        return arrange(QRect(0, 0, width, 0), false);
    }

    QSize sizeHint() const override {
        int width = 0;
        for (int i = 0; i < count(); ++i)
            if (visible(i))
                width += preferred_width(i) + (width ? spacing() : 0);
        width += contentsMargins().left() + contentsMargins().right();
        return QSize(width, heightForWidth(width));
    }

    QSize minimumSize() const override {
        // Narrow composition: status, pickup label + slider, progress + clock.
        const auto minimum_width = [this](int i) {
            return visible(i) ? itemAt(i)->minimumSize().width() : 0;
        };
        const auto pair_width = [&](int first, int second) {
            return minimum_width(first) + minimum_width(second)
                + (visible(first) && visible(second) ? spacing() : 0);
        };
        int height = 0;
        for (const auto* item : fields)
            if (!item->isEmpty())
                height = std::max(height, item->minimumSize().height());
        const auto margins = contentsMargins();
        return QSize(
            std::max({ minimum_width(0), pair_width(2, 3), pair_width(1, 4) })
                + margins.left() + margins.right(),
            height + margins.top() + margins.bottom()
        );
    }

    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    // Field order: status, progress, pickup readout, slider, clock.
    QList<QLayoutItem*> fields;

    bool visible(int index) const {
        return itemAt(index) && !itemAt(index)->isEmpty();
    }

    int preferred_width(int index) const {
        auto* item = itemAt(index);
        int width = item->sizeHint().width();
        // Fractional advances (e.g. styled monospace) must not wrap a label
        // that was measured as fitting.
        if (auto* label = qobject_cast<QLabel*>(item->widget());
            label && !label->property("instrument_paint").toBool())
            width = static_cast<int>(
                        std::ceil(QFontMetricsF(label->font())
                                      .horizontalAdvance(label->text()))
                    )
                + 2;
        return std::clamp(
            width, item->minimumSize().width(), item->maximumSize().width()
        );
    }

    int arrange(const QRect& bounds, bool place) const {
        const auto margins = contentsMargins();
        const auto rect = bounds.marginsRemoved(margins);
        const int width = std::max(0, rect.width());
        int controls_width = 0;
        for (int i = 1; i < count(); ++i)
            if (visible(i))
                controls_width
                    += preferred_width(i) + (controls_width ? spacing() : 0);
        const bool separate_status = visible(0)
            && preferred_width(0) + controls_width + spacing() > width;
        const bool wrap_controls = controls_width > width;
        int y = rect.y();
        // The expanding field takes remaining space; all other fields retain
        // their preferred width. A long readout wraps alongside the slider.
        const auto put_row = [&](std::initializer_list<int> indexes,
                                 int expanding) {
            QList<int> row;
            int fixed_width = 0;
            for (const int i : indexes) {
                if (!visible(i))
                    continue;
                row.append(i);
                if (i != expanding)
                    fixed_width += preferred_width(i);
            }
            if (row.isEmpty())
                return;
            fixed_width += (static_cast<int>(row.size()) - 1) * spacing();
            int row_height = 0;
            for (const int i : row) {
                auto* item = itemAt(i);
                const int item_width = i == expanding
                    ? std::max(0, width - fixed_width)
                    : preferred_width(i);
                const int height = item->hasHeightForWidth()
                    ? item->heightForWidth(item_width)
                    : item->sizeHint().height();
                row_height = std::max(
                    row_height, std::max(item->minimumSize().height(), height)
                );
            }
            if (place) {
                int x = rect.x();
                for (const int i : row) {
                    const int item_width = i == expanding
                        ? std::max(0, width - fixed_width)
                        : preferred_width(i);
                    itemAt(i)->setGeometry(QStyle::visualRect(
                        parentWidget()->layoutDirection(), rect,
                        QRect(x, y, item_width, row_height)
                    ));
                    x += item_width + spacing();
                }
            }
            y += row_height + spacing();
        };
        if (!separate_status && !wrap_controls) {
            put_row({ 0, 1, 2, 3, 4 }, 0);
        } else {
            put_row({ 0 }, 0);
            if (wrap_controls) {
                put_row({ 2, 3 }, 2);
                put_row({ 1, 4 }, -1);
            } else {
                put_row({ 1, 2, 3, 4 }, -1);
            }
        }
        return std::max(0, y - rect.y() - spacing()) + margins.top()
            + margins.bottom();
    }
};

} // namespace status_strip_support

desktop_status_strip::desktop_status_strip(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("desktop_status_surface"));
    row = new status_strip_support::desktop_status_layout(this);
    status = new status_strip_support::instrument_label(true, this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    progress = new QProgressBar(this);
    progress->setTextVisible(false);
    progress->setRange(0, 0);
    progress->setMinimumWidth(40);
    progress->setMaximumWidth(120);
    progress->setAccessibleName(str_label("Preparing card images"));
    progress->hide();
    readout = new status_strip_support::instrument_label(false, this);
    readout->setTextFormat(Qt::PlainText);
    readout->setWordWrap(true);
    static_cast<status_strip_support::instrument_label*>(readout)
        ->set_caption_alias(str_label("Pickup interval"), str_label("Pickup"));
    slider = new status_strip_support::tape_slider(this);
    slider->setRange(
        trainer_preferences::minimum_pickup_interval_ms,
        trainer_preferences::maximum_pickup_interval_ms
    );
    slider->setValue(300);
    slider->setMinimumWidth(96);
#ifdef KC_KDE
    slider->setMaximumWidth(160);
#else
    slider->setMaximumWidth(180);
#endif
    // KDE always shows the clock; the Qt shell shows it only as the
    // instrument strip's right-hand figure.
    clock = new QLabel(str_label("00:00:00"), this);
    clock->setObjectName(QStringLiteral("session_clock"));
#ifndef KC_KDE
    clock->hide();
    static_cast<status_strip_support::instrument_label*>(status)
        ->set_omitted_caption(str_label("Time"));
#endif
    slider->setAccessibleName(str_label("Card pickup interval (ms)"));
    slider->setToolTip(str_label("Card pickup interval (ms)"));
    readout->setBuddy(slider);
    row->addWidget(status);
    row->addWidget(progress);
    row->addWidget(readout);
    row->addWidget(slider);
    if (clock)
        row->addWidget(clock);
}

void desktop_status_strip::set_instruments(bool enabled) {
    setFrameShape(enabled ? QFrame::StyledPanel : QFrame::NoFrame);
    setFrameShadow(QFrame::Plain);
    const int margin = enabled ? 6 : 0;
    row->setContentsMargins(margin, margin, margin, margin);
    auto label_font = font();
    if (enabled)
        label_font.setBold(true);
    status->setFont(label_font);
    slider->setTickPosition(enabled ? QSlider::TicksBelow : QSlider::NoTicks);
    slider->setTickInterval(100);
    // D-HUD-1: a dark 44px instrument strip with tabular figures, whichever
    // window chrome surrounds it.
    setStyleSheet(enabled ? theme_settings::felt_hud_style_sheet() : QString());
    static_cast<status_strip_support::instrument_label*>(status)
        ->set_instruments(enabled);
    static_cast<status_strip_support::instrument_label*>(readout)
        ->set_instruments(enabled);
    static_cast<status_strip_support::tape_slider*>(slider)->set_tape(enabled);
#ifndef KC_KDE
    clock->setVisible(enabled);
#endif
    if (enabled) {
        // D-HUD-1: 14px ends, 10px between the pickup group's parts.
        row->setContentsMargins(14, 7, 14, 7);
        row->setSpacing(10);
    } else {
        row->setSpacing(8);
    }
    row->invalidate();
}
