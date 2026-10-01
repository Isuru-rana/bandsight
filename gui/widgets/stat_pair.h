// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>

namespace bandsight::gui {

// The ↓ / ↑ figures under the donut, each with a bar proportional to the larger
// of the two. Redundant with the donut by design: the ring shows the split, this
// shows the magnitudes, and reading a byte count off an arc is not a thing
// anyone can do.
class StatPair : public QWidget {
    Q_OBJECT
public:
    explicit StatPair(QWidget* parent = nullptr);

    void setValues(quint64 down, quint64 up);
    QString downText() const;
    QString upText() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    quint64 down_ = 0;
    quint64 up_ = 0;
};

}  // namespace bandsight::gui
