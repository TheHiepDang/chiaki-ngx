// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "diagnostics.h"
#include "crash_dump_win.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QRegularExpression>
#include <QSysInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

namespace {
struct LogState {
    QMutex mutex;
    QFile file;
    QString path;
    QtMessageHandler previous = nullptr;
    bool initialized = false;
};
// Logging can outlive Qt's normal static destruction on worker threads.
LogState &state() { static auto *s = new LogState; return *s; }
void writeLine(LogState &s, const QByteArray &line) {
    if (!s.file.isOpen()) return;
    // Keep the current and preceding 4 MiB segments for a long-running process.
    if (s.file.size() + line.size() > 4 * 1024 * 1024) {
        s.file.close();
        QFile::remove(s.path + ".previous.log");
        QFile::rename(s.path, s.path + ".previous.log");
        s.file.setFileName(s.path);
        if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
        s.file.write(DiagnosticLog::reportInstructions().toUtf8() + '\n');
    }
    s.file.write(line);
    s.file.flush();
}
void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    DiagnosticLog::record(type, message);
    auto previous = state().previous;
    if (previous) previous(type, context, DiagnosticLog::taggedMessage(message));
    else {
        // Preserve the existing console behavior without bypassing redaction.
        const QByteArray text = DiagnosticLog::taggedMessage(message).toUtf8();
        std::fprintf(stderr, "%s\n", text.constData());
    }
}
}

QString DiagnosticLog::sanitize(const QString &message) {
    QString result = message;
    static const QRegularExpression bearer(R"(((?:Bearer|Basic)\s+)[A-Za-z0-9._~+/-]+=*)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression secrets(R"(((?:access[_-]?token|refresh[_-]?token|authorization|password|passcode|regist[_ -]?key|rp[_ -]?key|morning|duid|account(?:_id)?|session[_ -]?id)["]?\s*[:=]\s*)(?:"[^"]*"|[^\s,;]+))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression ip(R"(\b(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\b)");
    static const QRegularExpression ipv6(R"((?<![0-9A-Za-z])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?![0-9A-Za-z]))");
    static const QRegularExpression mac(R"(\b[0-9a-f]{2}(?::[0-9a-f]{2}){5}\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression url(R"((?:https?|wss?)://[^\s<>]+)", QRegularExpression::CaseInsensitiveOption);
    result.replace(bearer, "\\1<redacted>");
    result.replace(secrets, "\\1<redacted>");
    result.replace(ip, "<redacted-ip>");
    result.replace(ipv6, "<redacted-ip>");
    result.replace(mac, "<redacted-device>");
    result.replace(url, "<redacted-url>");
    return result;
}

QString DiagnosticLog::taggedMessage(const QString &message) {
    const QString safe = sanitize(message);
    static const QRegularExpression tags(R"(^((?:\[[A-Za-z][A-Za-z0-9_-]*\])+))");
    QString inherited = QStringLiteral("[App]");
    QStringList lines;
    for (QString line : safe.split('\n', Qt::SkipEmptyParts)) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        const auto match = tags.match(line);
        if (match.hasMatch()) inherited = match.captured(1);
        else line = inherited + ' ' + line;
        lines.append(line);
    }
    return lines.join('\n');
}

QString DiagnosticLog::filename() { return state().path; }
void DiagnosticLog::shutdown() {
    record(QtInfoMsg, "[App][Shutdown] Application exited normally");
    QMutexLocker lock(&state().mutex);
    state().file.close();
}
QString DiagnosticLog::reportInstructions() {
    return QStringLiteral(
        "Report a bug: https://github.com/TheHiepDang/chiaki-ngx/issues/new\n"
        "Attach the matching chiaki_ngx_*.log (and .previous.log if present), "
        "chiaki_session_*.log and any matching *-crash.txt.\n"
        "Open the folder with Settings > General > Log Directory > Open.\n"
        "For SM86 FG also attach matching loader_*.jsonl and backend_*.jsonl from "
        "runtimes/sm86-proxy/dlssg_sm86/logs/ inside the release folder.\n"
        "RTX messages use [RTX]; send complete logs, not only filtered lines.\n"
        "Include steps to reproduce, app version, GPU/driver, and RTX/Eye Candy settings.\n"
        "Review logs before sharing. Native *-crash.dmp files may contain private data; share privately.\n"
        "No files are uploaded automatically.\n");
}

void DiagnosticLog::record(QtMsgType type, const QString &message) {
    auto &s = state();
    if (type == QtDebugMsg && !qEnvironmentVariableIsSet("CHIAKI_DIAGNOSTICS_VERBOSE")) return;
    if (type == QtInfoMsg && message.startsWith("[libplacebo] Dithering to")) return;
    const char *level = type == QtDebugMsg ? "DEBUG" : type == QtInfoMsg ? "INFO"
        : type == QtWarningMsg ? "WARN" : type == QtCriticalMsg ? "ERROR" : "FATAL";
    const QString text = taggedMessage(message.left(16384));
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QMutexLocker lock(&s.mutex);
    for (const QString &part : text.split('\n', Qt::SkipEmptyParts))
        writeLine(s, (timestamp + " [" + level + "] " + part + '\n').toUtf8());
    if (type == QtFatalMsg) writeLine(s, reportInstructions().toUtf8());
}

bool DiagnosticLog::initialize(const QString &requestedDirectory) {
    auto &s = state();
    if (s.initialized) return s.file.isOpen();
    QString directory = requestedDirectory;
    if (directory.isEmpty()) directory = qEnvironmentVariable("CHIAKI_DIAGNOSTIC_LOG_DIR");
    if (directory.isEmpty()) directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/log";
    if (!QDir::isAbsolutePath(directory) || !QDir().mkpath(directory)) return false;
    QDir dir(directory);
    // Only our own diagnostic files are pruned; session logs have their existing retention.
    auto old = dir.entryInfoList({"chiaki_ngx_*.log"}, QDir::Files, QDir::Time);
    for (int n = 10; n < old.size(); ++n) QFile::remove(old[n].absoluteFilePath());
    const QString base = dir.filePath("chiaki_ngx_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")
        + '_' + QString::number(QCoreApplication::applicationPid()));
    s.path = base + ".log";
    s.file.setFileName(s.path);
    if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    s.initialized = true;
    qputenv("CHIAKI_DIAGNOSTIC_LOG_DIR", dir.absolutePath().toUtf8());
    s.file.write(reportInstructions().toUtf8());
    s.file.write(("Application log: " + s.path + '\n').toUtf8());
    s.file.flush();
#ifdef Q_OS_WIN
    ChiakiCrash::install(base.toStdWString(), reportInstructions().toUtf8().toStdString());
#endif
    s.previous = qInstallMessageHandler(handler);
    record(QtInfoMsg, QString("[App][Startup] chiaki-ngx version=%1 Qt=%2 OS=%3 arch=%4 pid=%5")
        .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture())
        .arg(QCoreApplication::applicationPid()));
    QFile build(QCoreApplication::applicationDirPath() + "/build-info.json");
    if (build.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(build.readAll()).object();
        for (const auto &name : {"update", "base_commit", "gui_sha256", "worker_sha256", "source_patch_sha256", "patch_sha256", "runtime_manifest_sha256", "build_type", "built_at", "packaged_at"})
            if (object.contains(name)) record(QtInfoMsg, "[App][Build] " + QString::fromLatin1(name) + '=' + object.value(name).toString());
    }
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] {
        shutdown();
    });
    return true;
}
