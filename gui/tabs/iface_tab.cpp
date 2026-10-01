// SPDX-License-Identifier: GPL-3.0-or-later
#include "tabs/iface_tab.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

namespace bandsight::gui {

// Sorts the byte columns on the raw counts rather than on "1.2 GB" as text, and
// hides the interfaces that carry no counts at all.
//
// It keeps NO copy of the rows. It used to, refreshed only through
// IfaceTab::setTotals - but model() hands out a non-const IfaceModel whose
// setTotals is public, so a caller going straight to the model left the copy
// stale, and with an unchanged row count the bounds check still passed and rows
// were ordered by another interface's bytes: in range, silent, no crash
// (FOLLOWUP B4). Everything is read from the model through a role, so there is
// nothing left to desynchronise.
class ByteSortProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setShowUnmeasured(bool on) {
        if (on == show_unmeasured) return;
        beginFilterChange();
        show_unmeasured = on;
        endFilterChange(Direction::Rows);
    }
    bool showUnmeasured() const { return show_unmeasured; }

protected:
    bool measured(int source_row) const {
        return sourceModel()
            ->index(source_row, IfaceModel::Name)
            .data(IfaceModel::MeasuredRole)
            .toBool();
    }

    bool filterAcceptsRow(int source_row, const QModelIndex&) const override {
        // Unmeasured rows outnumber the real interfaces roughly twenty to one on
        // a machine running containers, and every one renders the same "not
        // measured" filler, so the default view drops them. Hidden, never
        // removed: the model still holds every interface (spec §2.2).
        return show_unmeasured || measured(source_row);
    }

    bool lessThan(const QModelIndex& l, const QModelIndex& r) const override {
        const int c = l.column();
        if (c != IfaceModel::Rx && c != IfaceModel::Tx)
            return QSortFilterProxyModel::lessThan(l, r);

        // An unmeasured row has no number at all (spec §2.2), so it must not be
        // ordered as if it were zero: group them at one end instead.
        const bool am = l.data(IfaceModel::MeasuredRole).toBool();
        const bool bm = r.data(IfaceModel::MeasuredRole).toBool();
        if (am != bm) return !am;
        return l.data(IfaceModel::SortRole).toULongLong() <
               r.data(IfaceModel::SortRole).toULongLong();
    }

private:
    bool show_unmeasured = false;
};

IfaceTab::IfaceTab(QWidget* parent)
    : QWidget(parent),
      model_(new IfaceModel(this)),
      proxy_(new ByteSortProxy(this)),
      view_(new QTableView(this)),
      show_unmeasured_(new QCheckBox(tr("Show unmeasured interfaces (0)"), this)) {
    proxy_->setSourceModel(model_);
    view_->setModel(proxy_);
    view_->setSortingEnabled(true);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->verticalHeader()->hide();
    // Naming the window made "Downloaded (5 minutes)" wider than its column, and
    // a default-width section clips it - from the left, so the header read
    // "oaded (5 mi". Sizing to contents measures the header text too.
    view_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    view_->horizontalHeader()->setStretchLastSection(true);
    // The interesting row is the one moving the most bytes, not the one whose
    // name sorts first - which on this machine put the only active interface
    // below sixty-eight veths and off the bottom of the window.
    view_->sortByColumn(IfaceModel::Rx, Qt::DescendingOrder);

    show_unmeasured_->setObjectName(QStringLiteral("showUnmeasured"));
    connect(show_unmeasured_, &QCheckBox::toggled,
            this, &IfaceTab::setShowUnmeasured);

    auto* box = new QVBoxLayout(this);
    box->addWidget(view_);
    box->addWidget(show_unmeasured_);
}

bool IfaceTab::showUnmeasured() const { return proxy_->showUnmeasured(); }

void IfaceTab::setShowUnmeasured(bool on) {
    proxy_->setShowUnmeasured(on);
    // The checkbox is also the caller on the signal path, and setChecked on an
    // already-correct box emits nothing, so this does not recurse.
    show_unmeasured_->setChecked(on);
}

void IfaceTab::setTotals(const QVector<IfaceTotal>& totals) {
    model_->setTotals(totals);

    int hidden = 0;
    for (const IfaceTotal& t : totals)
        if (!kind_is_measured(t.kind)) ++hidden;
    show_unmeasured_->setText(tr("Show unmeasured interfaces (%1)").arg(hidden));
    // Nothing to reveal is not a choice worth offering, but the label still
    // states the count so an empty list reads as an answer, not a missing one.
    show_unmeasured_->setEnabled(hidden > 0);
}

}  // namespace bandsight::gui
