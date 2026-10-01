// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QSet>
#include <QVector>
#include <QWidget>

namespace bandsight::gui {

// One row per entity: name, an inline proportion bar, and the value. The bar is
// scaled to the largest row, so the column reads as a ranking at a glance
// without any axis.
//
// Built for the Apps column and deliberately generic, because Hosts and Traffic
// type are the same shape once the daemon collects them.
class RankedList : public QWidget {
    Q_OBJECT
public:
    struct Row {
        QString label;
        QString tooltip;
        quint64 value = 0;   // the ranking key: down + up
        quint64 down = 0;
        quint64 up = 0;
        QString key;         // stable identity for exclusion: the executable
    };

    explicit RankedList(QWidget* parent = nullptr);

    // Rows are drawn in the order given: ranking is the caller's decision, and
    // the query already sorts.
    void setRows(const QVector<Row>& rows);
    const QVector<Row>& rows() const { return rows_; }

    // Test-only: the drawn strings, so a test can assert what a reader sees
    // rather than what the model holds.
    QStringList visibleLabels() const;
    quint64 maxValue() const;

    // Rows the user has clicked out of the total. Excluded rows stay VISIBLE and
    // stay in the ranking - they are filtered, not hidden, so they can always be
    // clicked back. Keyed by executable so the set survives a refresh that
    // reorders or renames rows.
    void setExcluded(const QSet<QString>& keys);
    const QSet<QString>& excluded() const { return excluded_; }
    bool isExcluded(int row) const;

    // Bars stay scaled to the largest row overall, excluded or not: the column
    // answers "how do these applications compare", which does not change because
    // one of them was filtered out of a total elsewhere.
    int rowAt(const QPoint& pos) const;

signals:
    void rowClicked(const QString& key);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    QSize sizeHint() const override;

private:
    int rowHeight() const;
    int chipSize() const;

    QVector<Row> rows_;
    QSet<QString> excluded_;
};

}  // namespace bandsight::gui
