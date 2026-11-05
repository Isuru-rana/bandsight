// SPDX-License-Identifier: GPL-3.0-or-later
#include "about.h"

#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <sys/utsname.h>

#include "appearance.h"
#include "build_info.h"
#include "git_commit.h"
#include "theme.h"

namespace bandsight::gui {

namespace {
const QString kRepoUrl = QStringLiteral("https://github.com/Isuru-rana/bandsight");
}  // namespace

QString app_version() { return QStringLiteral(BANDSIGHT_VERSION); }

QString about_title() { return QStringLiteral("Bandsight %1").arg(app_version()); }

QString git_commit() { return QStringLiteral(BANDSIGHT_GIT_COMMIT); }

QString build_type() {
    const QString t = QStringLiteral(BANDSIGHT_BUILD_TYPE);
    return t.isEmpty() ? QStringLiteral("unspecified") : t;
}

QString kernel_text() {
    struct utsname u {};
    if (::uname(&u) != 0) return QStringLiteral("unknown");
    return QStringLiteral("%1 %2 %3")
        .arg(QString::fromLocal8Bit(u.sysname), QString::fromLocal8Bit(u.release),
             QString::fromLocal8Bit(u.machine));
}

QString daemon_line(const DaemonInfo& d) {
    if (!d.connected) return QStringLiteral("bandsightd: not connected");
    if (d.version.isEmpty()) return QStringLiteral("bandsightd: unknown");
    return QStringLiteral("bandsightd %1").arg(d.version);
}

QString build_details(const DaemonInfo& d) {
    QStringList lines;
    QString app = about_title();
    QStringList extra;
    if (!git_commit().isEmpty()) extra << QStringLiteral("commit %1").arg(git_commit());
    extra << build_type();
    app += QStringLiteral(" (%1)").arg(extra.join(QStringLiteral(", ")));
    lines << app;
    lines << daemon_line(d);
    lines << QStringLiteral("Qt %1 (built with %2), %3")
                 .arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR),
                      QGuiApplication::platformName());
    lines << QStringLiteral("Kernel: %1").arg(kernel_text());
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (!desktop.isEmpty()) lines << QStringLiteral("Desktop: %1").arg(desktop);
    lines << QStringLiteral("Theme: %1").arg(theme_key(current_theme()));
    if (!d.db_path.isEmpty()) lines << QStringLiteral("Database: %1").arg(d.db_path);
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

AboutPage::AboutPage(const DaemonInfo& d, QWidget* parent)
    : QWidget(parent),
      daemon_(d),
      title_(new QLabel(about_title(), this)),
      daemon_label_(new QLabel(this)),
      db_label_(new QLabel(this)),
      link_(new QLabel(this)) {
    setObjectName(QStringLiteral("aboutPage"));

    auto* logo = new QLabel(this);
    const QIcon icon = QIcon::fromTheme(QStringLiteral("bandsight"),
                                        QIcon(QStringLiteral(":/icons/bandsight.png")));
    logo->setPixmap(icon.pixmap(QSize(64, 64)));
    logo->setFixedSize(64, 64);

    title_->setObjectName(QStringLiteral("aboutTitle"));
    QFont tf = title_->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.4);
    title_->setFont(tf);
    title_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* blurb = new QLabel(tr("Per-application network usage monitor for Linux."), this);
    blurb->setWordWrap(true);

    auto* head_text = new QVBoxLayout;
    head_text->setSpacing(2);
    head_text->addWidget(title_);
    head_text->addWidget(blurb);
    head_text->addStretch();
    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    head->addWidget(logo, 0, Qt::AlignTop);
    head->addLayout(head_text, 1);

    link_->setObjectName(QStringLiteral("aboutLink"));
    link_->setOpenExternalLinks(true);
    link_->setTextInteractionFlags(Qt::TextBrowserInteraction);

    daemon_label_->setObjectName(QStringLiteral("aboutDaemon"));
    daemon_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    db_label_->setObjectName(QStringLiteral("aboutDatabase"));
    db_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    db_label_->setWordWrap(true);

    auto value = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return l;
    };
    auto* form = new QFormLayout;
    form->addRow(tr("Licence"), value(QStringLiteral("GPL-3.0-or-later")));
    form->addRow(tr("Source"), link_);
    form->addRow(tr("Daemon"), daemon_label_);
    form->addRow(tr("Database"), db_label_);
    form->addRow(tr("Qt"), value(QString::fromLatin1(qVersion())));
    form->addRow(tr("Build"), value(build_type()));
    form->addRow(tr("Commit"),
                 value(git_commit().isEmpty() ? tr("not available") : git_commit()));
    for (int r = 0; r < form->rowCount(); ++r)
        if (auto* item = form->itemAt(r, QFormLayout::LabelRole))
            if (auto* l = qobject_cast<QLabel*>(item->widget())) muted_ << l;

    auto* copy = new QPushButton(tr("Copy details"), this);
    copy->setObjectName(QStringLiteral("copyDetails"));
    copy->setToolTip(tr("Versions, Qt and kernel, for a bug report"));
    connect(copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(build_details(daemon_));
    });
    auto* copy_row = new QHBoxLayout;
    copy_row->addStretch();
    copy_row->addWidget(copy);

    auto* box = new QVBoxLayout(this);
    box->setSpacing(14);
    box->addLayout(head);
    box->addLayout(form);
    box->addStretch();
    box->addLayout(copy_row);

    setDaemonInfo(d);
    applyInk();
    connect(&Appearance::instance(), &Appearance::changed, this, &AboutPage::applyInk);
}

void AboutPage::setDaemonInfo(const DaemonInfo& d) {
    daemon_ = d;
    daemon_label_->setText(daemon_line(d));
    db_label_->setText(d.db_path.isEmpty() ? tr("not reported") : d.db_path);
}

// Link and field names in the theme's own checked colours: a named theme's
// palette already carries them, but under System the desktop's link blue may
// not be one the card was checked against.
void AboutPage::applyInk() {
    const Theme t = theme();
    link_->setText(QStringLiteral("<a href=\"%1\" style=\"color:%2;\">%1</a>")
                       .arg(kRepoUrl, t.link.name()));
    for (QLabel* l : muted_)
        l->setStyleSheet(QStringLiteral("color:%1;").arg(t.ink_muted.name()));
}

QString AboutPage::titleText() const { return title_->text(); }
QString AboutPage::daemonText() const { return daemon_label_->text(); }
QString AboutPage::dbText() const { return db_label_->text(); }

}  // namespace bandsight::gui
