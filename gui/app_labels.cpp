// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_labels.h"

#include <QHash>
#include <QSet>

namespace bandsight::gui {

namespace {

// Past this the label is longer than anything a column can show, and the tooltip
// is the honest place for the rest.
constexpr int kMaxComponents = 3;

QStringList dir_components(const QString& exe) {
    QStringList parts = exe.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!parts.isEmpty()) parts.removeLast();   // drop the binary itself
    return parts;
}

QString tail(const QStringList& parts, int n) {
    const int take = qMin(n, int(parts.size()));
    return QStringList(parts.mid(parts.size() - take)).join(QLatin1Char('/'));
}

}  // namespace

QStringList disambiguated_labels(const QVector<AppTotal>& apps) {
    QStringList out;
    out.reserve(apps.size());
    for (const AppTotal& a : apps) out << a.display_name();

    // Which rows share a name. Only those are touched; every other label is
    // exactly what it was.
    QHash<QString, QVector<int>> by_name;
    for (int i = 0; i < out.size(); ++i) by_name[out[i]].push_back(i);

    for (auto it = by_name.begin(); it != by_name.end(); ++it) {
        const QVector<int>& rows = it.value();
        if (rows.size() < 2) continue;

        QVector<QStringList> dirs;
        for (int r : rows) dirs.push_back(dir_components(apps[r].exe));

        // The shortest tail that separates them all. Two nvm installs differ
        // only near the root, so this can run out - hence the cap and the
        // fallback below.
        int depth = 1;
        for (; depth <= kMaxComponents; ++depth) {
            QSet<QString> seen;
            bool unique = true;
            for (const QStringList& d : dirs) {
                const QString t = tail(d, depth);
                if (seen.contains(t)) { unique = false; break; }
                seen.insert(t);
            }
            if (unique) break;
        }

        // The cap can leave rows still identical - two nvm installs share
        // "node/v24.18.0/bin" and differ only near the root, which is exactly
        // what the cap refuses to spell out. Found against the live database,
        // where both rendered as "node (node/v24.18.0/bin)".
        //
        // Those fall back to the whole directory, which ALWAYS separates them:
        // rows collide only when they share a name, and a shared name in a
        // shared directory would mean the same executable - which cannot happen,
        // since exe is the identity of the row. Long, but the column elides and
        // the tooltip carries the full path anyway. An earlier attempt spliced
        // in the first differing component instead; it produced "usr/.../usr/bin"
        // when that component was already in the tail, and still left duplicates.
        const bool use_full_dir = depth > kMaxComponents;

        for (int i = 0; i < rows.size(); ++i) {
            const QString suffix = use_full_dir
                                       ? dirs[i].join(QLatin1Char('/'))
                                       : tail(dirs[i], depth);
            // A row whose executable is unknown has no directory to show; it
            // keeps the bare name rather than gaining an empty bracket.
            if (suffix.isEmpty()) continue;
            out[rows[i]] = QStringLiteral("%1 (%2)").arg(out[rows[i]], suffix);
        }
    }
    return out;
}

}  // namespace bandsight::gui
