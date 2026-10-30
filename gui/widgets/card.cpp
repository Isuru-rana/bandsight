// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/card.h"

#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "theme.h"

namespace bandsight::gui {

Card::Card(const QString& title, QWidget* parent)
    : QWidget(parent), title_(new QLabel(title, this)) {
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(8);
    outer->addWidget(title_);
    body_ = new QVBoxLayout;
    body_->setContentsMargins(0, 0, 0, 0);
    outer->addLayout(body_, 1);
    applyInk();
}

QString Card::title() const { return title_->text(); }

void Card::setBody(QWidget* w) {
    body_->addWidget(w);
    w->setParent(this);
}

void Card::setReserved(const QString& reason) {
    reserved_ = true;
    auto* note = new QLabel(reason, this);
    note->setWordWrap(true);
    note->setAlignment(Qt::AlignCenter);
    note->setObjectName(QStringLiteral("reservedNote"));
    body_->addStretch();
    body_->addWidget(note);
    body_->addStretch();
    applyInk();
}

void Card::paintEvent(QPaintEvent*) {
    const Theme t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                        Theme::radius_card, Theme::radius_card);
    p.fillPath(path, t.card);
    p.strokePath(path, QPen(t.border, 1));
}

// Never from paintEvent: setStyleSheet re-polishes the label, which schedules
// another paint of this card, so styling there repainted forever and pinned a
// core. A desktop theme switch arrives as PaletteChange, which re-runs this.
void Card::changeEvent(QEvent* e) {
    if (e->type() == QEvent::PaletteChange) applyInk();
    QWidget::changeEvent(e);
}

// Recessive, not absent: a reserved column still occupies its space and still
// says what it is.
void Card::applyInk() {
    const Theme t = theme();
    title_->setStyleSheet(QStringLiteral("color:%1;")
                              .arg((reserved_ ? t.ink_faint : t.ink).name()));
    if (auto* note = findChild<QLabel*>(QStringLiteral("reservedNote")))
        note->setStyleSheet(QStringLiteral("color:%1;").arg(t.ink_faint.name()));
    update();
}

}  // namespace bandsight::gui
