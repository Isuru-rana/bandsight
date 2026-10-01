// SPDX-License-Identifier: GPL-3.0-or-later
#include "smooth.h"

#include <cmath>

namespace bandsight::gui {

QVector<QPointF> monotone_smooth(const QVector<QPointF>& p, int per_segment) {
    const int n = int(p.size());
    if (n < 2 || per_segment <= 0) return p;

    // Secant slopes between neighbours.
    QVector<double> d(n - 1);
    for (int i = 0; i + 1 < n; ++i)
        d[i] = (p[i + 1].y() - p[i].y()) / (p[i + 1].x() - p[i].x());

    // Tangents: the average of neighbouring secants, zeroed at any local extremum
    // or flat run. Zeroing there is what keeps a peak at the peak and a floor on
    // the floor - an ordinary spline carries momentum through them, which is
    // exactly the overshoot.
    QVector<double> m(n);
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (int i = 1; i + 1 < n; ++i)
        m[i] = (d[i - 1] * d[i] <= 0.0) ? 0.0 : (d[i - 1] + d[i]) / 2.0;

    // Fritsch-Carlson limit: where a tangent is too steep relative to its secant,
    // scale the pair down so the segment cannot bow outside its endpoints.
    for (int i = 0; i + 1 < n; ++i) {
        if (d[i] == 0.0) { m[i] = 0.0; m[i + 1] = 0.0; continue; }
        const double a = m[i] / d[i];
        const double b = m[i + 1] / d[i];
        // A tangent pointing against its own secant would bow the curve outside
        // the segment on its own, before any steepness is considered.
        if (a < 0.0) m[i] = 0.0;
        if (b < 0.0) m[i + 1] = 0.0;
        const double h = a * a + b * b;
        if (h > 9.0) {
            const double t = 3.0 / std::sqrt(h);
            m[i] = t * a * d[i];
            m[i + 1] = t * b * d[i];
        }
    }

    QVector<QPointF> out;
    out.reserve((n - 1) * (per_segment + 1) + 1);
    for (int i = 0; i + 1 < n; ++i) {
        const double x0 = p[i].x(), x1 = p[i + 1].x();
        const double y0 = p[i].y(), y1 = p[i + 1].y();
        const double h = x1 - x0;
        out.append(p[i]);   // the measurement itself, exactly
        for (int j = 1; j <= per_segment; ++j) {
            const double t = double(j) / double(per_segment + 1);
            const double t2 = t * t, t3 = t2 * t;
            // Cubic Hermite basis.
            const double y = (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * m[i] +
                             (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * h * m[i + 1];
            out.append(QPointF(x0 + t * h, y));
        }
    }
    out.append(p[n - 1]);
    return out;
}

}  // namespace bandsight::gui
