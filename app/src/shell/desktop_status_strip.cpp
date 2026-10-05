#include "shell/desktop_status_strip.hpp"

#include "arch/str_label.hpp"
#include "settings/preferences.hpp"

#include <QLabel>
#include <QLayout>
#include <QProgressBar>
#include <QSlider>
#include <QStyle>

#include <algorithm>
#include <initializer_list>

namespace status_strip_support {

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
        if (auto* label = qobject_cast<QLabel*>(item->widget()))
            width = label->fontMetrics().horizontalAdvance(label->text());
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
                    itemAt(i)->setGeometry(
                        QStyle::visualRect(
                            parentWidget()->layoutDirection(), rect,
                            QRect(x, y, item_width, row_height)
                        )
                    );
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
    status = new QLabel(this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    progress = new QProgressBar(this);
    progress->setTextVisible(false);
    progress->setRange(0, 0);
    progress->setMinimumWidth(40);
    progress->setMaximumWidth(120);
    progress->setAccessibleName(str_label("Preparing card images"));
    progress->hide();
    readout = new QLabel(this);
    readout->setTextFormat(Qt::PlainText);
    readout->setWordWrap(true);
    slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(
        trainer_preferences::minimum_pickup_interval_ms,
        trainer_preferences::maximum_pickup_interval_ms
    );
    slider->setValue(300);
    slider->setMinimumWidth(96);
#ifdef KC_KDE
    slider->setMaximumWidth(160);
    clock = new QLabel(str_label("00:00:00"), this);
    clock->setObjectName(QStringLiteral("session_clock"));
#else
    slider->setMaximumWidth(180);
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
    row->invalidate();
}
