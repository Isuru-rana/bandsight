// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QSet>
#include <QVector>

#include "frame_types.h"

namespace bandsight::gui {

// The 5-minute window's only data source (spec §4.3): live frames in, never a
// query. Plain class, no QObject, so all of the graph's logic is unit-testable
// and GraphTab stays glue.
class LiveRing {
public:
    explicit LiveRing(int capacity = 300)
        : capacity_(capacity > 0 ? capacity : 1) {}

    // Sums the frame's interfaces, restricted to `visible`.
    //
    // The daemon emits a sample frame every tick but omits interfaces whose
    // delta was zero, so an interface absent from the array moved nothing -
    // absence is a size optimisation, not missing data (spec §4.3). Every frame
    // therefore appends exactly one point, and a frame with no interfaces at
    // all appends a zero. Skipping it instead would leave the chart to draw a
    // straight line across the idle stretch and invent traffic that never
    // happened.
    void append(const SampleFrame& f, const QSet<int>& visible);

    // Dropped wholesale on disconnect: the buffer holds a contiguous
    // second-by-second history, and a gap it cannot represent must not be
    // papered over by joining the two ends (spec §8.1).
    void clear() { points_.clear(); }

    QVector<SeriesPoint> points() const { return points_; }   // oldest -> newest

    // Fills in what happened BEFORE the GUI was listening, from the daemon's
    // per-second history. Only points older than the oldest live one are taken:
    // where the two overlap the live frame is kept, because it is what this
    // process actually observed and the history row for the current second may
    // still be accumulating. Capacity is respected from the old end.
    void seed(const QVector<SeriesPoint>& history);

    // The newest points covering `seconds`, for a window shorter than the ring.
    QVector<SeriesPoint> tail(qint64 seconds) const;

private:
    int capacity_;
    QVector<SeriesPoint> points_;
};

}  // namespace bandsight::gui
