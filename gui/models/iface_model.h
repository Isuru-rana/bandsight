// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractTableModel>
#include <QHash>
#include <QSet>
#include <QVector>

#include "frame_types.h"

namespace bandsight::gui {

// One row per interface for the Interfaces tab (spec §2.1/§2.2). The checkboxes
// are a display-only filter over the graph: toggling one emits visibleChanged
// and changes nothing else - no requery, no row removal, no daemon traffic.
class IfaceModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { Name, Kind, Rx, Tx, ColCount };

    // The byte columns render "1.2 GB", which sorts as text with the gigabyte
    // below the megabyte, so the view's proxy sorts on these instead. Served by
    // the model rather than copied beside it: a second copy is refreshed by one
    // caller and goes stale for every other, which is exactly what B4 was.
    static constexpr int SortRole = Qt::UserRole + 1;
    static constexpr int MeasuredRole = Qt::UserRole + 2;

    explicit IfaceModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void setTotals(const QVector<IfaceTotal>& totals);   // full reset
    QSet<int> visibleIfindexes() const;                  // checked AND measured rows

    // The byte columns hold a total over one window, not a lifetime figure, and
    // which window is the graph tab's business - so the model is told, and says
    // so in the header. Repaints the two byte headers; touches no row.
    void setWindow(Window w);
    Window window() const { return window_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation o,
                        int role = Qt::DisplayRole) const override;

signals:
    void visibleChanged(QSet<int>);   // emitted on any checkbox toggle

private:
    // Whether this row's bytes are real. Derived from the kind string every
    // time, never from the byte counts and never from the incoming struct's
    // own flag - an interface that moved zero bytes is measured and must read
    // "0 B", and a busy tunnel is still unmeasured.
    bool measured(int row) const { return kind_is_measured(rows_[row].kind); }

    QVector<IfaceTotal> rows_;
    Window window_ = Window::FiveMin;   // GraphTab's own default
    // Keyed by ifindex, not row, so a refresh that reorders or grows the list
    // keeps every checkbox where the user left it. Absent means checked.
    QHash<int, bool> checked_;
};

}  // namespace bandsight::gui
