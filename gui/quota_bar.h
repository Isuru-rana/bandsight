// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>

#include "quota.h"

class QLabel;
class QProgressBar;

namespace bandsight::gui {

// Spec §7: always visible above the tabs. Cap progress plus a linear projection
// to the end of the cycle. No notifications in v1 - visible state only.
class QuotaBar : public QWidget {
    Q_OBJECT
public:
    explicit QuotaBar(QWidget* parent = nullptr);

    // Test-only accessors, for the same reason MainWindow has them: the wiring
    // is the behaviour, and none of it is observable from outside otherwise.
    QString text() const;
    int percent() const;          // -1 when no cap is configured
    QuotaInfo info() const { return info_; }

public slots:
    void setQuota(const QuotaInfo& info);

private:
    QuotaInfo info_;
    QLabel* label_;
    QProgressBar* bar_;
};

}  // namespace bandsight::gui
