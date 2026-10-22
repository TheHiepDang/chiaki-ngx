// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace chiaki_enhancement
{
// Return an actionable error, or an empty string after validating all files
// used by this request. Optional packages are not prerequisites for each other.
inline QString verifyRuntimeManifest(const QString &runtime, bool neural, bool frameGeneration, bool sm86 = false)
{
    QFile manifest(QDir(runtime).filePath(QStringLiteral("runtime-manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly))
        return QStringLiteral("Optional runtime package is missing; see the release README for runtimes/");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    const auto object = document.object();
    const int schema = object.value(QStringLiteral("schema")).toInt();
    if (parseError.error != QJsonParseError::NoError || (schema != 1 && schema != 2))
        return QStringLiteral("Optional runtime manifest is invalid");
    QJsonObject files;
    if (schema == 1)
        files = object.value(QStringLiteral("files")).toObject();
    else
    {
        const auto packages = object.value(QStringLiteral("packages")).toObject();
        auto addPackage = [&](const QString &name, const QString &entry) {
            const auto package = packages.value(name).toObject();
            if (package.isEmpty() || !package.contains(entry))
                return false;
            for (auto file = package.begin(); file != package.end(); ++file)
            {
                // A route-specific package can narrow a shared runtime pin.
                // SM86 is added last and pins its backend's matching stock DLL.
                files.insert(file.key(), file.value());
            }
            return true;
        };
        if (neural && !addPackage(QStringLiteral("neural"), QStringLiteral("nvngx_dlssnr.dll")))
            return QStringLiteral("NR runtime is not pinned in this release; see the release README");
        if (frameGeneration &&
            !addPackage(QStringLiteral("frame_generation"), QStringLiteral("nvngx_dlssg.dll")))
            return QStringLiteral("FG runtime is not pinned in this release; see the release README");
        if (frameGeneration && sm86 &&
            (!packages.value(QStringLiteral("sm86")).toObject().contains(QStringLiteral("nvngx_dlssg.dll")) ||
             !addPackage(QStringLiteral("sm86"), QStringLiteral("sm86-proxy/version.dll"))))
            return QStringLiteral("Installed SM86 proxy is not pinned in this release");
    }
    if (files.isEmpty())
        return QStringLiteral("Optional runtime manifest contains no required files");
    const QString root = QDir(runtime).canonicalPath() + QStringLiteral("/");
    for (auto file = files.begin(); file != files.end(); ++file)
    {
        QFileInfo info(QDir(runtime).filePath(file.key()));
        // Check the lexical path first so a missing DLL reports a missing file,
        // rather than an invalid canonical path. Canonical checks block links.
        if (QDir::isAbsolutePath(file.key()) ||
            !info.absoluteFilePath().startsWith(root, Qt::CaseInsensitive) ||
            file.key().split('/').contains(QStringLiteral("..")) ||
            file.key().split('\\').contains(QStringLiteral("..")))
            return QStringLiteral("Optional runtime manifest contains an invalid path");
        if (!info.exists())
            return QStringLiteral("Required runtime file is missing: %1. See the release README.").arg(file.key());
        if (!info.canonicalFilePath().startsWith(root, Qt::CaseInsensitive))
            return QStringLiteral("Optional runtime manifest contains an invalid path");
        QJsonArray accepted;
        if (file.value().isString())
            accepted.append(file.value());
        else if (schema == 2 && file.value().isArray())
            accepted = file.value().toArray();
        if (accepted.isEmpty())
            return QStringLiteral("Optional runtime manifest has an invalid hash entry");
        for (const auto &pin : accepted)
            if (!pin.isString() || pin.toString().size() != 64)
                return QStringLiteral("Optional runtime manifest has an invalid hash entry");
        QFile input(info.absoluteFilePath());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input))
            return QStringLiteral("Cannot read optional runtime file: %1").arg(file.key());
        const auto digest = QString::fromLatin1(hash.result().toHex());
        bool matched = false;
        for (const auto &pin : accepted)
            matched |= digest.compare(pin.toString(), Qt::CaseInsensitive) == 0;
        if (!matched)
            return QStringLiteral("Optional runtime file does not match this release: %1. Use a listed DLL version.").arg(file.key());
        qInfo().nospace() << "[RTX][Runtime] file=" << file.key() << " sha256=" << digest;
    }
    return {};
}
}
