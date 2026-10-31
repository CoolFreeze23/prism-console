// SPDX-License-Identifier: GPL-3.0-only
// Content requests from the console renderer. Registered by ConsoleBridge; bodies run on the GUI thread.
#include "ConsoleBridge.h"
#include "ConsoleBridgeSupport.h"

using namespace consolebridge;

void ConsoleBridge::registerContentHandlers()
{
    onInstance({"installLocalResource"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap& args) -> QVariantMap {
        if (APPLICATION->instanceLaunchActive(inst->id()) || (m_task && !m_task->isFinished())) return fail("Stop the game and finish current tasks first");
        auto* model = resources(inst, args.value("kind").toString());
        if (!model) return fail("Unknown resource type");
        const QFileInfo file(args.value("path").toString());
        if (!file.isAbsolute() || !file.isFile() || !file.isReadable() || !QStringList{"jar", "zip", "litemod"}.contains(file.suffix().toLower())) return fail("Choose a readable resource archive");
        const auto destinationName = QFileInfo(file.canonicalFilePath()).fileName();
        if (QFileInfo::exists(model->dir().filePath(destinationName)) || QFileInfo::exists(model->dir().filePath(destinationName + ".disabled"))) return fail("A resource with this filename already exists. Disable or manage the existing copy before replacing it.");
        if (!model->installResource(file.canonicalFilePath())) return fail("Prism could not install this resource");
        model->update(); emit changed(); return ok();
    });

    onInstance({"catalogResults"}, [this](MinecraftInstance* inst, const QString&, const QVariantMap&) -> QVariantMap {
        if (inst->id() != m_catalogInstance) return ok(QVariantMap{{"projects", QVariantList{}}, {"versions", QVariantList{}}});
        QVariantList projects, versions, plan;
        for (const auto& pack : m_catalog) projects.append(QVariantMap{{"id", pack->addonId.toString()}, {"name", pack->name}});
        for (const auto& v : m_catalogVersions) versions.append(QVariantMap{{"id", v.fileId.toString()}, {"name", v.version}, {"file", v.fileName}});
        for (const auto& p : m_downloadPlan) plan.append(QVariantMap{{"name", p->pack->name}, {"file", p->version.fileName}});
        return ok(QVariantMap{{"projects", projects}, {"versions", versions}, {"project", m_catalogProject}, {"plan", plan}});
    });

    onInstance({"searchCatalog", "catalogVersions", "installCatalogResource", "confirmCatalogInstall"}, [this](MinecraftInstance* inst, const QString& method, const QVariantMap& args) -> QVariantMap {
        if (m_task && !m_task->isFinished()) return fail("Wait for the current operation to finish");
        if (APPLICATION->instanceLaunchActive(inst->id())) return fail("Stop the game before changing resources");
        const auto kind = args.value("kind").toString();
        auto* model = resources(inst, kind);
        if (!model) return fail("Unknown resource type");
        const auto type = kind == "mods" ? ModPlatform::ResourceType::Mod : kind == "resourcepacks" ? ModPlatform::ResourceType::ResourcePack : ModPlatform::ResourceType::ShaderPack;
        auto* profile = inst->getPackProfile();
        if (!profile->getComponent("net.minecraft")) inst->getStatusbarDescription();
        const auto mc = profile->getComponent("net.minecraft");
        if (!mc) return fail("This instance has no Minecraft version");
        const auto loaders = profile->getModLoaders();
        if (kind == "mods" && (!loaders || !*loaders)) return fail("Install a mod loader first");
        const auto& api = ModrinthAPI::get();
        if (method == "searchCatalog") {
            ResourceAPI::SearchArgs search;
            search.type = type; search.search = args.value("query").toString().left(200);
            search.versions = std::vector<Version>{Version(mc->m_version)};
            if (kind == "mods") search.loaders = loaders;
            search.offset = qBound(0, args.value("offset").toInt(), 10000);
            auto [task, results] = api.searchProjectsTask(search);
            m_catalog.clear(); m_catalogVersions.clear(); m_catalogProject.clear(); m_downloadPlan.clear();
            m_catalogInstance = inst->id(); m_catalogKind = kind;
            connect(task.get(), &Task::succeeded, this, [this, results] {
                for (const auto& p : *results) m_catalog[p.addonId.toString()] = std::make_shared<ModPlatform::IndexedPack>(p);
                emit changed();
            });
            return startTask(task, "Search Modrinth");
        }
        const auto project = args.value("project").toString();
        if (m_catalogInstance != inst->id() || m_catalogKind != kind || !m_catalog.contains(project)) return fail("Search again before selecting this project");
        const auto pack = m_catalog.value(project);
        if (method == "confirmCatalogInstall") {
            if (project != m_catalogProject || m_downloadPlan.isEmpty()) return fail("Review a release and its dependencies first");
            for (const auto& p : m_downloadPlan)
                if (!p->pack || p->version.fileName.isEmpty() || QFileInfo(p->version.fileName).fileName() != p->version.fileName || p->version.downloadUrl.isEmpty())
                    return fail("Dependency resolution was incomplete; no files were installed");
            auto tasks = makeShared<SequentialTask>();
            for (const auto& p : m_downloadPlan) tasks->addTask(Task::Ptr(new ResourceDownloadTask(p->pack, p->version, model)));
            m_downloadPlan.clear();
            return startTask(tasks, "Install resources");
        }
        if (method == "catalogVersions") {
            ResourceAPI::VersionSearchArgs search;
            search.pack = pack; search.resourceType = type;
            search.mcVersions = std::vector<Version>{Version(mc->m_version)};
            if (kind == "mods") search.loaders = loaders;
            m_catalogVersions.clear(); m_catalogProject = project; m_downloadPlan.clear();
            ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks;
            callbacks.onSucceed = [this](QVector<ModPlatform::IndexedVersion>& versions) { m_catalogVersions = versions; emit changed(); };
            callbacks.onFail = [](const QString&, int) {};
            callbacks.onAbort = [] {};
            return startTask(api.getProjectVersions(search, callbacks), "Load compatible releases");
        }
        if (project != m_catalogProject) return fail("Choose a release for this project first");
        for (const auto& v : m_catalogVersions) if (v.fileId.toString() == args.value("version").toString()) {
            if (QFileInfo(v.fileName).fileName() != v.fileName || v.fileName.isEmpty()) return fail("Invalid resource filename");
            auto selected = std::make_shared<GetModDependenciesTask::PackDependency>(pack, v);
            m_downloadPlan.clear();
            if (kind != "mods") { m_downloadPlan.append(selected); emit changed(); return ok(); }
            auto task = makeShared<GetModDependenciesTask>(inst, inst->loaderModList(), QList<std::shared_ptr<GetModDependenciesTask::PackDependency>>{selected});
            auto* taskPtr = task.get();
            connect(taskPtr, &Task::succeeded, this, [this, selected, taskPtr] {
                m_downloadPlan = {selected};
                m_downloadPlan.append(taskPtr->getDependecies());
                emit changed();
            });
            return startTask(task, "Resolve required dependencies");
        }
        return fail("Release not found");
    });

    onInstance({"resources", "setResourceEnabled"}, [this](MinecraftInstance* inst, const QString& method, const QVariantMap& args) -> QVariantMap {
        auto* model = resources(inst, args.value("kind").toString());
        if (!model) return fail("Unknown resource type");
        if (method == "resources") {
            model->update();
            QVariantList result;
            for (int i = 0; i < model->rowCount(); ++i) {
                const auto& resource = model->at(i);
                result.append(QVariantMap{{"id", resource.internalId()}, {"name", resource.name()},
                                         {"version", resource.version()}, {"enabled", resource.enabled()}});
            }
            return ok(result);
        }
        if (APPLICATION->instanceLaunchActive(inst->id())) return fail("Stop the game before changing resources");
        if (m_task && !m_task->isFinished()) return fail("Wait for the current operation to finish");
        if (args.value("enabled").metaType().id() != QMetaType::Bool) return fail("enabled must be a boolean");
        for (int i = 0; i < model->rowCount(); ++i) {
            if (model->at(i).internalId() == args.value("resourceId").toString()) {
                const bool enabled = args.value("enabled").toBool();
                if (model->at(i).enabled() == enabled) return ok();
                const auto state = enabled ? EnableAction::ENABLE : EnableAction::DISABLE;
                if (!model->setResourceEnabled({model->index(i, ResourceFolderModel::ActiveColumn)}, state))
                    return fail("Prism could not change this resource");
                emit changed();
                return ok();
            }
        }
        return fail("Resource no longer exists; refresh the list");
    });
}
