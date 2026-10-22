// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include <QByteArray>
#include <QStringList>

namespace chiaki_enhancement {
// QProcess notifications are byte chunks, not lines. Keep partial UTF-8 and
// secret fields together until a complete message can be tagged/redacted.
class WorkerLogBuffer {
    QByteArray pending;
    bool dropping = false;
    static constexpr int MaxLine = 64 * 1024;
    static QString tag(const QByteArray &bytes) {
        QString line = QString::fromUtf8(bytes);
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) return {};
        return line.startsWith("[RTX]") ? line : QStringLiteral("[RTX][Worker] ") + line;
    }
public:
    QStringList append(const QByteArray &bytes, bool flush = false) {
        pending += bytes;
        QStringList lines;
        int newline;
        while ((newline = pending.indexOf('\n')) >= 0) {
            const auto line = pending.left(newline);
            pending.remove(0, newline + 1);
            if (dropping) { dropping = false; continue; }
            if (line.size() > MaxLine) {
                lines.append(QStringLiteral("[RTX][Worker] Oversized diagnostic line discarded"));
                continue;
            }
            const auto text = tag(line);
            if (!text.isEmpty()) lines.append(text);
        }
        if (dropping) pending.clear();
        else if (pending.size() > MaxLine) {
            pending.clear();
            dropping = true;
            lines.append(QStringLiteral("[RTX][Worker] Oversized diagnostic line discarded"));
        }
        if (flush) {
            if (!dropping && !pending.isEmpty()) lines.append(tag(pending));
            pending.clear();
            dropping = false;
        }
        return lines;
    }
};
}
