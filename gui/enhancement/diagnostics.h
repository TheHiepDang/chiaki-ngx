// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include <QString>
#include <QtLogging>

namespace DiagnosticLog {
// Empty directory uses the same application-data log directory as session logs.
bool initialize(const QString &directory = QString());
void record(QtMsgType type, const QString &message);
void shutdown();
QString sanitize(const QString &message);
QString taggedMessage(const QString &message);
QString filename();
QString reportInstructions();
}
