// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtGlobal>

#include "core/cycle.h"

namespace bandsight::gui {

// What the QuotaBar draws. cap == 0 means no cap is configured, in which case
// the bar shows usage without a target rather than a full or empty gauge.
struct QuotaInfo {
    quint64 used = 0;
    quint64 cap = 0;
    quint64 projected = 0;
    qint64 cycle_start = 0;
    qint64 cycle_end = 0;
    int cycle_day = 1;
};

// The cycle arithmetic lives in core/ (core/cycle.h) so the command-line
// client reports the same cycle and projection.
using core::Cycle;
using core::cycle_for;
using core::cycle_elapsed_fraction;
using core::project_to_cycle_end;

}  // namespace bandsight::gui
