// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <QVector>

#include "frame_types.h"

namespace bandsight::gui {

// Labels for a list of applications, disambiguated ONLY where they collide.
//
// Four different `node` binaries render as four rows called "node" (FOLLOWUP
// U4). The executable is in the tooltip, which is the right place for it, but it
// is invisible while scanning the column. Showing a path on every row instead
// would make the common case - one binary, one obvious name - unreadable to fix
// the rare one.
//
// A colliding label gains the shortest tail of its directory that tells it apart
// from the others it collides with: /usr/bin/node and /usr/local/bin/node need
// two components ("usr/bin", "local/bin") because both end in "bin".
QStringList disambiguated_labels(const QVector<AppTotal>& apps);

}  // namespace bandsight::gui
