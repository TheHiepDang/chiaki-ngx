// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <array>
#include <algorithm>

namespace NgxRelease {
constexpr const char *Repository = "https://github.com/TheHiepDang/chiaki-ngx";
constexpr const char *Api = "https://api.github.com/repos/TheHiepDang/chiaki-ngx/releases";
struct Version {
    std::array<int,3> numbers{{0,0,0}};
    QString prerelease, text;
    bool valid = false;
    static Version parse(QString value) {
        Version result;
        static const QRegularExpression pattern(QStringLiteral("^[vV]?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:\\.(0|[1-9][0-9]*))?(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?\\z"));
        if(value.size()>128) return result;
        auto match=pattern.match(value);
        if(!match.hasMatch()) return result;
        for(int n=0;n<3;++n) {
            bool okay=true;
            const QString number=match.captured(n+1);
            result.numbers[n]=number.isEmpty() ? 0 : number.toInt(&okay);
            if(!okay || result.numbers[n]>65535) return Version{};
        }
        result.prerelease=match.captured(4);
        for(const QString &part:result.prerelease.split('.')) {
            const bool numeric=!part.isEmpty() && std::all_of(part.cbegin(),part.cend(),[](QChar c){return c>='0' && c<='9';});
            if(numeric && part.size()>1 && part.startsWith('0')) return Version{};
        }
        result.text=value.startsWith('v',Qt::CaseInsensitive)?value.mid(1):value;
        result.valid=true;
        return result;
    }
    int compare(const Version &other) const {
        if(numbers!=other.numbers) return numbers<other.numbers?-1:1;
        if(prerelease==other.prerelease) return 0;
        if(prerelease.isEmpty()) return 1;
        if(other.prerelease.isEmpty()) return -1;
        const auto a=prerelease.split('.'),b=other.prerelease.split('.');
        for(int n=0;n<qMin(a.size(),b.size());++n) {
            if(a[n]==b[n]) continue;
            const auto numeric=[](const QString &s){return std::all_of(s.cbegin(),s.cend(),[](QChar c){return c>='0' && c<='9';});};
            const bool an=numeric(a[n]),bn=numeric(b[n]);
            if(an!=bn) return an?-1:1;
            if(an && a[n].size()!=b[n].size()) return a[n].size()<b[n].size()?-1:1;
            return a[n]<b[n]?-1:1;
        }
        return a.size()<b.size()?-1:1;
    }
};
struct Candidate {
    Version version;
    QString tag, assetName, sha256;
    QUrl page, download;
    qint64 size=0;
    bool prerelease=false;
    bool installable() const {return download.isValid() && !download.isEmpty() && sha256.size()==64 && size>0 && size<=1024LL*1024*1024;}
};
inline Candidate candidate(const QJsonObject &object) {
    Candidate c;
    if(object.value("draft").toBool()) return c;
    c.tag=object.value("tag_name").toString();
    c.version=Version::parse(c.tag);
    // Published fork baseline predates versioned tags and was marked as a regular release.
    if(c.tag==QStringLiteral("pre-release") && object.value("name").toString()==QStringLiteral("chiaki-ngx-alpha-v0.1"))
        c.version=Version::parse(QStringLiteral("0.1"));
    if(!c.version.valid) return c;
    c.prerelease=object.value("prerelease").toBool() || !c.version.prerelease.isEmpty();
    c.page=QUrl(QString::fromLatin1(Repository)+"/releases/tag/"+QString::fromLatin1(QUrl::toPercentEncoding(c.tag)));
    const QString canonical="chiaki-ngx-windows-x64-v"+c.version.text+".zip";
    for(const auto &item:object.value("assets").toArray()) {
        const auto asset=item.toObject();const QString name=asset.value("name").toString();
        if(name!=canonical && name!=QStringLiteral("chiaki-ngx.zip")) continue;
        const QUrl url(asset.value("browser_download_url").toString());
        const QUrl expected(QString::fromLatin1(Repository)+"/releases/download/"+QString::fromLatin1(QUrl::toPercentEncoding(c.tag))+"/"+name);
        const QString digest=asset.value("digest").toString();
        static const QRegularExpression hash("^sha256:([0-9a-fA-F]{64})$");
        auto match=hash.match(digest);
        if(url!=expected || !match.hasMatch() || asset.value("state").toString()!="uploaded") continue;
        const qint64 size=asset.value("size").toInteger();
        if(size<=0 || size>1024LL*1024*1024) continue;
        c.assetName=name;c.download=url;c.sha256=match.captured(1).toLower();c.size=size;
        if(name==canonical) break;
    }
    return c;
}
inline Candidate newest(const QJsonArray &releases,bool includePrereleases) {
    Candidate best;
    for(const auto &item:releases) {
        const auto c=candidate(item.toObject());
        if(!c.version.valid || (!includePrereleases && c.prerelease)) continue;
        if(!best.version.valid || c.version.compare(best.version)>0) best=c;
    }
    return best;
}
}
