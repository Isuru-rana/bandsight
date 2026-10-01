// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QSet>
#include <QVector>
#include <QWidget>

#include "frame_types.h"

namespace bandsight::gui {

// The strip under the Graph tab's chart: what the window moved, and what is
// moving now.
//
//   ↓ download total   [ semicircle gauge, total ]   ↑ upload total  |  per-interface bars
//     now <rate>                                        now <rate>
//
// The totals are handed in by GraphTab as the sum of exactly the points the
// chart plots, so the card and the curve above it cannot disagree. The rates are
// the newest LIVE frame whatever the window, and are labelled "now" so a 30-day
// view is never read as reporting a 30-day average.
//
// The right-hand block is per interface. The app this is modelled on shows WAN /
// LAN there, which needs each connection's peer address - data the collector does
// not record yet. Interfaces are the real split we have, in the same slot.
class TotalsCard : public QWidget {
    Q_OBJECT
public:
    explicit TotalsCard(QWidget* parent = nullptr);

    void setTotals(quint64 down, quint64 up);
    void setRates(quint64 down_bps, quint64 up_bps);

    // Interface totals for the window. Only measured interfaces in `visible` are
    // shown, largest first: the same set the chart is summing.
    void setIfaces(const QVector<IfaceTotal>& totals, const QSet<int>& visible);

    quint64 downTotal() const { return down_; }
    quint64 upTotal() const { return up_; }
    quint64 downRate() const { return down_rate_; }
    quint64 upRate() const { return up_rate_; }
    // The rows actually drawn, in order. For tests.
    const QVector<IfaceTotal>& ifaceRows() const { return rows_; }

    // The gauge's download share in [0,1]; 0 when nothing moved, never NaN.
    double downShare() const;
    QString gaugeText() const;

    // Layout, shared by paint and tests so alignment is checked on the same
    // arithmetic that draws it.
    struct Geometry {
        QRect down, gauge, up;     // the three totals columns
        QRect list;                // interface rows block (empty without rows)
        QVector<QRect> rows;       // one per ifaceRows() entry
        int divider_x = -1;
    };
    Geometry geometry() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    quint64 down_ = 0, up_ = 0;
    quint64 down_rate_ = 0, up_rate_ = 0;
    QVector<IfaceTotal> rows_;
};

}  // namespace bandsight::gui
