// SPDX-License-Identifier: GPL-3.0-or-later
#include "tabs/graph_tab.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "widgets/totals_card.h"
#include "widgets/traffic_chart.h"

namespace bandsight::gui {

GraphTab::GraphTab(QWidget* parent)
    : QWidget(parent),
      combo_(new QComboBox(this)),
      chart_(new TrafficChart(this)),
      totals_(new TotalsCard(this)) {
    for (Window w : all_windows()) combo_->addItem(window_label(w), int(w));
    // Five minutes is the window a monitor is normally watched on.
    combo_->setCurrentIndex(combo_->findData(int(Window::FiveMin)));

    auto* top = new QHBoxLayout;
    top->addStretch();
    top->addWidget(combo_);   // top right, as on the Usage screen

    auto* box = new QVBoxLayout(this);
    box->addLayout(top);
    box->addWidget(chart_, 1);
    box->addWidget(totals_);

    last_window_ = window();
    needs_seed_ = is_live(last_window_);
    connect(combo_, &QComboBox::currentIndexChanged, this, &GraphTab::refresh);
    refresh();
}

Window GraphTab::window() const {
    return static_cast<Window>(combo_->currentData().toInt());
}

const QVector<SeriesPoint>& GraphTab::plottedPoints() const { return chart_->points(); }

void GraphTab::onSample(const SampleFrame& f) {
    ring_.append(f, visible_);
    // The rate is this frame, on every window: a frame is one second, so its
    // bytes are bytes per second. Summed over the same interfaces as the chart.
    quint64 rx = 0, tx = 0;
    for (const SampleIface& i : f.ifaces)
        if (visible_.contains(i.id)) { rx += i.rx; tx += i.tx; }
    totals_->setRates(rx, tx);
    const Window w = window();
    if (is_live(w)) redraw(ring_.tail(window_spec(w).range_s));
}

void GraphTab::onSeries(Window w, QVector<SeriesPoint> points) {
    // A reply for a window the user has already switched away from would
    // redraw the chart with the wrong range under the wrong label.
    if (w != window()) return;
    if (is_live(w)) {
        // A live window's reply is its seed, not its contents: it fills in the
        // seconds before the GUI was listening and the ring carries on from there.
        ring_.seed(points);
        needs_seed_ = false;
        redraw(ring_.tail(window_spec(w).range_s));
        return;
    }
    redraw(points);
}

void GraphTab::onVisibleChanged(QSet<int> visible) {
    visible_ = std::move(visible);
    totals_->setIfaces(iface_totals_, visible_);
    refresh();
}

void GraphTab::onDisconnected() {
    ring_.clear();
    // A dead daemon is moving nothing we know of; a frozen last rate would lie.
    totals_->setRates(0, 0);
    // Not re-seeded on reconnect: history would backfill the outage with the
    // zero-filled grid, drawing a dead daemon as an idle network.
    if (is_live(window())) redraw({});
}

void GraphTab::setIfaceTotals(QVector<IfaceTotal> totals) {
    iface_totals_ = std::move(totals);
    totals_->setIfaces(iface_totals_, visible_);
}

void GraphTab::setWindow(Window w) {
    const int i = combo_->findData(int(w));
    if (i >= 0) combo_->setCurrentIndex(i);   // no-op if already there: no loop
}

void GraphTab::refresh() {
    const Window w = window();
    if (w != last_window_) {
        last_window_ = w;
        // Entering a live window asks for its seed once.
        needs_seed_ = is_live(w);
        emit windowChanged(w);
    }
    if (is_live(w)) {
        redraw(ring_.tail(window_spec(w).range_s));
        if (needs_seed_) emit seriesNeeded(w, visible_, QDateTime::currentSecsSinceEpoch());
    } else {
        emit seriesNeeded(w, visible_, QDateTime::currentSecsSinceEpoch());
    }
}

void GraphTab::redraw(const QVector<SeriesPoint>& pts) {
    // Every path into the chart - live ring, history reply, clear on disconnect -
    // goes through here, so the chart and the Usage timeline cannot disagree.
    chart_->setSeries(pts, window());
    // The card's totals are the plotted points summed - not a separate query -
    // so the numbers under the curve are exactly the area of the curve.
    quint64 rx = 0, tx = 0;
    for (const SeriesPoint& p : pts) { rx += p.rx; tx += p.tx; }
    totals_->setTotals(rx, tx);
    emit plotted(pts);
}

}  // namespace bandsight::gui
