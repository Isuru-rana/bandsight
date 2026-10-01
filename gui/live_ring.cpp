// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_ring.h"

#include <algorithm>
#include <limits>

namespace bandsight::gui {

void LiveRing::append(const SampleFrame& f, const QSet<int>& visible) {
    SeriesPoint p{f.ts, 0, 0};
    for (const SampleIface& i : f.ifaces) {
        if (!visible.contains(i.id)) continue;
        p.rx += i.rx;
        p.tx += i.tx;
    }
    points_.append(p);
    // 300 elements, so a front erase per tick costs nothing worth an index.
    while (points_.size() > capacity_) points_.removeFirst();
}

void LiveRing::seed(const QVector<SeriesPoint>& history) {
    const qint64 oldest = points_.isEmpty() ? std::numeric_limits<qint64>::max()
                                            : points_.front().ts;
    QVector<SeriesPoint> older;
    for (const SeriesPoint& h : history)
        if (h.ts < oldest) older.append(h);
    if (older.isEmpty()) return;
    std::sort(older.begin(), older.end(),
              [](const SeriesPoint& a, const SeriesPoint& b) { return a.ts < b.ts; });

    QVector<SeriesPoint> merged = older;
    merged += points_;
    // Trim from the OLD end: the newest seconds are the ones on screen.
    while (merged.size() > capacity_) merged.removeFirst();
    points_ = merged;
}

QVector<SeriesPoint> LiveRing::tail(qint64 seconds) const {
    if (points_.isEmpty()) return {};
    const qint64 from = points_.back().ts - seconds;
    QVector<SeriesPoint> out;
    for (const SeriesPoint& p : points_)
        if (p.ts > from) out.append(p);
    return out;
}

}  // namespace bandsight::gui
