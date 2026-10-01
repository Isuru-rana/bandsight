// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/iface_model.h"

#include <QApplication>
#include <QIcon>
#include <QStyle>

#include "units.h"

namespace bandsight::gui {

namespace {

// Standard freedesktop names, all present in Breeze and Adwaita. Unlike
// application icons - which were measured and abandoned, see identity_chip.h -
// interface kinds map onto a fixed, small vocabulary that icon themes actually
// ship, so this one is worth doing.
//
// Every branch has a fallback and the last is a generic that no theme omits:
// B12's lesson is that naming an icon no installed theme has renders nothing at
// all, silently.
QIcon icon_for_kind(const QString& kind) {
    if (kind == QLatin1String("wifi"))
        return QIcon::fromTheme(QStringLiteral("network-wireless"),
                                QIcon::fromTheme(QStringLiteral("network-wired")));
    if (kind == QLatin1String("physical"))
        return QIcon::fromTheme(QStringLiteral("network-wired"));
    if (kind == QLatin1String("vpn"))
        return QIcon::fromTheme(QStringLiteral("network-vpn"),
                                QIcon::fromTheme(QStringLiteral("network-workgroup")));
    if (kind == QLatin1String("loopback"))
        return QIcon::fromTheme(QStringLiteral("computer"));
    return QIcon::fromTheme(QStringLiteral("network-workgroup"),
                            QIcon::fromTheme(QStringLiteral("application-x-executable")));
}

// fromTheme returns a null icon wherever no icon theme is configured - a bare
// session, a container, the offscreen platform the tests run under - and a null
// icon draws nothing at all, which is B12 again. QStyle always has something,
// so the chain ends somewhere real. The tray settled on the same fallback for
// the same reason.
QIcon with_fallback(QIcon icon) {
    if (!icon.isNull()) return icon;
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        return app->style()->standardIcon(QStyle::SP_DriveNetIcon);
    return icon;
}

const QString kNotMeasured = QStringLiteral("not measured");
const QString kWhyExcluded = QStringLiteral(
    "Tunnel and loopback bytes are excluded because they are also counted "
    "on the physical interface underneath.");
}  // namespace

void IfaceModel::setTotals(const QVector<IfaceTotal>& totals) {
    beginResetModel();
    rows_ = totals;          // checked_ is deliberately untouched
    endResetModel();
}

void IfaceModel::setWindow(Window w) {
    if (w == window_) return;   // an unchanged window repaints nothing
    window_ = w;
    emit headerDataChanged(Qt::Horizontal, Rx, Tx);
}

QSet<int> IfaceModel::visibleIfindexes() const {
    QSet<int> out;
    for (int r = 0; r < rows_.size(); ++r)
        if (measured(r) && checked_.value(rows_[r].id, true)) out.insert(rows_[r].id);
    return out;
}

int IfaceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(rows_.size());
}

int IfaceModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(ColCount);
}

QVariant IfaceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) return {};
    const IfaceTotal& t = rows_[index.row()];
    const bool m = measured(index.row());
    const bool is_bytes = index.column() == Rx || index.column() == Tx;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Name: return t.name;
            case Kind: return t.kind;
            case Rx:   return m ? format_volume(t.rx) : kNotMeasured;
            case Tx:   return m ? format_volume(t.tx) : kNotMeasured;
            default:   return {};
        }
    }
    // The icon rides on the Kind column, which keeps its text as the accessible
    // label: an icon alone would be identity by picture only, and the kinds
    // (physical vs virtual vs vpn) are not something a glyph reliably conveys.
    if (role == Qt::DecorationRole && index.column() == Kind)
        return with_fallback(icon_for_kind(t.kind));
    if (role == SortRole) {
        switch (index.column()) {
            case Name: return t.name;
            case Kind: return t.kind;
            case Rx:   return QVariant::fromValue(t.rx);
            case Tx:   return QVariant::fromValue(t.tx);
            default:   return {};
        }
    }
    if (role == MeasuredRole) return m;
    if (role == Qt::ToolTipRole && is_bytes && !m) return kWhyExcluded;
    if (role == Qt::CheckStateRole && index.column() == Name && m)
        return checked_.value(t.id, true) ? Qt::Checked : Qt::Unchecked;
    return {};
}

bool IfaceModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::CheckStateRole || !index.isValid() || index.column() != Name ||
        index.row() >= rows_.size() || !measured(index.row()))
        return false;
    checked_[rows_[index.row()].id] = value.toInt() == Qt::Checked;
    emit dataChanged(index, index, {Qt::CheckStateRole});
    emit visibleChanged(visibleIfindexes());
    return true;
}

Qt::ItemFlags IfaceModel::flags(const QModelIndex& index) const {
    if (!index.isValid() || index.row() >= rows_.size()) return Qt::NoItemFlags;
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == Name && measured(index.row())) f |= Qt::ItemIsUserCheckable;
    return f;
}

QVariant IfaceModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case Name: return QStringLiteral("Interface");
        case Kind: return QStringLiteral("Kind");
        // Spec §2.1 shows a total, and refreshFromHistory queries it over the
        // graph's current window - so "Downloaded" on its own reads 65 MB or
        // 7 GB for the same interface depending on a combo box on another tab.
        // Naming the period is what makes that coupling visible.
        case Rx:   return tr("Downloaded (%1)").arg(window_label(window_));
        case Tx:   return tr("Uploaded (%1)").arg(window_label(window_));
        default:   return {};
    }
}

}  // namespace bandsight::gui
