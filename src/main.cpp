/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include <QDebug>
#include <QSettings>
#include <QSysInfo>
#include <QApplication>
#include <QDataStream>
#include <QDir>

#include "NotepadNextApplication.h"

int main(int argc, char *argv[])
{
    qSetMessagePattern("[%{time process}] %{if-debug}D%{endif}%{if-info}I%{endif}%{if-warning}W%{endif}%{if-critical}C%{endif}%{if-fatal}F%{endif}: %{message}");

    // Set these since other parts of the app references these
    QApplication::setOrganizationName("8bitCode");
    QApplication::setApplicationName("8bitCode");
    QGuiApplication::setApplicationDisplayName("8bitCode");
    QGuiApplication::setApplicationVersion(APP_VERSION);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    // Default settings format
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Profile overrides are set before constructing SingleApplication or settings.
    for (int i=1; i+1<argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--profile") qputenv("BIT8PAD_PROFILE", QDir(QString::fromLocal8Bit(argv[++i])).absolutePath().toUtf8());
        else if (arg == "--smoke-test") {
            const QString output = QDir(QString::fromLocal8Bit(argv[++i])).absolutePath();
            qputenv("BIT8PAD_PROFILE", QDir(output).filePath("profile").toUtf8());
            QApplication::setApplicationName("8bitCode-Smoke");
        }
    }

    NotepadNextApplication app(argc, argv);

    // Log some debug info
    qInfo("=============================");
    for(const auto &d : app.debugInfo()){
        qInfo("%s", qUtf8Printable(d));
    }
    qInfo("=============================");


    if(app.isPrimary()) {
        app.init();

        return app.exec();
    }
    else {
        qInfo() << "Primary instance already running. PID:" << app.primaryPid();

        const bool delivered = app.sendInfoToPrimaryInstance();

        qInfo() << "Secondary instance closing...";

        app.exit(0);

        return delivered ? 0 : 1;
    }
}
