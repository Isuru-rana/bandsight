// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QVector>
#include <QWidget>

#include "frame_types.h"
#include "models/iface_model.h"

class QCheckBox;
class QTableView;

namespace bandsight::gui {

// Defined in the .cpp: sorts the byte columns on the raw counts rather than on
// "1.2 GB" as text. Nothing outside needs its type.
class ByteSortProxy;

// Spec §4.6: the per-interface table. Owns its IfaceModel; MainWindow feeds it
// totals and listens to model()->visibleChanged. No logic of its own.
class IfaceTab : public QWidget {
    Q_OBJECT
public:
    explicit IfaceTab(QWidget* parent = nullptr);

    IfaceModel* model() const { return model_; }
    QTableView* view() const { return view_; }   // the proxy is view_->model()
    QCheckBox* unmeasuredToggle() const { return show_unmeasured_; }

    // Unmeasured rows are filtered out of the view, never out of the model: the
    // count in the toggle's label, and everything visibleIfindexes() decides,
    // still see every interface (spec §2.2).
    bool showUnmeasured() const;
    void setShowUnmeasured(bool on);

public slots:
    void setTotals(const QVector<IfaceTotal>& totals);

private:
    IfaceModel* model_;
    ByteSortProxy* proxy_;
    QTableView* view_;
    QCheckBox* show_unmeasured_;
};

}  // namespace bandsight::gui
