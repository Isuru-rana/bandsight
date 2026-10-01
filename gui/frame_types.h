// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace bandsight::gui {

struct HelloIfaceInfo { int id = 0; QString name; QString kind; };
struct HelloInfo { QString db_path; QVector<HelloIfaceInfo> ifaces; };

struct SampleIface { int id = 0; quint64 rx = 0; quint64 tx = 0; };
struct SampleFrame { qint64 ts = 0; QVector<SampleIface> ifaces; };

struct SeriesPoint { qint64 ts = 0; quint64 rx = 0; quint64 tx = 0; };
// No `measured` flag: measuredness is derived from `kind` wherever it is needed,
// through kind_is_measured(). Carrying it as a field made a second source of
// truth that production never consulted - an author setting it would find the
// table did not change, and one trusting it would have bypassed the honesty
// invariant spec §2.2 puts in the model (FOLLOWUP B7).
struct IfaceTotal {
    int id = 0; QString name; QString kind;
    quint64 rx = 0; quint64 tx = 0;
};

// One row of the Usage tab (spec §7): what one application moved over the
// window. exe is the identity the daemon recorded; name is its friendlier label
// and may be empty, which is why display_name() exists rather than a bare field.
struct AppTotal {
    int id = 0; QString exe; QString name;
    quint64 rx = 0; quint64 tx = 0;

    // Never empty: an application the resolver could not name still has to
    // occupy a readable row rather than a blank one.
    QString display_name() const {
        if (!name.isEmpty()) return name;
        if (!exe.isEmpty()) return exe.section(QLatin1Char('/'), -1);
        return QCoreApplication::translate("AppTotal", "(unknown)");
    }
};

// The selectable time windows, in the order they are offered.
//
// Resolution is chosen per window to keep the point count drawable - between 60
// and 1800 - and every choice sits inside the daemon's retention (1 s kept for an
// hour, 1 min for 30 days, 1 h for a year), so no window ever asks for a tier
// that has already been rolled away.
enum class Window { OneMin, FiveMin, ThirtyMin, Hour, SixHour, TwelveHour, Day, Week, Month };
struct WindowSpec { int res = 0; qint64 range_s = 0; };
inline WindowSpec window_spec(Window w) {
    switch (w) {
        case Window::OneMin:     return {1, 60};
        case Window::FiveMin:    return {1, 300};
        case Window::ThirtyMin:  return {1, 1800};
        case Window::Hour:       return {60, 3600};
        case Window::SixHour:    return {60, 6 * 3600};
        case Window::TwelveHour: return {60, 12 * 3600};
        case Window::Day:        return {60, 86400};
        case Window::Week:       return {3600, 7LL * 86400};
        case Window::Month:      return {3600, 30LL * 86400};
    }
    return {};
}

// Every window, in display order - the single list both selectors are built from.
inline const QVector<Window>& all_windows() {
    static const QVector<Window> w{Window::OneMin, Window::FiveMin, Window::ThirtyMin,
                                   Window::Hour, Window::SixHour, Window::TwelveHour,
                                   Window::Day, Window::Week, Window::Month};
    return w;
}

// Windows short enough to be served by the live ring once it is running.
//
// They are SEEDED from history when entered, which reverses the original rule
// that the 5-minute window was the live ring and never a query. Without the
// seed a live window starts empty at the moment the GUI opens and takes its whole
// length to fill, while the daemon has been holding per-second history for the
// last hour the entire time.
inline bool is_live(Window w) { return w == Window::OneMin || w == Window::FiveMin; }

// The one place these strings live. GraphTab labels its window combo from here
// and IfaceModel names the period in its byte headers from here, so the two can
// never drift into calling the same window different things.
inline QString window_label(Window w) {
    switch (w) {
        case Window::OneMin:     return QCoreApplication::translate("Window", "1 minute");
        case Window::FiveMin:    return QCoreApplication::translate("Window", "5 minutes");
        case Window::ThirtyMin:  return QCoreApplication::translate("Window", "30 minutes");
        case Window::Hour:       return QCoreApplication::translate("Window", "1 hour");
        case Window::SixHour:    return QCoreApplication::translate("Window", "6 hours");
        case Window::TwelveHour: return QCoreApplication::translate("Window", "12 hours");
        case Window::Day:        return QCoreApplication::translate("Window", "24 hours");
        case Window::Week:       return QCoreApplication::translate("Window", "7 days");
        case Window::Month:      return QCoreApplication::translate("Window", "30 days");
    }
    return {};
}

inline bool kind_is_measured(const QString& kind) {
    return kind == QLatin1String("physical") || kind == QLatin1String("wifi");
}

}  // namespace bandsight::gui

// QSignalSpy needs these registered to store copies of the signal arguments.
Q_DECLARE_METATYPE(bandsight::gui::HelloInfo)
Q_DECLARE_METATYPE(bandsight::gui::SampleFrame)
Q_DECLARE_METATYPE(bandsight::gui::AppTotal)
Q_DECLARE_METATYPE(QVector<bandsight::gui::AppTotal>)
