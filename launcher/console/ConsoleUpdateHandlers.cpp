// SPDX-License-Identifier: GPL-3.0-only
// Mod updates for the console renderer, following Prism's ResourceUpdateDialog: mods without metadata are
// first identified on Modrinth by hash, then every identified mod is checked for a newer compatible release.
// Only Modrinth is used, because CurseForge needs an API key this build does not carry.
#include "ConsoleBridge.h"
#include "ConsoleBridgeSupport.h"
#include "modplatform/EnsureMetadataTask.h"
#include "modplatform/modrinth/ModrinthCheckUpdate.h"
#include "tasks/SequentialTask.h"

using namespace consolebridge;

namespace {
// Release-note excerpt for the controller UI: plain text, first lines only.
QString excerpt(const QString& changelog)
{
    auto text = changelog;
    text.remove(QRegularExpression("<[^>]*>"));
    text = text.trimmed();
    return text.size() > 600 ? text.left(600) + "…" : text;
}
}  // namespace

void ConsoleBridge::checkModUpdates(MinecraftInstance* inst)
{
    auto* model = inst->loaderModList();
    m_updateInstance = inst->id();
    m_modUpdates = { { "instance", m_updateInstance }, { "phase", "checking" }, { "managed", inst->isManagedPack() } };
    emit changed();
    // Prism scans the mods folder asynchronously, so a list read straight away can be empty or stale (a freshly
    // installed pack shows no mods yet). Collect once the scan of the current files has finished; the fallback
    // keeps the page from waiting forever if no scan could start.
    auto done = std::make_shared<bool>(false);
    auto collect = [this, done, id = inst->id()] {
        if (*done) return;
        *done = true;
        if (auto* target = APPLICATION->instances()->getInstanceById(id); target && m_updateInstance == id) collectUpdateCandidates(target);
    };
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(model, &ResourceFolderModel::updateFinished, this, [this, connection, collect] {
        QObject::disconnect(*connection);
        // updateFinished is emitted before the model's queued onUpdateSucceeded applies the scan results;
        // one event-loop turn later the list is current.
        QTimer::singleShot(0, this, collect);
    });
    QTimer::singleShot(30000, this, collect);
    model->update();
}

void ConsoleBridge::collectUpdateCandidates(MinecraftInstance* inst)
{
    auto* model = inst->loaderModList();
    m_updateCandidates.clear();
    m_updateNoMetadata.clear();
    m_updateMcVersions = { inst->getPackProfile()->getComponent("net.minecraft")->getVersion() };
    m_updateLoaders = inst->getPackProfile()->getModLoadersList();
    const auto all = model->allResources();
    // Mods a modpack installed are pinned to it (lockUpdate) and update with the pack, as in Prism's own dialog.
    int folders = 0, locked = 0;
    for (auto* resource : all) {
        if (resource->type() == ResourceType::FOLDER) {
            ++folders;
            continue;
        }
        if (resource->lockUpdate()) {
            ++locked;
            continue;
        }
        if (!resource->metadata())
            m_updateNoMetadata.append(resource);  // identified by hash first
        else if (resource->metadata()->provider == ModPlatform::ResourceProvider::MODRINTH)
            m_updateCandidates.append(resource);
    }
    m_modUpdates["total"] = m_updateCandidates.size() + m_updateNoMetadata.size();
    m_modUpdates["scan"] = QVariantMap{ { "files", all.size() }, { "folders", folders }, { "pinned", locked }, { "unidentified", m_updateNoMetadata.size() },
                                        { "modrinth", m_updateCandidates.size() } };
    if (m_updateNoMetadata.isEmpty()) {
        runUpdateCheck(model);
        return;
    }
    auto identify = makeShared<EnsureMetadataTask>(m_updateNoMetadata, model->indexDir(), ModPlatform::ResourceProvider::MODRINTH);
    connect(identify.get(), &EnsureMetadataTask::metadataReady, this, [this](Resource* resource) {
        if (resource->metadata() && !resource->lockUpdate())
            m_updateCandidates.append(resource);
    });
    // The second phase replaces m_task, so it starts after this task has finished emitting.
    connect(identify.get(), &Task::finished, this, [this, model] { QTimer::singleShot(0, this, [this, model] { runUpdateCheck(model); }); });
    // Another task may have started while the mod list was being scanned; say so instead of waiting forever.
    if (const auto reply = startTask(identify, "Identify mods on Modrinth"); !reply.value("ok").toBool()) {
        m_modUpdates["phase"] = "failed";
        m_modUpdates["error"] = reply.value("error");
        emit changed();
    }
}

void ConsoleBridge::runUpdateCheck(ResourceFolderModel* model)
{
    if (m_updateCandidates.isEmpty()) {
        m_modUpdates["phase"] = "ready";
        m_modUpdates["updates"] = QVariantList{};
        m_modUpdates["checked"] = 0;
        emit changed();
        return;
    }
    auto check = makeShared<ModrinthCheckUpdate>(m_updateCandidates, m_updateMcVersions, m_updateLoaders, model);
    auto* checkPtr = check.get();
    connect(check.get(), &Task::succeeded, this, [this, checkPtr] {
        m_updateList = checkPtr->getUpdates();
        QVariantList updates;
        for (const auto& update : m_updateList)
            updates.append(QVariantMap{ { "name", update.name },
                                        { "from", update.oldVersion },
                                        { "to", update.newVersion },
                                        { "beta", update.newVersionType && *update.newVersionType != ModPlatform::IndexedVersionType(ModPlatform::IndexedVersionTypeValue::Release) },
                                        { "changelog", excerpt(update.changelog) } });
        m_modUpdates["phase"] = "ready";
        m_modUpdates["updates"] = updates;
        // Mods Modrinth could not identify (custom builds, CurseForge-only mods) cannot be checked.
        m_modUpdates["checked"] = m_updateCandidates.size();
        emit changed();
    });
    connect(check.get(), &Task::failed, this, [this](const QString& reason) {
        m_modUpdates["phase"] = "failed";
        m_modUpdates["error"] = reason;
        emit changed();
    });
    // Another task may have started while the mod list was being scanned; say so instead of waiting forever.
    if (const auto reply = startTask(check, "Check for mod updates"); !reply.value("ok").toBool()) {
        m_modUpdates["phase"] = "failed";
        m_modUpdates["error"] = reply.value("error");
        emit changed();
    }
}

void ConsoleBridge::registerUpdateHandlers()
{
    on({ "modUpdates" }, [this](const QString&, const QVariantMap&) -> QVariantMap { return ok(m_modUpdates); });

    onInstance({ "checkModUpdates" }, [this](MinecraftInstance* inst, const QString&, const QVariantMap&) -> QVariantMap {
        if (m_task && !m_task->isFinished())
            return fail("Wait for the current task to finish");
        if (APPLICATION->instanceLaunchActive(inst->id()))
            return fail("Stop the game before updating mods");
        if (!inst->getPackProfile()->getComponent("net.minecraft"))
            inst->getStatusbarDescription();
        if (!inst->getPackProfile()->getComponent("net.minecraft"))
            return fail("This instance has no Minecraft version");
        checkModUpdates(inst);
        return ok();
    });

    onInstance({ "applyModUpdates" }, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        if (m_task && !m_task->isFinished())
            return fail("Wait for the current task to finish");
        if (APPLICATION->instanceLaunchActive(inst->id()))
            return fail("Stop the game before updating mods");
        if (inst->id() != m_updateInstance || m_modUpdates.value("phase") != "ready")
            return fail("Check for updates again");
        const auto chosen = args.value("names").toStringList();
        auto updates = makeShared<SequentialTask>(tr("Update mods"));
        int count = 0;
        for (const auto& update : m_updateList)
            if (chosen.contains(update.name) && update.download) {
                updates->addTask(update.download);
                ++count;
            }
        if (!count)
            return fail("Choose at least one update");
        auto* model = inst->loaderModList();
        connect(updates.get(), &Task::finished, this, [this, model] {
            model->update();
            m_modUpdates = {};  // results are stale once files changed
            m_updateList.clear();
            emit changed();
        });
        return startTask(updates, count == 1 ? QString("Update 1 mod") : QString("Update %1 mods").arg(count));
    });
}
