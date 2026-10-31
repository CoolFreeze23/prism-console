// SPDX-License-Identifier: GPL-3.0-only
// Library requests from the console renderer. Registered by ConsoleBridge; bodies run on the GUI thread.
#include <algorithm>
#include "ConsoleBridge.h"
#include "ConsoleBridgeSupport.h"
#include "minecraft/launch/MinecraftTarget.h"
#include <io/stream_reader.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_string.h>
#include <tag_primitive.h>
#include <io/stream_writer.h>
#include <FileSystem.h>
#include <QRegularExpression>
#include <sstream>

using namespace consolebridge;

namespace {
// servers.dat, read and written the way Prism's Servers page does. A missing file is an empty list; an unreadable
// one is reported so it is never overwritten.
std::unique_ptr<nbt::tag_compound> loadServersDat(MinecraftInstance* inst, bool* unreadable = nullptr)
{
    if (unreadable) *unreadable = false;
    QFile file(QDir(inst->gameRoot()).filePath("servers.dat"));
    if (!file.exists()) return nullptr;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) {
        if (unreadable) *unreadable = true;
        return nullptr;
    }
    const auto bytes = file.readAll();
    try {
        std::istringstream stream(std::string(bytes.constData(), bytes.size()));
        auto root = nbt::io::read_compound(stream).second;
        if (root && (!root->has_key("servers") || root->has_key("servers", nbt::tag_type::List))) return std::move(root);
    } catch (...) {
    }
    if (unreadable) *unreadable = true;
    return nullptr;
}

bool saveServersDat(MinecraftInstance* inst, const nbt::tag_compound& root)
{
    try {
        const auto path = QDir(inst->gameRoot()).filePath("servers.dat");
        if (!FS::ensureFilePathExists(path)) return false;
        std::ostringstream stream;
        nbt::io::write_tag("", root, stream);
        const auto data = stream.str();
        return FS::write(path, QByteArray(data.data(), int(data.size()))).has_value();
    } catch (...) {
        return false;
    }
}

QString serverText(nbt::tag_compound& server, const char* key)
{
    return server.has_key(key, nbt::tag_type::String) ? QString::fromStdString(std::string(server[key])) : QString();
}

QVariantList readServers(MinecraftInstance* inst)
{
    QVariantList servers;
    auto root = loadServersDat(inst);
    if (!root || !root->has_key("servers", nbt::tag_type::List)) return servers;
    try {
        for (auto& entry : root->at("servers").as<nbt::tag_list>()) {
            auto& server = entry.as<nbt::tag_compound>();
            const auto icon = serverText(server, "icon");
            servers.append(QVariantMap{{"name", serverText(server, "name")}, {"address", serverText(server, "ip")},
                {"icon", icon.isEmpty() ? QString() : "data:image/png;base64," + icon}});
        }
    } catch (...) {
        return {};
    }
    return servers;
}
}  // namespace

void ConsoleBridge::registerLibraryHandlers()
{
    on({"snapshot"}, [this](const QString&, const QVariantMap&) -> QVariantMap {
        auto* list = APPLICATION->instances();
        QVariantList instances, accounts;
        for (int i = 0; i < list->count(); ++i) {
            auto* inst = list->at(i);
            // Prism loads component metadata lazily; use its offline loading
            // path rather than treating an unopened instance as uninstalled.
            if (!inst->getPackProfile()->getComponent("net.minecraft")) inst->getStatusbarDescription();
            const auto minecraft = inst->getPackProfile()->getComponent("net.minecraft");
            const auto version = minecraft ? (minecraft->getVersion().isEmpty() ? minecraft->m_version : minecraft->getVersion()) : QString();
            // Encoded once per icon; QIcon::cacheKey changes when the icon itself changes.
            const auto icon = APPLICATION->icons()->getIcon(inst->iconKey());
            auto& cached = m_instanceIcons[inst->iconKey()];
            if (cached.first != icon.cacheKey() || cached.second.isEmpty()) {
                // At the icon's own size (pixel art stays pixel art; the renderer scales it with hard edges),
                // capped at 256 px.
                QSize size(128, 128);
                if (const auto sizes = icon.availableSizes(); !sizes.isEmpty())
                    size = *std::max_element(sizes.begin(), sizes.end(), [](const QSize& a, const QSize& b) { return a.width() < b.width(); });
                if (size.width() > 256 || size.height() > 256) size.scale(256, 256, Qt::KeepAspectRatio);
                cached = {icon.cacheKey(), imageDataUrl(icon.pixmap(size).toImage())};
            }
            // The mod loader shown with the Minecraft version ("NeoForge 21.1.252").
            QString loader;
            for (const auto& [uid, label] : std::initializer_list<std::pair<const char*, const char*>>{
                     {"net.neoforged", "NeoForge"}, {"net.minecraftforge", "Forge"}, {"net.fabricmc.fabric-loader", "Fabric"}, {"org.quiltmc.quilt-loader", "Quilt"}})
                if (const auto component = inst->getPackProfile()->getComponent(uid)) {
                    loader = QString("%1 %2").arg(label, component->getVersion().isEmpty() ? component->m_version : component->getVersion()).trimmed();
                    break;
                }
            // Prism marks an instance running when its launch task starts, before library/asset checks and
            // before Java exists. The launch counts as started only once the game process has a PID.
            const bool launchActive = APPLICATION->instanceLaunchActive(inst->id());
            // The launch screen stays until the launcher hands the screen to the game (see watchGameWindow);
            // this is the safety net should the log never show the process starting.
            if (auto* task = inst->getLaunchTask(); launchActive && task && task->pid() > 0 && !m_handOffScheduled.contains(inst->id())) {
                m_handOffScheduled.insert(inst->id());
                QTimer::singleShot(12000, this, [this, id = inst->id()] { handOffToGame(id); });
            }
            const bool launching = launchActive && !m_gameStarted.contains(inst->id());
            instances.append(QVariantMap{
                {"id", inst->id()}, {"name", inst->name()}, {"group", list->getInstanceGroup(inst->id())},
                {"icon", cached.second},
                {"version", version}, {"loader", loader},
                {"running", inst->isRunning()}, {"launching", launching},
                {"canLaunch", inst->canLaunch() && !launchActive}, {"notes", inst->notes()},
                {"minMemory", inst->settings()->get("MinMemAlloc").toInt()}, {"maxMemory", inst->settings()->get("MaxMemAlloc").toInt()},
                {"lastLaunch", inst->lastLaunch()}, {"playTime", APPLICATION->settings()->get("ShowGameTime").toBool() ? qint64(inst->totalTimePlayed()) : 0}, {"steamAppId", steamAppId(inst)},
                {"launchSeconds", typicalLaunchSeconds(inst)}, {"gameWindow", m_gameStarted.contains(inst->id()) && inst->isRunning()}});
        }
        auto* accountList = APPLICATION->accounts();
        for (int i = 0; i < accountList->count(); ++i) {
            auto account = accountList->at(i);
            accounts.append(QVariantMap{{"id", account->profileId()}, {"name", account->profileName()},
                                       {"selected", account == accountList->defaultAccount()}});
        }
        QVariantList versions;
        auto versionList = APPLICATION->metadataIndex()->get("net.minecraft");
        if (versionList->isLoaded()) {
            for (const auto& version : versionList->versions()) {
                if (version->type() == "release") versions.append(version->version());
            }
        }
        return ok(QVariantMap{{"instances", instances}, {"accounts", accounts}, {"login", m_loginState},
                             {"task", m_taskState}, {"versions", versions}, {"launches", m_launchStates}, {"prompt", m_prompt},
                             {"language", APPLICATION->settings()->get("Language").toString()},
                             {"modpacksRevision", m_modpacksRevision},
                             {"sound", QVariantMap{{"enabled", APPLICATION->settings()->get("ConsoleSoundEnabled").toBool()},
                                                   {"volume", APPLICATION->settings()->get("ConsoleSoundVolume").toInt()}}},
                             {"setup", QVariantMap{{"required", !APPLICATION->settings()->get("ConsoleSetupComplete").toBool()},
                                 {"automaticJava", APPLICATION->settings()->get("AutomaticJavaDownload").toBool()}}}});
    });

    on({"completeSetup"}, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        if (args.value("automaticJava").metaType().id() != QMetaType::Bool) return fail("Choose how Java should be managed");
        auto settings = APPLICATION->settings();
        if (!args.value("keepExisting").toBool()) {
            settings->set("AutomaticJavaDownload", args.value("automaticJava"));
            settings->set("AutomaticJavaSwitch", args.value("automaticJava"));
        }
        settings->set("UserAskedAboutAutomaticJavaDownload", true);
        settings->set("ConsoleSetupComplete", true);
        emit changed(); return ok();
    });

    on({"loadVersions"}, [this](const QString&, const QVariantMap&) -> QVariantMap {
        auto versions = APPLICATION->metadataIndex()->get("net.minecraft");
        if (versions->isLoaded()) return ok();
        return startTask(versions->getLoadTask(), "Load Minecraft versions");
    });

    on({"cancelTask"}, [this](const QString&, const QVariantMap&) -> QVariantMap {
        if (!m_task || m_task->isFinished()) return ok();
        if (!m_task->canAbort() || !m_task->abort()) return fail("This operation cannot be cancelled at this stage");
        return ok();
    });

    on({"browseFiles"}, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        const auto home = QFileInfo(QDir::homePath()).canonicalFilePath();
        const auto requested = args.value("path", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
        const auto path = QFileInfo(requested).canonicalFilePath();
        auto allowed = [&](const QString& candidate) {
            return !candidate.isEmpty() && (candidate == home || candidate.startsWith(home + '/') || candidate.startsWith("/run/media/"));
        };
        if (!allowed(path) || !QFileInfo(path).isDir()) return fail("Choose an accessible folder in Home or removable storage");
        QVariantList entries;
        const QStringList suffixes = args.value("kind").toString() == "skin" ? QStringList{"png"} : args.value("kind").toString() == "resource" ? QStringList{"jar", "zip", "litemod"} : QStringList{"zip", "mrpack"};
        const QDir directory(path);
        for (const auto& file : directory.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable, QDir::DirsFirst | QDir::Name)) {
            if (!allowed(file.canonicalFilePath())) continue;
            if (!file.isDir() && !suffixes.contains(file.suffix().toLower())) continue;
            entries.append(QVariantMap{{"path", file.canonicalFilePath()}, {"name", file.fileName()}, {"directory", file.isDir()}});
            if (entries.size() >= 1000) break;
        }
        const auto parent = QFileInfo(directory.filePath("..")).canonicalFilePath();
        return ok(QVariantMap{{"path", path}, {"parent", allowed(parent) ? parent : QString()}, {"entries", entries}});
    });

    on({"importInstance"}, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        auto* list = APPLICATION->instances();
        if (m_task && !m_task->isFinished()) return fail("Wait for the current task to finish");
        const auto name = args.value("name").toString().trimmed();
        const auto source = args.value("source").toString().trimmed();
        if (name.isEmpty() || name.size() > 120) return fail("Enter an instance name");
        QUrl url(source);
        if (QDir::isAbsolutePath(source)) {
            QFileInfo file(source);
            if (!file.isFile() || !file.isReadable() || !(source.endsWith(".zip", Qt::CaseInsensitive) || source.endsWith(".mrpack", Qt::CaseInsensitive)))
                return fail("Choose a readable ZIP or MRPACK archive");
            url = QUrl::fromLocalFile(file.canonicalFilePath());
        } else if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty()) {
            return fail("Enter an absolute archive path or HTTPS download URL");
        }
        auto* task = new InstanceImportTask(url, false, m_window);
        task->setName(name);
        task->setIcon("default");
        return startTask(Task::Ptr(list->wrapInstanceTask(task)), "Import " + name);
    });

    on({"createInstance"}, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        auto* list = APPLICATION->instances();
        if (m_task && !m_task->isFinished()) return fail("Wait for the current task to finish");
        auto versions = APPLICATION->metadataIndex()->get("net.minecraft");
        const auto name = args.value("name").toString().trimmed();
        const auto version = args.value("version").toString();
        if (name.isEmpty() || name.size() > 120 || !versions->isLoaded() || !versions->hasVersion(version))
            return fail("Choose a name and a loaded Minecraft version");
        auto* task = new VanillaCreationTask(versions->getVersion(version));
        task->setName(name);
        task->setIcon("default");
        return startTask(Task::Ptr(list->wrapInstanceTask(task)), "Create " + name);
    });

    // Transitional routes retain Prism's established workflows until their
    // controller-native equivalents have been implemented and tested.
    on({"nativeDialog"}, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        static const QMap<QString, QByteArray> routes{
            {"add", "on_actionAddInstance_triggered"}, {"accounts", "on_actionManageAccounts_triggered"},
            {"settings", "on_actionSettings_triggered"}, {"about", "on_actionAbout_triggered"}};
        const auto action = routes.value(args.value("route").toString());
        if (action.isEmpty()) return fail("Unknown dialog");
        QMetaObject::invokeMethod(m_window, action.constData(), Qt::QueuedConnection);
        return ok();
    });

    on({"keyboard"}, [this](const QString&, const QVariantMap&) -> QVariantMap {
        return QDesktopServices::openUrl(QUrl("steam://open/keyboard")) ? ok() : fail("Steam keyboard unavailable");
    });

    onInstance({"copyInstance"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        auto* list = APPLICATION->instances();
        if (APPLICATION->instanceLaunchActive(inst->id()) || (m_task && !m_task->isFinished())) return fail("Stop the game and finish current tasks first");
        const auto name = args.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 120) return fail("Enter a name for the copy");
        InstanceCopyPrefs prefs;
        prefs.enableCopySaves(args.value("includeWorlds").toBool());
        prefs.enableCopyScreenshots(false); prefs.enableKeepPlaytime(false);
        auto* task = new InstanceCopyTask(inst, prefs);
        task->setName(name); task->setIcon(inst->iconKey());
        return startTask(Task::Ptr(list->wrapInstanceTask(task)), "Copy " + inst->name());
    });

    onInstance({"addToSteam"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap&) -> QVariantMap {
        if (m_task && !m_task->isFinished()) return fail("Wait for the current task to finish");
        auto task = makeShared<SteamShortcutTask>(inst, steamAppId(inst));
        const auto id = inst->id();
        connect(task.get(), &Task::succeeded, this, [this, id, task = task.get()] {
            if (auto* target = APPLICATION->instances()->getInstanceById(id)) {
                steamAppId(target); // registers the setting
                target->settings()->set("ConsoleSteamAppId", task->appId());
            }
            emit changed();
        });
        return startTask(task, "Add " + inst->name() + " to Steam");
    });

    onInstance({"saveNotes"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        const auto notes = args.value("notes").toString();
        if (notes.size() > 100000) return fail("Notes are too long");
        inst->setNotes(notes); emit changed(); return ok();
    });

    onInstance({"worlds", "copyWorld"}, [this](MinecraftInstance* inst, const QString& method, const QVariantMap& args) -> QVariantMap {
        auto* worlds = inst->worldList();
        worlds->update();
        if (method == "worlds") {
            QVariantList result;
            for (int i = 0; i < worlds->rowCount(); ++i) {
                const auto& world = (*worlds)[i];
                QImage icon(world.iconFile());
                result.append(QVariantMap{{"id", world.folderName()}, {"name", world.name()}, {"mode", world.gameType().toTranslatedString()},
                    {"lastPlayed", world.lastPlayed().toString(Qt::ISODate)}, {"lastPlayedMs", world.lastPlayed().toMSecsSinceEpoch()},
                    {"size", qint64(world.bytes())}, {"icon", icon.isNull() ? QString() : imageDataUrl(icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation))}});
            }
            return ok(result);
        }
        if (APPLICATION->instanceLaunchActive(inst->id()) || (m_task && !m_task->isFinished())) return fail("Stop the game and finish current tasks first");
        const auto name = args.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 100 || name == "." || name == ".." || name.contains('/') || name.contains('\\')) return fail("Enter a simple copy name");
        for (int i = 0; i < worlds->rowCount(); ++i) if ((*worlds)[i].folderName() == name) return fail("A world with that folder name already exists");
        for (int i = 0; i < worlds->rowCount(); ++i) if ((*worlds)[i].folderName() == args.value("world").toString())
            return startTask(Task::Ptr(worlds->createCopyWorldTask(i, name).release()), "Copy world");
        return fail("World not found");
    });

    onInstance({"exportInstance"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        if (APPLICATION->instanceLaunchActive(inst->id()) || (m_task && !m_task->isFinished())) return fail("Stop the game and finish current tasks first");
        const QDir root(inst->instanceRoot());
        QFileInfoList files;
        QDirIterator it(root.absolutePath(), QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info(it.next());
            const auto relative = root.relativeFilePath(info.filePath());
            if (relative.startsWith(".minecraft/logs/") || relative.startsWith(".minecraft/crash-reports/") || relative.startsWith(".minecraft/screenshots/")) continue;
            if (!args.value("includeWorlds").toBool() && relative.startsWith(".minecraft/saves/")) continue;
            files.append(info);
        }
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (!QDir().mkpath(directory)) return fail("Cannot create Downloads directory");
        const auto output = QDir(directory).filePath("Prism-instance-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".zip");
        auto reply = startTask(Task::Ptr(new MMCZip::ExportToZipTask(output, root, files)), "Export to " + output);
        if (reply.value("ok").toBool()) reply["data"] = output;
        return reply;
    });

    onInstance({"launchLog"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap&) -> QVariantMap {
        QVariantList lines;
        const auto model = m_logs.value(inst->id());
        if (model) for (int i = qMax(0, model->rowCount() - 500); i < model->rowCount(); ++i)
            lines.append(model->data(model->index(i, 0), Qt::DisplayRole).toString());
        return ok(lines); // Prism's existing log model applies its private-info censor.
    });

    onInstance({"cancelLaunch"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap&) -> QVariantMap {
        auto task = m_launchTasks.value(inst->id());
        if (!task || !task->canAbort()) return fail("Launch cannot be cancelled at this stage");
        // abort() can ask a controller-native question. Return to QWebChannel
        // first so the renderer can receive and answer it without deadlocking.
        QTimer::singleShot(0, task, [task] { if (task) task->abort(); });
        return ok();
    });

    onInstance({"launch"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        if (m_task && !m_task->isFinished()) return fail("Wait for the current operation to finish");
        if (APPLICATION->instanceLaunchActive(inst->id()) || !inst->canLaunch()) return fail("Instance cannot be launched now");
        // Optional quick play: straight into one of this instance's worlds, or one of its listed servers.
        MinecraftTarget::Ptr target;
        if (const auto server = args.value("server").toString(); !server.isEmpty()) {
            bool listed = false;
            for (const auto& entry : readServers(inst)) listed = listed || entry.toMap().value("address") == server;
            if (!listed) return fail("Server not found");
            target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(server, false));
        }
        if (const auto world = args.value("world").toString(); !world.isEmpty()) {
            auto* worlds = inst->worldList();
            worlds->update();
            bool found = false;
            for (int i = 0; i < worlds->rowCount() && !found; ++i) found = (*worlds)[i].folderName() == world;
            if (!found) return fail("World not found");
            target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(world, true));
        }
        if (!APPLICATION->accounts()->defaultAccount()) return fail("Select a signed-in account in Accounts first");
        // LaunchController handles auth, Java selection, install tasks and hooks.
        return APPLICATION->launch(inst, LaunchMode::Normal, target) ? ok() : fail("Prism declined the launch");
    });

    onInstance({"servers"}, [](MinecraftInstance* inst, const QString&, const QVariantMap&) -> QVariantMap { return ok(readServers(inst)); });

    // Add, edit or remove a saved server. Edits name the entry by its position and current address, so a list that
    // changed meanwhile is never edited in the wrong place; other tags (icons, texture choices) are kept.
    onInstance({"saveServer", "removeServer"}, [](MinecraftInstance* inst, const QString& method, const QVariantMap& args) -> QVariantMap {
        if (inst->isRunning()) return fail("Close the game first: Minecraft rewrites its server list when it exits");
        bool unreadable = false;
        auto root = loadServersDat(inst, &unreadable);
        if (unreadable) return fail("This instance's server list can't be read, so it was left unchanged");
        if (!root) root = std::make_unique<nbt::tag_compound>();
        if (!root->has_key("servers", nbt::tag_type::List) || root->at("servers").as<nbt::tag_list>().size() == 0)
            root->put("servers", nbt::tag_list(nbt::tag_type::Compound));
        auto& list = root->at("servers").as<nbt::tag_list>();
        const int index = args.value("index", -1).toInt();
        if (index >= 0 && (index >= int(list.size()) || serverText(list[index].as<nbt::tag_compound>(), "ip") != args.value("expected").toString()))
            return fail("The server list changed; open it again");
        if (method == "removeServer") {
            if (index < 0) return fail("Choose a server");
            nbt::tag_list kept(nbt::tag_type::Compound);
            for (size_t i = 0; i < list.size(); ++i)
                if (int(i) != index) kept.push_back(std::move(list[i]));
            root->put("servers", std::move(kept));
        } else {
            const auto name = args.value("name").toString().trimmed();
            const auto address = args.value("address").toString().trimmed();
            if (name.isEmpty() || name.size() > 64) return fail("Enter a server name");
            static const QRegularExpression whitespace("\\s");
            if (address.isEmpty() || address.size() > 255 || address.contains(whitespace)) return fail("Enter a server address, like play.example.net");
            if (index < 0) {
                nbt::tag_compound server;
                server.put("name", name.toUtf8().toStdString());
                server.put("ip", address.toUtf8().toStdString());
                list.push_back(std::move(server));
            } else {
                auto& server = list[index].as<nbt::tag_compound>();
                if (serverText(server, "ip") != address) server.erase("icon");  // the old server's icon no longer applies
                server.put("name", name.toUtf8().toStdString());
                server.put("ip", address.toUtf8().toStdString());
            }
        }
        if (!saveServersDat(inst, *root)) return fail("The server list could not be saved");
        return ok(readServers(inst));
    });

    // Screenshots, newest first, as gallery thumbnails; "screenshot" returns one at screen size. Read-only.
    onInstance({"screenshots", "screenshot"}, [](MinecraftInstance* inst, const QString& method, const QVariantMap& args) -> QVariantMap {
        const QDir dir(QDir(inst->gameRoot()).filePath("screenshots"));
        if (method == "screenshot") {
            const auto name = args.value("name").toString();
            if (name.isEmpty() || QFileInfo(name).fileName() != name || !name.endsWith(".png", Qt::CaseInsensitive)) return fail("Choose a screenshot");
            QImage image(dir.filePath(name));
            if (image.isNull()) return fail("Screenshot not found");
            return ok(QVariantMap{{"name", name}, {"image", imageDataUrl(image.scaled(1920, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation), "JPG")}});
        }
        constexpr int pageSize = 24;
        const int offset = qBound(0, args.value("offset").toInt(), 100000);
        const auto files = dir.entryInfoList({"*.png", "*.PNG"}, QDir::Files, QDir::Time);
        QVariantList shots;
        for (int i = offset; i < files.size() && i < offset + pageSize; ++i) {
            QImageReader reader(files[i].absoluteFilePath());
            reader.setScaledSize(reader.size().scaled(320, 200, Qt::KeepAspectRatio));
            shots.append(QVariantMap{{"name", files[i].fileName()}, {"taken", files[i].lastModified().toMSecsSinceEpoch()},
                {"thumb", imageDataUrl(reader.read(), "JPG")}});
        }
        return ok(QVariantMap{{"total", files.size()}, {"offset", offset}, {"shots", shots}});
    });

    // Stop asks the game to exit so Minecraft saves the world (SIGTERM to its process group); force ends it at once.
    // Either way the exit is reported as closed, not as a crash.
    onInstance({"stop"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        if (!inst->isRunning()) return fail("No running game to stop");
        const auto id = inst->id();
        const bool force = args.value("force").toBool();
        m_stopRequested.insert(id);
        QTimer::singleShot(0, this, [id, force] {
            auto* current = APPLICATION->instances()->getInstanceById(id);
            if (!current) return;
            if (auto* task = current->getLaunchTask(); task && task->pid() > 0) {
                task->stopGame(force);
                return;
            }
            s_stopConfirmed = true; // the renderer's "Stop game?" card was the confirmation
            APPLICATION->kill(current);
            s_stopConfirmed = false;
        });
        return ok();
    });

    onInstance({"instanceDialog"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        static const QSet<QString> pages{"version", "mods", "resourcepacks", "texturepacks", "shaderpacks", "settings", "console", "worlds", "servers", "screenshots", "otherlogs", "notes", "managed_pack"};
        const auto page = args.value("page").toString();
        if (!pages.contains(page)) return fail("Unknown instance page");
        APPLICATION->showInstanceWindow(inst, page);
        return ok();
    });

    onInstance({"saveInstance"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        if (m_task && !m_task->isFinished()) return fail("Wait for the current operation to finish");
        if (APPLICATION->instanceLaunchActive(inst->id())) return fail("Stop the game before changing its configuration");
        const auto name = args.value("name").toString().trimmed();
        bool minValid = false, maxValid = false;
        const int min = args.value("minMemory").toInt(&minValid), max = args.value("maxMemory").toInt(&maxValid);
        if (name.isEmpty() || name.size() > 120 || !minValid || !maxValid || min < 256 || max < min || max > 65536)
            return fail("Enter a name and valid memory limits (256–65536 MiB; minimum no greater than maximum)");
        inst->setName(name);
        inst->settings()->set("OverrideMemory", true);
        inst->settings()->set("MinMemAlloc", min);
        inst->settings()->set("MaxMemAlloc", max);
        emit changed();
        return ok();
    });
}
