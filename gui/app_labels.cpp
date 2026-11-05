// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_labels.h"

#include "core/app_labels.h"

namespace bandsight::gui {

// The rule is core::disambiguated_labels, shared with the command-line client.
// The base label stays display_name(), which is translated.
QStringList disambiguated_labels(const QVector<AppTotal>& apps) {
    std::vector<core::LabelledExe> in;
    in.reserve(std::size_t(apps.size()));
    for (const AppTotal& a : apps) in.push_back({a.display_name().toStdString(), a.exe.toStdString()});
    QStringList out;
    out.reserve(apps.size());
    for (const std::string& l : core::disambiguated_labels(in)) out << QString::fromStdString(l);
    return out;
}

}  // namespace bandsight::gui
