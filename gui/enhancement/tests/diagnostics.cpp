// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "../diagnostics.h"
#include "../worker_log.h"
#include "../crash_dump_win.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QProcess>
#include <QDebug>
#include <cstdio>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "[Test][Diagnostics] FAIL %d %s\n", __LINE__, #x); qCritical() << "FAIL" << __LINE__ << #x; DiagnosticLog::shutdown(); return 1; } } while (0)
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (app.arguments().size() == 3 && app.arguments()[1] == "--crash-child") {
        if (!DiagnosticLog::initialize(app.arguments()[2])) return 2;
        qInfo() << "[test] Before controlled child exception";
        RaiseException(0xE0424348, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 3;
    }
#endif
#ifdef Q_OS_WIN
    if (app.arguments().size() == 3 && app.arguments()[1] == "--worker-crash-child") {
        if (!DiagnosticLog::initialize(app.arguments()[2])) return 2;
        ChiakiCrash::installWorkerFromEnvironment();
        RaiseException(0xE0424357, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 3;
    }
#endif
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    CHECK(DiagnosticLog::initialize(temporary.path()));
    const QString path = DiagnosticLog::filename();
    qInfo().noquote() << "[RTX][Config] NR=1 scale=74 FSR=1";
    qInfo().noquote() << "[EyeCandy][Config] Exposure=0.35";
    CHECK(DiagnosticLog::taggedMessage("[RTX][FG] first\nsecond") == "[RTX][FG] first\n[RTX][FG] second");
    CHECK(DiagnosticLog::taggedMessage("plain failure") == "[App] plain failure");
    qWarning().noquote() << "[RTX][Runtime] Load failed\nWin32 126\n[EyeCandy][State] bypassed\nHDR output";
    chiaki_enhancement::WorkerLogBuffer worker;
    CHECK(worker.append("[RTX][NR] frag").isEmpty());
    const auto fragments = worker.append("ment\nplain worker line\n");
    CHECK(fragments.size() == 2 && fragments[0] == "[RTX][NR] fragment" && fragments[1] == "[RTX][Worker] plain worker line");
    const auto utf8 = QString::fromUtf8("[RTX][NR] café\n").toUtf8();
    CHECK(worker.append(utf8.left(utf8.size()-2)).isEmpty());
    CHECK(worker.append(utf8.right(2)) == QStringList{QString::fromUtf8("[RTX][NR] café")});
    CHECK(worker.append("access_token=worker-secret").isEmpty());
    for (const auto &line : worker.append(" Bearer worker-bearer\n")) DiagnosticLog::record(QtWarningMsg,line);
    CHECK(worker.append("last fragment").isEmpty());
    CHECK(worker.append({},true) == QStringList{QString("[RTX][Worker] last fragment")});
    CHECK(worker.append(QByteArray(70*1024,'x')).size() == 1);
    CHECK(worker.append("secret-tail\nrecovered\n") == QStringList{QString("[RTX][Worker] recovered")});
    qWarning().noquote() << "access_token=secret-token Bearer bearer-secret 192.168.1.25 passcode=9876 duid=private-id";
    const auto sanitized = DiagnosticLog::sanitize("https://auth.example/?code=secret 2001:db8::1234 01:23:45:67:89:ab refresh_token=hidden");
    CHECK(!sanitized.contains("secret") && !sanitized.contains("hidden") && !sanitized.contains("2001:db8") && !sanitized.contains("01:23:45"));
    const auto jsonSecrets = DiagnosticLog::sanitize(R"({"access_token":"json-secret", "refresh_token":"refresh-secret", "registkey":"private-key"})");
    CHECK(!jsonSecrets.contains("json-secret") && !jsonSecrets.contains("refresh-secret") && !jsonSecrets.contains("private-key"));
    CHECK(!DiagnosticLog::sanitize("Authorization: Basic private-basic").contains("private-basic"));
    std::vector<std::thread> writers;
    for (int n=0;n<4;++n) writers.emplace_back([n] { for (int i=0;i<30;++i) DiagnosticLog::record(QtInfoMsg, QString("[thread] %1 %2").arg(n).arg(i)); });
    for (auto &thread : writers) thread.join();
    QFile file(path); CHECK(file.open(QIODevice::ReadOnly));
    QByteArray contents=file.readAll();file.close();
    CHECK(contents.contains("Report a bug:") && contents.contains("chiaki_session_*.log") && contents.contains("scale=74"));
    CHECK(contents.contains("[INFO] [RTX][Config]") && contents.contains("[INFO] [EyeCandy][Config]"));
    CHECK(contents.contains("[WARN] [RTX][Runtime] Win32 126") && contents.contains("[WARN] [EyeCandy][State] HDR output"));
    CHECK(!contents.contains("worker-secret") && !contents.contains("worker-bearer"));
    CHECK(contents.contains("loader_*.jsonl") && contents.contains("[RTX]"));
    CHECK(!contents.contains("secret-token") && !contents.contains("bearer-secret") && !contents.contains("192.168.1.25") && !contents.contains("9876") && !contents.contains("private-id"));
    for(int n=0;n<1100;++n) DiagnosticLog::record(QtInfoMsg, QString(6000,'x'));
    CHECK(QFileInfo(path).size() <= 4*1024*1024+2000);
    CHECK(QFileInfo::exists(path+".previous.log"));
#ifdef Q_OS_WIN
    const QString crashes = temporary.path()+"/child";
    QProcess child;child.setProgram(QCoreApplication::applicationFilePath());child.setArguments({"--crash-child",crashes});child.start();
    CHECK(child.waitForFinished(15000));CHECK(child.exitCode()!=0);
    QDir folder(crashes);const auto reports=folder.entryList({"*-crash.txt"},QDir::Files);
    CHECK(reports.size()==1);
    QFile report(folder.filePath(reports[0]));CHECK(report.open(QIODevice::ReadOnly));
    const auto crash=report.readAll();
    CHECK(crash.contains("[App][Crash]") && crash.contains("e0424348") && crash.contains("Report a bug:") && crash.contains("Native minidump saved"));
    const auto dumps=folder.entryList({"*-crash.dmp"},QDir::Files);CHECK(dumps.size()==1 && QFileInfo(folder.filePath(dumps[0])).size()>0);
    const QString workerCrashDir = temporary.path()+"/worker-child";
    QProcess workerChild; workerChild.setProgram(QCoreApplication::applicationFilePath());
    workerChild.setArguments({"--worker-crash-child",workerCrashDir}); workerChild.start();
    CHECK(workerChild.waitForFinished(15000) && workerChild.exitCode()!=0);
    QDir workerFolder(workerCrashDir);
    const auto workerReports=workerFolder.entryList({"*-crash.txt"},QDir::Files);
    CHECK(workerReports.size()==1);
    QFile workerReport(workerFolder.filePath(workerReports[0]));CHECK(workerReport.open(QIODevice::ReadOnly));
    const auto workerCrash=workerReport.readAll();
    CHECK(workerCrash.contains("[RTX][Crash]") && workerCrash.contains("e0424357") && workerCrash.contains("loader_*.jsonl"));
    CHECK(workerFolder.entryList({"*-crash.dmp"},QDir::Files).size()==1);

#endif
    qInfo() << "PASS: flushed persistent logs, credentials/IP redaction, concurrent writes, bounded rotation and controlled native crash report/dump";
    DiagnosticLog::shutdown();
    return 0;
}
