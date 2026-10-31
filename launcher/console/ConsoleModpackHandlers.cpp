// SPDX-License-Identifier: GPL-3.0-only
// Modrinth modpack browser for the console renderer: search, releases and install. Installs go through
// Prism's own InstanceImportTask with the same pack metadata and icon naming as the desktop Modrinth page,
// so Prism can offer pack updates later. Remote icons are fetched here and passed on as data URLs because
// the renderer may not load anything from the network.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>
#include "ConsoleBridge.h"
#include "ConsoleBridgeSupport.h"

using namespace consolebridge;

namespace {
const QString kModrinthApi = QStringLiteral("https://api.modrinth.com/v2");
constexpr int kPageSize = 24;

QString iconDataUrl(const QByteArray& bytes)
{
    QImage image;
    if (!image.loadFromData(bytes))
        return {};
    image = image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}
}  // namespace

// The browser state is polled only when this revision moves, so an idle list costs nothing per refresh.
void ConsoleBridge::modpacksChanged()
{
    ++m_modpacksRevision;
    emit changed();
}

void ConsoleBridge::modrinthGet(const QUrl& url, std::function<void(const QJsonDocument&, const QString&)> done)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    auto* reply = APPLICATION->network()->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) {
            done({}, QObject::tr("Modrinth is limiting requests right now. Wait a minute, then try again."));
            return;
        }
        if (status == 404) {
            done({}, QObject::tr("Modrinth no longer has this modpack."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const bool offline = status == 0;
            done({}, offline ? QObject::tr("Can't reach Modrinth. Check the network connection, then try again.")
                             : QObject::tr("Modrinth returned an error (%1). Try again shortly.").arg(status));
            return;
        }
        QJsonParseError parseError;
        auto json = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            done({}, QObject::tr("Modrinth sent a reply Prism could not read. Try again shortly."));
            return;
        }
        done(json, {});
    });
}

void ConsoleBridge::fetchModpackIcon(const QString& project, const QUrl& url)
{
    if (m_modpackIcons.contains(project) || !url.isValid() || url.scheme() != "https")
        return;
    m_modpackIcons.insert(project, {});  // in flight; never fetched twice
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    auto* reply = APPLICATION->network()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, project] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError && reply->size() < 4 * 1024 * 1024)
            m_modpackIcons.insert(project, iconDataUrl(reply->readAll()));
        modpacksChanged();
    });
}

void ConsoleBridge::registerModpackHandlers()
{
    // Hits only flag whether an icon is ready; the renderer fetches each icon once via modpackIcons and keeps it,
    // so the state polled on every change stays small however long the list grows.
    on({ "modpacks" }, [this](const QString&, const QVariantMap&) -> QVariantMap {
        auto state = m_modpacks;
        QVariantList hits;
        for (const auto& value : m_modpacks.value("hits").toList()) {
            auto hit = value.toMap();
            hit["hasIcon"] = !m_modpackIcons.value(hit.value("id").toString()).isEmpty();
            hits.append(hit);
        }
        state["hits"] = hits;
        return ok(state);
    });

    on({ "modpackIcons" }, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        QVariantMap icons;
        for (const auto& id : args.value("ids").toStringList().mid(0, 200))
            if (const auto icon = m_modpackIcons.value(id); !icon.isEmpty())
                icons[id] = icon;
        return ok(icons);
    });

    on({ "searchModpacks" }, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        const auto query = args.value("query").toString().trimmed().left(200);
        const int offset = qBound(0, args.value("offset").toInt(), 10000);
        QUrlQuery params;
        params.addQueryItem("query", query);
        params.addQueryItem("facets", R"([["project_type:modpack"]])");
        params.addQueryItem("index", query.isEmpty() ? "downloads" : "relevance");
        params.addQueryItem("limit", QString::number(kPageSize));
        params.addQueryItem("offset", QString::number(offset));
        QUrl url(kModrinthApi + "/search");
        url.setQuery(params);

        const int serial = ++m_modpackSearchSerial;
        m_modpacks["query"] = query;
        m_modpacks["loading"] = true;
        m_modpacks["error"] = QString();
        if (offset == 0) {
            m_modpacks["hits"] = QVariantList{};
            // Bound the icon cache: a new search keeps nothing it no longer shows.
            if (m_modpackIcons.size() > 300)
                m_modpackIcons.clear();
        }
        modpacksChanged();
        modrinthGet(url, [this, serial, offset](const QJsonDocument& json, const QString& error) {
            if (serial != m_modpackSearchSerial)
                return;  // a newer search replaced this one
            m_modpacks["loading"] = false;
            m_modpacks["error"] = error;
            auto hits = offset == 0 ? QVariantList{} : m_modpacks.value("hits").toList();
            for (const auto& value : json.object().value("hits").toArray()) {
                const auto hit = value.toObject();
                const auto id = hit.value("project_id").toString();
                const auto gameVersions = hit.value("versions").toArray();
                hits.append(QVariantMap{ { "id", id },
                                         { "slug", hit.value("slug").toString() },
                                         { "title", hit.value("title").toString() },
                                         { "description", hit.value("description").toString() },
                                         { "author", hit.value("author").toString() },
                                         { "downloads", hit.value("downloads").toDouble() },
                                         { "categories", hit.value("display_categories").toVariant() },
                                         { "minecraft", gameVersions.isEmpty() ? QString() : gameVersions.last().toString() } });
                fetchModpackIcon(id, QUrl(hit.value("icon_url").toString()));
            }
            m_modpacks["hits"] = hits;
            m_modpacks["total"] = error.isEmpty() ? json.object().value("total_hits").toInt() : m_modpacks.value("total").toInt();
            modpacksChanged();
        });
        return ok();
    });

    on({ "modpackVersions" }, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        const auto project = args.value("project").toString();
        if (project.isEmpty() || project.contains('/') || project.size() > 64)
            return fail("Choose a modpack");
        m_modpacks["project"] = project;
        m_modpacks["versions"] = QVariantList{};
        m_modpacks["versionsLoading"] = true;
        modpacksChanged();
        modrinthGet(QUrl(kModrinthApi + "/project/" + project + "/version"), [this, project](const QJsonDocument& json, const QString& error) {
            if (m_modpacks.value("project") != project)
                return;
            QVariantList versions;
            for (const auto& value : json.array()) {
                const auto version = value.toObject();
                QJsonObject file;
                for (const auto& candidate : version.value("files").toArray())
                    if (file.isEmpty() || candidate.toObject().value("primary").toBool())
                        file = candidate.toObject();
                if (!file.value("filename").toString().endsWith(".mrpack"))
                    continue;
                versions.append(QVariantMap{ { "id", version.value("id").toString() },
                                             { "name", version.value("name").toString() },
                                             { "number", version.value("version_number").toString() },
                                             { "type", version.value("version_type").toString() },
                                             { "minecraft", version.value("game_versions").toVariant() },
                                             { "loaders", version.value("loaders").toVariant() },
                                             { "published", version.value("date_published").toString() },
                                             { "url", file.value("url").toString() },
                                             { "size", file.value("size").toDouble() } });
                if (versions.size() >= 50)
                    break;
            }
            m_modpacks["versions"] = versions;
            m_modpacks["versionsLoading"] = false;
            m_modpacks["error"] = error;
            modpacksChanged();
        });
        return ok();
    });

    on({ "installModpack" }, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        if (m_task && !m_task->isFinished())
            return fail("Wait for the current task to finish");
        const auto name = args.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 120)
            return fail("Enter a name for the new instance");
        const auto project = args.value("project").toString();
        if (project != m_modpacks.value("project").toString())
            return fail("Choose the modpack again");
        QVariantMap version;
        for (const auto& value : m_modpacks.value("versions").toList())
            if (value.toMap().value("id") == args.value("version"))
                version = value.toMap();
        const QUrl url(version.value("url").toString());
        // Only Modrinth's CDN; the renderer never chooses an arbitrary download location.
        if (version.isEmpty() || url.scheme() != "https" || url.host() != "cdn.modrinth.com")
            return fail("Choose a release to install");

        QMap<QString, QString> extraInfo{ { "pack_id", project }, { "pack_version_id", version.value("id").toString() } };
        auto* task = new InstanceImportTask(url, true, m_window, std::move(extraInfo));
        task->setName(name);
        task->setIcon("default");
        // Same icon naming as Prism's desktop Modrinth page.
        QString slug;
        for (const auto& value : m_modpacks.value("hits").toList())
            if (value.toMap().value("id") == project)
                slug = value.toMap().value("slug").toString();
        const auto icon = m_modpackIcons.value(project);
        if (!slug.isEmpty() && icon.startsWith("data:image/png;base64,")) {
            const auto file = QDir::temp().filePath("modrinth_" + slug + ".png");
            QFile out(file);
            if (out.open(QIODevice::WriteOnly) && out.write(QByteArray::fromBase64(icon.mid(22).toLatin1())) > 0) {
                out.close();
                APPLICATION->icons()->installIcon(file, "modrinth_" + slug + ".png");
                task->setIcon("modrinth_" + slug);
            }
            QFile::remove(file);
        }
        return startTask(Task::Ptr(APPLICATION->instances()->wrapInstanceTask(task)), "Install " + name);
    });
}
