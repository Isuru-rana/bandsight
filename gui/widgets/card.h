// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace bandsight::gui {

// The panel chrome every column shares: a titled, rounded, hairline-bordered
// surface. Having exactly one of these is what stops the four columns drifting
// apart visually as they are edited separately.
class Card : public QWidget {
    Q_OBJECT
public:
    explicit Card(const QString& title, QWidget* parent = nullptr);

    // Fills the body. Takes ownership.
    void setBody(QWidget* w);

    // A column whose data does not exist yet: same chrome, dimmed, with one line
    // saying why. Reserved space that reads as deliberate rather than broken.
    void setReserved(const QString& reason);
    bool isReserved() const { return reserved_; }

    QString title() const;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QLabel* title_;
    QVBoxLayout* body_;
    bool reserved_ = false;
};

}  // namespace bandsight::gui
