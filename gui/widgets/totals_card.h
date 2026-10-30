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
//   ↓ download total   [ semicircle gauge, total ]   ↑ upload total  |  WAN / LAN bars
//     now <rate>                                        now <rate>
//
// The totals are handed in by GraphTab as the sum of exactly the points the
// chart plots, so the card and the curve above it cannot disagree. The rates are
// the newest LIVE frame whatever the window, and are labelled "now" so a 30-day
// view is never read as reporting a 30-day average.
//
// The right-hand block splits the window's traffic by where the peer is - WAN
// (the internet) or LAN (this network) - summed from the per-host pairs, the
// same data as the Usage screen's Hosts column. Bytes with no peer address are
// on neither side, so WAN + LAN can be slightly under the totals beside them.
class TotalsCard : public QWidget {
    Q_OBJECT
public:
    explicit TotalsCard(QWidget* parent = nullptr);

    void setTotals(quint64 down, quint64 up);
    void setRates(quint64 down_bps, quint64 up_bps);

    struct ZoneRow {
        QString label;
        quint64 rx = 0;
        quint64 tx = 0;
    };

    // Host pairs for the window. collected=false (a daemon older than host
    // collection) shows no bars, rather than two empty ones that read as "idle".
    void setHostPairs(const QVector<AppHostTotal>& pairs, bool collected);

    quint64 downTotal() const { return down_; }
    quint64 upTotal() const { return up_; }
    quint64 downRate() const { return down_rate_; }
    quint64 upRate() const { return up_rate_; }
    // The rows actually drawn, in order. For tests.
    const QVector<ZoneRow>& zoneRows() const { return rows_; }

    // The gauge's download share in [0,1]; 0 when nothing moved, never NaN.
    double downShare() const;
    QString gaugeText() const;

    // Layout, shared by paint and tests so alignment is checked on the same
    // arithmetic that draws it.
    struct Geometry {
        QRect down, gauge, up;     // the three totals columns
        QRect list;                // WAN/LAN rows block (empty without rows)
        QVector<QRect> rows;       // one per zoneRows() entry
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
    QVector<ZoneRow> rows_;
};

}  // namespace bandsight::gui
