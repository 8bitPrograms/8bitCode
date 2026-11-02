#include "UpdateClient.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QVersionNumber>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
QString systemName() {
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}
bool trustedDownload(const QUrl &url) {
    static const QStringList hosts{QStringLiteral("github.com"), QStringLiteral("api.github.com"),
        QStringLiteral("release-assets.githubusercontent.com"), QStringLiteral("objects.githubusercontent.com")};
    return url.scheme() == QStringLiteral("https") && hosts.contains(url.host())
        && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443);
}
QString normalizedVersion(QString value) {
    if (value.startsWith(QLatin1Char('v'))) value.remove(0, 1);
    static const QRegularExpression stable(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
    if (!stable.match(value).hasMatch()) throw std::runtime_error("A stable three-part version is required.");
    return value;
}
}
UpdateClient::UpdateClient(QString app, QString repository, QString version, QString settingsFile,
    QObject *parent, QNetworkAccessManager *transport, QString cacheRoot)
    : QObject(parent), m_app(std::move(app)), m_repository(std::move(repository)), m_current(std::move(version)),
      m_status(tr("Updates are optional.")), m_settings(std::move(settingsFile), QSettings::IniFormat),
      m_network(transport ? transport : new QNetworkAccessManager(this)) {
    m_canInstall = ownsInstalledDirectory();
    m_cacheRoot = cacheRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/updates") : cacheRoot;
    connect(qApp, &QCoreApplication::aboutToQuit, this, &UpdateClient::applyOnExit);
}
UpdateClient::~UpdateClient() { if (m_reply) { m_reply->disconnect(this); m_reply->abort(); } }
QString UpdateClient::architecture() {
    const QString cpu = QSysInfo::buildCpuArchitecture().toLower();
    if (cpu == QStringLiteral("x86_64")) return QStringLiteral("x64");
    if (cpu == QStringLiteral("arm64") || cpu == QStringLiteral("aarch64")) return QStringLiteral("arm64");
    if (cpu == QStringLiteral("i386") || cpu == QStringLiteral("x86")) return QStringLiteral("x86");
    if (cpu.startsWith(QStringLiteral("arm"))) return QStringLiteral("armv7");
    return cpu;
}
bool UpdateClient::ownsInstalledDirectory() const {
#ifdef Q_OS_WIN
    // The first Code upgrade retains Pad's installed directory and upgrade ID.
    const QString directory = m_app == QStringLiteral("8bitCode") ? QStringLiteral("8bitPad") : m_app;
    const QString base = QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Programs/") + directory);
    const QString actual = QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath();
    return !actual.isEmpty() && (actual.compare(QDir(base).filePath(QStringLiteral("bin")), Qt::CaseInsensitive) == 0
        || actual.compare(QDir::cleanPath(base), Qt::CaseInsensitive) == 0);
#else
    return false;
#endif
}
bool UpdateClient::autoCheck() const { return m_settings.value(QStringLiteral("AutoCheck"), true).toBool(); }
bool UpdateClient::autoUpdate() const { return m_canInstall && m_settings.value(QStringLiteral("AutoUpdate"), false).toBool(); }
void UpdateClient::setAutoCheck(bool enabled) { m_settings.setValue(QStringLiteral("AutoCheck"), enabled); emit changed(); }
void UpdateClient::setAutoUpdate(bool enabled) {
    m_settings.setValue(QStringLiteral("AutoUpdate"), m_canInstall && enabled); emit changed();
    if (autoUpdate() && m_available && canDownload() && !ready() && !m_downloading) download();
}
void UpdateClient::start() {
    QTimer::singleShot(5000, this, [this] {
        const qint64 last = m_settings.value(QStringLiteral("LastCheck"), 0).toLongLong();
        if (autoCheck() && QDateTime::currentSecsSinceEpoch() - last >= 86400) check(false);
    });
}
QNetworkRequest UpdateClient::request(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", (m_app + QLatin1Char('/') + m_current).toUtf8());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(30000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    return req;
}
void UpdateClient::fail(const QString &message) {
    m_checking = false; m_downloading = false; m_status = message; m_file.reset(); emit changed();
}
void UpdateClient::fetch(const QUrl &url, qsizetype limit, std::function<void(const QByteArray &)> done, bool noReleaseOk) {
    if (!trustedDownload(url)) { fail(tr("The update URL is not trusted.")); return; }
    auto *reply = m_network->get(request(url)); m_reply = reply;
    auto data = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl &next) {
        if (trustedDownload(next)) reply->redirectAllowed(); else reply->abort();
    });
    connect(reply, &QIODevice::readyRead, this, [reply, data, limit] {
        data->append(reply->read(qMax<qsizetype>(0, limit - data->size() + 1)));
        if (data->size() > limit) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, limit, done, noReleaseOk] {
        if (m_reply == reply) m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (noReleaseOk && status == 404) {
            m_checking = false; m_available = false; m_status = tr("No release has been published yet.");
            m_settings.setValue(QStringLiteral("LastCheck"), QDateTime::currentSecsSinceEpoch());
            reply->deleteLater(); emit changed(); return;
        }
        data->append(reply->read(qMax<qsizetype>(0, limit - data->size() + 1)));
        if (data->size() > limit || reply->error() != QNetworkReply::NoError || status != 200) {
            const QString error = reply->errorString(); reply->deleteLater();
            fail(tr("Could not retrieve the update: %1").arg(error)); return;
        }
        reply->deleteLater();
        try { done(*data); } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    });
}
UpdateClient::Release UpdateClient::parseRelease(const QByteArray &data, const QString &app, const QString &repo,
    const QString &current, const QString &system, const QString &cpu, bool installed) {
    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) throw std::runtime_error("The release response is not valid JSON.");
    const auto json = document.object();
    if (json.value(QStringLiteral("draft")).toBool() || json.value(QStringLiteral("prerelease")).toBool())
        throw std::runtime_error("Only published stable releases are accepted.");
    Release release;
    release.tag = json.value(QStringLiteral("tag_name")).toString();
    release.version = normalizedVersion(release.tag);
    release.newer = QVersionNumber::compare(QVersionNumber::fromString(release.version),
        QVersionNumber::fromString(normalizedVersion(current))) > 0;
    release.notes = json.value(QStringLiteral("body")).toString().left(16000);
    const QString suffix = system == QStringLiteral("windows") ? (installed ? QStringLiteral(".msi") : QStringLiteral(".zip"))
        : system == QStringLiteral("macos") ? QStringLiteral(".dmg") : QStringLiteral(".tar.gz");
    release.assetName = app + QLatin1Char('-') + release.version + QLatin1Char('-') + system + QLatin1Char('-') + cpu + suffix;
    const QString prefix = QStringLiteral("https://github.com/") + repo + QStringLiteral("/releases/download/")
        + QString::fromLatin1(QUrl::toPercentEncoding(release.tag)) + QLatin1Char('/');
    QSet<QString> seen;
    for (const auto &entry : json.value(QStringLiteral("assets")).toArray()) {
        const auto asset = entry.toObject(); const QString name = asset.value(QStringLiteral("name")).toString();
        if (name != release.assetName && name != QStringLiteral("SHA256SUMS.txt")) continue;
        if (seen.contains(name)) throw std::runtime_error("Duplicate release assets were rejected.");
        seen.insert(name);
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!trustedDownload(url) || url.toString(QUrl::FullyEncoded) != prefix + name)
            throw std::runtime_error("The update asset does not belong to the configured repository and tag.");
        if (name == release.assetName) {
            release.size = asset.value(QStringLiteral("size")).toVariant().toLongLong();
            if (release.size <= 0 || release.size > 1024LL * 1024 * 1024)
                throw std::runtime_error("The update package has an invalid size.");
            release.assetUrl = url;
        } else release.checksumUrl = url;
    }
    return release;
}
QString UpdateClient::checksumFor(const QByteArray &data, const QString &name) {
    QString result; const QRegularExpression line(QStringLiteral("^([A-Fa-f0-9]{64}) [ *](.+)$"));
    for (const auto &value : QString::fromUtf8(data).split(QLatin1Char('\n'))) {
        const auto match = line.match(value.trimmed());
        if (match.hasMatch() && match.captured(2) == name) {
            if (!result.isEmpty()) throw std::runtime_error("The checksum list contains a duplicate entry.");
            result = match.captured(1).toLower();
        }
    }
    if (result.isEmpty()) throw std::runtime_error("The package checksum is missing.");
    return result;
}
void UpdateClient::check(bool manual) {
    if (m_reply || m_downloading || ready()) return;
    m_checking = true; m_status = tr("Checking for updates..."); emit changed();
    fetch(QUrl(QStringLiteral("https://api.github.com/repos/") + m_repository + QStringLiteral("/releases/latest")),
        1024 * 1024, [this, manual](const QByteArray &data) {
            m_release = parseRelease(data, m_app, m_repository, m_current, systemName(), architecture(), m_canInstall);
            m_settings.setValue(QStringLiteral("LastCheck"), QDateTime::currentSecsSinceEpoch());
            m_checking = false;
            m_available = m_release.newer && (manual || m_settings.value(QStringLiteral("SkippedVersion")).toString() != m_release.version);
            m_status = m_available ? tr("Version %1 is available.").arg(m_release.version) : tr("You are up to date.");
            emit changed();
            if (m_available) { emit updateFound(); if (autoUpdate() && canDownload()) download(); }
        }, true);
}
void UpdateClient::download() {
    if (m_reply || m_downloading || !m_available || !canDownload() || ready()) return;
    m_downloading = true; m_status = tr("Downloading update..."); m_progress = 0; emit changed();
    fetch(m_release.checksumUrl, 4 * 1024 * 1024, [this](const QByteArray &data) {
        m_expectedHash = checksumFor(data, m_release.assetName); downloadPayload();
    });
}
void UpdateClient::downloadPayload() {
    const QString cache = m_cacheRoot;
    if (!QDir().mkpath(cache)) { fail(tr("Could not create the update download folder.")); return; }
    m_staging = std::make_unique<QTemporaryDir>(cache + QStringLiteral("/download-XXXXXX"));
    if (!m_staging->isValid()) { fail(tr("Could not create a private update folder.")); return; }
    const QString path = m_staging->filePath(m_release.assetName); m_file = std::make_unique<QSaveFile>(path);
    if (!m_file->open(QIODevice::WriteOnly)) { fail(tr("Could not save the update package.")); return; }
    auto *reply = m_network->get(request(m_release.assetUrl)); m_reply = reply;
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto count = std::make_shared<qint64>(0);
    auto drain = [this, reply, hash, count] {
        while (reply->bytesAvailable() && m_file) {
            const QByteArray bytes = reply->read(64 * 1024); *count += bytes.size();
            if (*count > m_release.size || m_file->write(bytes) != bytes.size()) { reply->abort(); return; }
            hash->addData(bytes);
        }
        m_progress = m_release.size > 0 ? double(*count) / double(m_release.size) : 0; emit changed();
    };
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl &next) {
        if (trustedDownload(next)) reply->redirectAllowed(); else reply->abort();
    });
    connect(reply, &QIODevice::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [this, reply, hash, count, path, drain] {
        drain(); if (m_reply == reply) m_reply = nullptr;
        const bool verified = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && *count == m_release.size && QString::fromLatin1(hash->result().toHex()) == m_expectedHash;
        reply->deleteLater();
        if (!verified || !m_file || !m_file->commit()) { fail(tr("The update failed verification. Nothing was installed.")); return; }
        m_file.reset(); m_staging->setAutoRemove(false); m_readyPath = path; m_downloading = false; m_progress = 1;
        m_status = m_canInstall ? (autoUpdate() ? tr("Update ready. It will install when you close the app.") : tr("Update ready to install."))
            : tr("Download verified. Open the download folder to install or extract it.");
        emit changed();
    });
}
void UpdateClient::install() { if (ready() && m_canInstall) { m_manualInstall = true; emit closeRequested(); } }
void UpdateClient::cancelInstall() { m_manualInstall = false; m_status = tr("Update ready. Installation postponed."); emit changed(); }
void UpdateClient::skip() {
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    m_file.reset(); m_checking = false; m_downloading = false; m_settings.setValue(QStringLiteral("SkippedVersion"), m_release.version);
    if (m_staging) { m_staging->setAutoRemove(true); m_staging.reset(); }
    m_available = false; m_readyPath.clear(); m_manualInstall = false;
    m_status = tr("Skipped version %1.").arg(m_release.version); emit changed();
}
void UpdateClient::openReleasePage() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/") + m_repository + QStringLiteral("/releases"))); }
void UpdateClient::showDownload() { if (ready()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_readyPath).absolutePath())); }
void UpdateClient::applyOnExit() {
#ifdef Q_OS_WIN
    if (!ready() || !m_canInstall || (!autoUpdate() && !m_manualInstall)) return;
    const QString helper = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Apply-Update.ps1"));
    if (!QFileInfo::exists(helper)) return;
    const QString powershell = QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), helper, QStringLiteral("-Package"), m_readyPath, QStringLiteral("-Sha256"), m_expectedHash,
        QStringLiteral("-ParentPid"), QString::number(QCoreApplication::applicationPid()), QStringLiteral("-Application"), m_app,
        QStringLiteral("-Executable"), QCoreApplication::applicationFilePath()};
    if (m_manualInstall) args << QStringLiteral("-Restart");
    QProcess process;
    process.setProgram(powershell); process.setArguments(args);
    process.setWorkingDirectory(QFileInfo(m_readyPath).absolutePath());
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_HIDE;
    });
    process.startDetached();
#endif
}
