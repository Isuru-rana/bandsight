// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>

namespace bandsight::gui {

// Total volume for the window, split download/upload, with the total in the
// hole. A donut rather than a pie because the hole carries the headline number -
// a pie needs that label outside it, where it competes with the slices.
//
// Two slices only, ever. It is a split, not a category breakdown.
class DonutChart : public QWidget {
    Q_OBJECT
public:
    explicit DonutChart(QWidget* parent = nullptr);

    void setValues(quint64 down, quint64 up);
    quint64 down() const { return down_; }
    quint64 up() const { return up_; }

    // Test-only: what the hole shows, which is the whole point of the widget.
    QString centerText() const;

    QSize sizeHint() const override { return {180, 180}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    quint64 down_ = 0;
    quint64 up_ = 0;
};

}  // namespace bandsight::gui
