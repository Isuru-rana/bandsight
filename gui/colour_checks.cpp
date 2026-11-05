// SPDX-License-Identifier: GPL-3.0-or-later
#include "colour_checks.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bandsight::gui {

namespace {

double linear(double c8) {
    const double c = c8 / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double encode(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return 255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055);
}

double luminance(const QColor& c) {
    return 0.2126 * linear(c.red()) + 0.7152 * linear(c.green()) + 0.0722 * linear(c.blue());
}

using Mat = std::array<std::array<double, 3>, 3>;
constexpr Mat kProtan{{{0.152286, 1.052583, -0.204868},
                       {0.114503, 0.786281, 0.099216},
                       {-0.003882, -0.048116, 1.051998}}};
constexpr Mat kDeutan{{{0.367322, 0.860646, -0.227968},
                       {0.280085, 0.672501, 0.047413},
                       {-0.011820, 0.042940, 0.968881}}};
constexpr Mat kTritan{{{1.255528, -0.076749, -0.178779},
                       {-0.078411, 0.930809, 0.147602},
                       {0.004733, 0.691367, 0.303900}}};

// 8-bit sRGB as seen with the given vision, applied in linear light.
std::array<double, 3> simulate(const QColor& c, Vision v) {
    const Mat* m = v == Vision::Protan   ? &kProtan
                   : v == Vision::Deutan ? &kDeutan
                   : v == Vision::Tritan ? &kTritan
                                         : nullptr;
    if (!m) return {double(c.red()), double(c.green()), double(c.blue())};
    const std::array<double, 3> lin{linear(c.red()), linear(c.green()), linear(c.blue())};
    std::array<double, 3> out{};
    for (int i = 0; i < 3; ++i)
        out[i] = encode((*m)[i][0] * lin[0] + (*m)[i][1] * lin[1] + (*m)[i][2] * lin[2]);
    return out;
}

std::array<double, 3> oklab(const std::array<double, 3>& rgb8) {
    const double r = linear(rgb8[0]), g = linear(rgb8[1]), b = linear(rgb8[2]);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

}  // namespace

double contrast_ratio(const QColor& a, const QColor& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

double delta_e(const QColor& a, const QColor& b, Vision v) {
    const auto x = oklab(simulate(a, v)), y = oklab(simulate(b, v));
    return 100.0 * std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) +
                             (x[2] - y[2]) * (x[2] - y[2]));
}

}  // namespace bandsight::gui
