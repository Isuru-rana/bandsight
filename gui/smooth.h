// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPointF>
#include <QVector>

namespace bandsight::gui {

// Smooth curve through data points, by monotone cubic interpolation
// (Fritsch-Carlson).
//
// Not a plain cubic spline, deliberately. A Catmull-Rom or natural spline -
// which is what QSplineSeries draws - overshoots: between an idle second and a
// spike it dips the curve BELOW zero and swings it ABOVE the real peak, so the
// graph shows negative throughput and traffic that never happened. The monotone
// variant cannot leave the range of the two points it joins, which is what
// makes "smooth" honest for a byte count.
//
// Every input point appears exactly in the output; `per_segment` points are
// inserted between each pair. x must be strictly increasing.
QVector<QPointF> monotone_smooth(const QVector<QPointF>& points, int per_segment);

}  // namespace bandsight::gui
