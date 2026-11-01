#include "PadUpdates.h"
#include "UpdateClient.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>

void setupPadUpdates(QMainWindow *window, QMenu *help, const QString &settingsFile, bool startupCheck)
{
    auto *client = new UpdateClient(QStringLiteral("8bitCode"), QStringLiteral("8bitPrograms/8bitPad"),
        QCoreApplication::applicationVersion(), settingsFile, qApp);
    client->setObjectName(QStringLiteral("appUpdater"));
    auto *action = help->addAction(QObject::tr("Updates..."));
    action->setObjectName(QStringLiteral("actionUpdates"));
    QObject::connect(client, &UpdateClient::closeRequested, window, [window, client] {
        if (!window->close()) client->cancelInstall();
    });
    QObject::connect(client, &UpdateClient::updateFound, window, [window, client] {
        window->statusBar()->showMessage(QObject::tr("8bitCode %1 is available. Open Help > Updates to review it.")
            .arg(client->latestVersion()), 15000);
    });
    QObject::connect(action, &QAction::triggered, window, [window, client] {
        auto *dialog = new QDialog(window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName(QStringLiteral("updatesDialog"));
        dialog->setWindowTitle(QObject::tr("8bitCode updates"));
        dialog->resize(560, 420);
        auto *layout = new QVBoxLayout(dialog);
        auto *version = new QLabel(QObject::tr("Current version: %1").arg(client->currentVersion()), dialog);
        layout->addWidget(version);
        auto *status = new QLabel(dialog); status->setTextFormat(Qt::PlainText); status->setWordWrap(true);
        layout->addWidget(status);
        auto *check = new QCheckBox(QObject::tr("Check automatically for new versions"), dialog);
        auto *automatic = new QCheckBox(QObject::tr("Install updates automatically when I close the app"), dialog);
        check->setObjectName(QStringLiteral("automaticUpdateChecks"));
        automatic->setObjectName(QStringLiteral("automaticUpdates"));
        automatic->setEnabled(client->canInstall());
        layout->addWidget(check); layout->addWidget(automatic);
        if (!client->canInstall()) {
            auto *hint = new QLabel(QObject::tr("This copy is updated manually or by your system package manager."), dialog);
            hint->setWordWrap(true); layout->addWidget(hint);
        }
        auto *notes = new QPlainTextEdit(dialog); notes->setReadOnly(true); layout->addWidget(notes, 1);
        auto *row = new QHBoxLayout;
        auto *checkNow = new QPushButton(QObject::tr("Check now"), dialog);
        auto *download = new QPushButton(QObject::tr("Download update"), dialog);
        auto *install = new QPushButton(QObject::tr("Install and restart"), dialog);
        auto *show = new QPushButton(QObject::tr("Show download"), dialog);
        auto *skip = new QPushButton(QObject::tr("Skip this version"), dialog);
        row->addWidget(checkNow); row->addWidget(download); row->addWidget(install); row->addWidget(show);
        layout->addLayout(row); layout->addWidget(skip);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        buttons->button(QDialogButtonBox::Close)->setText(QObject::tr("Later"));
        layout->addWidget(buttons);
        const auto refresh = [=] {
            status->setText(client->status()); notes->setPlainText(client->releaseNotes());
            const QSignalBlocker a(check), b(automatic);
            check->setChecked(client->autoCheck()); automatic->setChecked(client->autoUpdate());
            checkNow->setEnabled(!client->checking() && !client->downloading() && !client->ready());
            download->setEnabled(client->available() && client->canDownload() && !client->downloading() && !client->ready());
            install->setVisible(client->canInstall()); install->setEnabled(client->ready());
            show->setVisible(!client->canInstall()); show->setEnabled(client->ready());
            skip->setEnabled(client->available() || client->ready());
        };
        QObject::connect(client, &UpdateClient::changed, dialog, refresh);
        QObject::connect(check, &QCheckBox::toggled, client, &UpdateClient::setAutoCheck);
        QObject::connect(automatic, &QCheckBox::toggled, client, &UpdateClient::setAutoUpdate);
        QObject::connect(checkNow, &QPushButton::clicked, client, [client] { client->check(); });
        QObject::connect(download, &QPushButton::clicked, client, &UpdateClient::download);
        QObject::connect(install, &QPushButton::clicked, client, &UpdateClient::install);
        QObject::connect(show, &QPushButton::clicked, client, &UpdateClient::showDownload);
        QObject::connect(skip, &QPushButton::clicked, client, &UpdateClient::skip);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        refresh(); dialog->show();
    });
    if (startupCheck) client->start();
}
