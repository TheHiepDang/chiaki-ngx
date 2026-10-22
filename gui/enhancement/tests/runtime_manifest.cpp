// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "../runtime_manifest.h"
#include "../runtime_route.h"
#include <QCoreApplication>
#include <QTemporaryDir>

#define CHECK(x) do { if (!(x)) { qCritical() << "FAIL" << __LINE__ << #x; return 1; } } while (0)
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    CHECK(directory.isValid());
    auto write = [&](const QString &name, const QByteArray &data) {
        const QString path = QDir(directory.path()).filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    auto digest = [](const QByteArray &data) {
        return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    };
    QJsonObject packages;
    packages.insert("neural", QJsonObject{{"nvngx_dlssnr.dll", digest("nr")}});
    packages.insert("frame_generation", QJsonObject{{"nvngx_dlssg.dll", QJsonArray{digest("official"), digest("stock")}}});
    packages.insert("sm86", QJsonObject{{"nvngx_dlssg.dll", digest("stock")}, {"sm86-proxy/version.dll", digest("proxy")}, {"sm86-proxy/backend.dll", digest("backend")}});
    auto manifest = [&]() { return write("runtime-manifest.json", QJsonDocument(QJsonObject{{"schema", 2}, {"packages", packages}}).toJson()); };
    CHECK(manifest());
    CHECK(write("nvngx_dlssg.dll", "official"));
    // Native FG works without NR or proxy files, even when their pins exist.
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), false, true).isEmpty());
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), true, true).contains("nvngx_dlssnr.dll"));
    CHECK(write("nvngx_dlssg.dll", "stock"));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), false, true).isEmpty());
    CHECK(write("sm86-proxy/version.dll", "proxy"));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), false, true, true).contains("backend.dll"));
    CHECK(write("sm86-proxy/backend.dll", "backend"));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), false, true, true).isEmpty());
    CHECK(write("nvngx_dlssg.dll", "official"));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), false, true, true).contains("does not match"));
    // Bundled proxy files must not affect native RTX 40/50 validation.
    CHECK(write("sm86-proxy/backend.dll", "broken"));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), false, true).isEmpty());
    CHECK(write("nvngx_dlssg.dll", "tampered"));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), false, true).contains("does not match"));
    CHECK(write("nvngx_dlssnr.dll", "nr"));
    // NR does not depend on the broken FG file or any proxy package.
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), true, false).isEmpty());
    packages.insert("neural", QJsonObject{{"nvngx_dlssnr.dll", digest("nr")}, {"../escape.dll", digest("escape")}});
    CHECK(manifest());
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), true, false).contains("invalid path"));
    packages.insert("neural", QJsonObject{{"nvngx_dlssnr.dll", QJsonArray{123}}});
    CHECK(manifest());
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), true, false).contains("invalid hash"));
    CHECK(write("runtime-manifest.json", QJsonDocument(QJsonObject{{"schema", 1}, {"files", QJsonObject{{"nvngx_dlssnr.dll", digest("nr")}}}}).toJson()));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), true, false).isEmpty());
    CHECK(write("runtime-manifest.json", "{invalid"));
    CHECK(chiaki_enhancement::verifyRuntimeManifest(directory.path(), true, false).contains("invalid"));
    CHECK(chiaki_enhancement::isRtxAdapter(0x10de, L"NVIDIA GeForce RTX 3070"));
    CHECK(chiaki_enhancement::isRtxAdapter(0x10de, L"NVIDIA GeForce RTX 5090"));
    CHECK(chiaki_enhancement::isRtxAdapter(0x10de, L"NVIDIA RTX A6000"));
    CHECK(!chiaki_enhancement::isRtxAdapter(0x10de, L"NVIDIA GeForce GTX 1080"));
    CHECK(!chiaki_enhancement::isRtxAdapter(0x1002, L"AMD Radeon RX 7900 XTX"));
    CHECK(!chiaki_enhancement::isRtxAdapter(0x8086, L"Intel Arc A770"));
    CHECK(!chiaki_enhancement::isRtxAdapter(0x1002, L"RTX name on a non-NVIDIA adapter"));
    CHECK(chiaki_enhancement::usesSm86(L"NVIDIA GeForce RTX 3070"));
    CHECK(chiaki_enhancement::usesSm86(L"NVIDIA GeForce RTX 2080 Ti"));
    CHECK(chiaki_enhancement::usesSm86(L"NVIDIA GeForce RTX 3060 Laptop GPU"));
    CHECK(!chiaki_enhancement::usesSm86(L"NVIDIA GeForce RTX 4090"));
    CHECK(!chiaki_enhancement::usesSm86(L"NVIDIA GeForce RTX 5090"));
    CHECK(!chiaki_enhancement::usesSm86(L"NVIDIA RTX A3000"));
    CHECK(!chiaki_enhancement::usesSm86(L"AMD Radeon RX 7900 XTX"));
    CHECK(!chiaki_enhancement::usesSm86(L"NVIDIA GeForce GTX 1080"));
    qInfo() << "Optional feature isolation, approved variants, tampering, traversal and legacy checks passed";
    return 0;
}
