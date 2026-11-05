// SPDX-License-Identifier: GPL-3.0-or-later
#include "service_names.h"

#include <QCoreApplication>

#include "core/service_names.h"

namespace bandsight::gui {

QString port_label(int proto, int port) {
    return QString::fromStdString(core::port_label(proto, port));
}

// The names are core's (shared with the command-line client), passed through the
// translator under the context they always had.
QString service_name(int proto, int port) {
    if (const char* n = core::known_service_name(proto, port))
        return QCoreApplication::translate("Services", n);
    return port_label(proto, port);
}

}  // namespace bandsight::gui
