// SPDX-License-Identifier: GPL-3.0-only
// Settings requests from the console renderer. Registered by ConsoleBridge; bodies run on the GUI thread.
#include "ConsoleBridge.h"
#include "ConsoleBridgeSupport.h"
#include "translations/TranslationsModel.h"
#include "BuildConfig.h"
#include <algorithm>
#include <vector>

using namespace consolebridge;

void ConsoleBridge::registerSettingsHandlers()
{
    // Launcher options the console shows natively, by a stable key. Prism settings that are phrased negatively
    // ("dependencies disabled") are presented positively; numbers are range-checked.
    struct Option { const char* key; const char* setting; bool inverted; int min, max; QVariant fallback; };
    static const std::vector<Option> options{
        {"installDependencies", "ModDependenciesDisabled", true, 0, 0, false},
        {"trackMetadata", "ModMetadataDisabled", true, 0, 0, false},
        {"askModpackUpdate", "SkipModpackUpdatePrompt", true, 0, 0, false},
        {"downloadGameFiles", "DownloadGameFilesDuringInstanceCreation", false, 0, 0, true},
        {"recordPlayTime", "RecordGameTime", false, 0, 0, true},
        {"showPlayTime", "ShowGameTime", false, 0, 0, true},
        {"lowMemoryWarning", "LowMemWarning", false, 0, 0, true},
        {"parallelDownloads", "NumberOfConcurrentDownloads", false, 1, 32, 6},
        {"retryLimit", "NetworkRetryLimit", false, 0, 10, 2},
        {"timeoutSeconds", "RequestTimeout", false, 5, 120, 15},
        {"soundEnabled", "ConsoleSoundEnabled", false, 0, 0, true},
        {"soundVolume", "ConsoleSoundVolume", false, 0, 100, 35},
    };
    const auto value = [](const Option& option) {
        auto settings = APPLICATION->settings();
        if (!settings->contains(option.setting)) settings->registerSetting(option.setting, option.fallback);
        const auto raw = settings->get(option.setting);
        return option.max ? QVariant(raw.toInt()) : QVariant(option.inverted ? !raw.toBool() : raw.toBool());
    };
    on({"launcherOptions"}, [value](const QString&, const QVariantMap&) -> QVariantMap {
        QVariantMap values;
        for (const auto& option : options) values[option.key] = value(option);
        values["about"] = QVariantMap{{"name", BuildConfig.LAUNCHER_DISPLAYNAME}, {"version", BuildConfig.printableVersionString()},
            {"commit", BuildConfig.GIT_COMMIT.left(10)}, {"qt", QString::fromLatin1(qVersion())}};
        return ok(values);
    });
    on({"setLauncherOption"}, [this, value](const QString&, const QVariantMap& args) -> QVariantMap {
        const auto key = args.value("key").toString();
        const auto it = std::find_if(options.begin(), options.end(), [&](const Option& option) { return key == option.key; });
        if (it == options.end()) return fail("Unknown setting");
        value(*it);  // registers it if needed
        const auto input = args.value("value");
        if (it->max) {
            bool number = false;
            const int n = input.toInt(&number);
            if (!number || n < it->min || n > it->max) return fail("Value out of range");
            APPLICATION->settings()->set(it->setting, n);
        } else {
            if (input.metaType().id() != QMetaType::Bool) return fail("Choose on or off");
            APPLICATION->settings()->set(it->setting, it->inverted ? !input.toBool() : input.toBool());
        }
        emit changed();
        return ok(value(*it));
    });

    // The console's language follows Prism's Language setting, so Prism's own strings switch with it.
    on({"setLanguage"}, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        const auto language = args.value("language").toString();
        if (language != "en_US" && language != "pt_BR") return fail("Unsupported language");
        // As Prism's Language page: store the choice (the console reads it back), then load it. Prism's own strings
        // need its downloaded translation; the console's are built in, so they switch even offline.
        APPLICATION->settings()->set("Language", language);
        auto* translations = APPLICATION->translations();
        translations->selectLanguage(language);
        translations->updateLanguage(language);
        emit changed();
        return ok();
    });

    on({"detectJava", "javaInstallations"}, [this](const QString& method, const QVariantMap&) -> QVariantMap {
        auto* java = APPLICATION->javalist();
        if (method == "detectJava") return startTask(java->getLoadTask(true), "Detect Java installations");
        QVariantList result;
        for (int i = 0; i < java->count(); ++i) {
            auto entry = std::dynamic_pointer_cast<JavaInstall>(java->at(i));
            if (entry) result.append(QVariantMap{{"path", entry->path}, {"version", entry->id.toString()}, {"architecture", entry->arch}});
        }
        return ok(result);
    });

    on({"globalSettings", "saveGlobalSettings", "instancePreferences", "saveInstancePreferences"}, [this](const QString& method, const QVariantMap& args) -> QVariantMap {
        auto* list = APPLICATION->instances();
        // Explicit schema prevents accidental exposure of proxy credentials,
        // account secrets, command hooks, or arbitrary configuration keys.
        static const QMap<QString, QPair<int, int>> numbers{
            {"MinMemAlloc", {256, 65536}}, {"MaxMemAlloc", {256, 65536}},
            {"MinecraftWinWidth", {320, 7680}}, {"MinecraftWinHeight", {240, 4320}},
            {"NumberOfConcurrentDownloads", {1, 32}}};
        QStringList flags{"AutomaticJavaSwitch", "AutomaticJavaDownload", "LaunchMaximized",
            "EnableFeralGamemode", "EnableMangoHud", "UseDiscreteGpu", "RecordGameTime"};
        static const QStringList textKeys{"JavaPath", "JvmArgs", "PreLoadCommand", "PreLaunchCommand", "WrapperCommand", "PostExitCommand"};
        auto settings = APPLICATION->settings();
        const bool instanceScope = method == "instancePreferences" || method == "saveInstancePreferences";
        const bool save = method.startsWith("save");
        if (instanceScope) {
            auto* target = list->getInstanceById(args.value("id").toString());
            if (!target) return fail("Instance not found");
            if (save && (APPLICATION->instanceLaunchActive(target->id()) || (m_task && !m_task->isFinished()))) return fail("Stop the game and finish current tasks first");
            settings = target->settings();
            flags.append({"OverrideJavaLocation", "OverrideJavaArgs", "OverrideWindow", "OverrideMemory", "OverridePerformance", "OverrideCommands", "OverrideGameTime"});
            flags.removeAll("AutomaticJavaSwitch"); flags.removeAll("AutomaticJavaDownload");
        }
        if (!save) {
            QVariantMap values;
            for (auto it = numbers.cbegin(); it != numbers.cend(); ++it) if (!instanceScope || it.key() != "NumberOfConcurrentDownloads") values[it.key()] = settings->get(it.key()).toInt();
            for (const auto& key : flags) values[key] = settings->get(key).toBool();
            for (const auto& key : textKeys) values[key] = settings->get(key).toString();
            return ok(values);
        }
        const auto values = args.value("values").toMap();
        if (values.isEmpty()) return fail("No settings provided");
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (instanceScope && it.key() == "NumberOfConcurrentDownloads") return fail("Download concurrency is a launcher-wide setting");
            if (numbers.contains(it.key())) {
                bool valid; const int value = it.value().toInt(&valid); const auto range = numbers.value(it.key());
                if (!valid || value < range.first || value > range.second) return fail("Invalid value for " + it.key());
            } else if (textKeys.contains(it.key())) {
                if (it.value().metaType().id() != QMetaType::QString || it.value().toString().size() > 16384 || it.value().toString().contains(QChar::Null)) return fail("Invalid text setting");
                if (it.key() == "JavaPath" && it.value() != settings->get("JavaPath") && !it.value().toString().isEmpty()) {
                    const QFileInfo binary(it.value().toString());
                    if (!binary.isAbsolute() || !binary.isFile() || !binary.isExecutable()) return fail("Java path must be an accessible executable with an absolute path");
                }
            } else if (!flags.contains(it.key()) || it.value().metaType().id() != QMetaType::Bool) return fail("Unknown or invalid setting");
        }
        const int min = values.value("MinMemAlloc", settings->get("MinMemAlloc")).toInt();
        const int max = values.value("MaxMemAlloc", settings->get("MaxMemAlloc")).toInt();
        if (min > max) return fail("Minimum memory cannot exceed maximum memory");
        // Set override gates before their values so observers see the intended
        // effective configuration throughout the save.
        for (auto it = values.cbegin(); it != values.cend(); ++it) if (it.key().startsWith("Override")) settings->set(it.key(), it.value());
        for (auto it = values.cbegin(); it != values.cend(); ++it) if (!it.key().startsWith("Override")) settings->set(it.key(), it.value());
        emit changed(); return ok();
    });

    onInstance({"components", "componentVersions", "loadComponentVersions", "setComponentVersion"}, [this](MinecraftInstance* inst, const QString& method, const QVariantMap& args) -> QVariantMap {
        auto* profile = inst->getPackProfile();
        if (!profile->getComponent("net.minecraft")) inst->getStatusbarDescription();
        if (method == "components") {
            QVariantList result;
            for (int i = 0; i < profile->rowCount(); ++i) {
                auto component = profile->getComponent(size_t(i));
                result.append(QVariantMap{{"id", component->getID()}, {"name", component->getName()},
                    {"version", component->m_version}, {"enabled", component->isEnabled()}});
            }
            return ok(result);
        }
        const auto uid = args.value("component").toString();
        static const QSet<QString> supported{"net.minecraft", "net.neoforged", "net.minecraftforge", "net.fabricmc.fabric-loader", "org.quiltmc.quilt-loader"};
        if (!supported.contains(uid)) return fail("Unsupported component");
        auto versions = APPLICATION->metadataIndex()->get(uid);
        if (method == "loadComponentVersions") return versions->isLoaded() ? ok() : startTask(versions->getLoadTask(), "Load component versions");
        auto mc = profile->getComponent("net.minecraft");
        const auto minecraft = mc ? mc->m_version : QString();
        const auto compatible = [&](const Meta::Version::Ptr& version) {
            for (const auto& req : version->requiredSet())
                if (req.uid == "net.minecraft" && !req.equalsVersion.isEmpty() && req.equalsVersion != minecraft) return false;
            return true;
        };
        if (method == "componentVersions") {
            QVariantList result;
            if (versions->isLoaded()) for (const auto& v : versions->versions()) if (compatible(v)) result.append(v->version());
            return ok(result);
        }
        if (APPLICATION->instanceLaunchActive(inst->id()) || (m_task && !m_task->isFinished())) return fail("Finish the current operation first");
        const auto version = args.value("version").toString();
        if (!versions->isLoaded() || !versions->hasVersion(version) || !compatible(versions->getVersion(version))) return fail("Choose a compatible loaded version");
        // A different loader family is never silently removed.
        if (uid != "net.minecraft") for (const auto& other : supported)
            if (other != uid && other != "net.minecraft" && profile->getComponent(other) && profile->getComponent(other)->isEnabled())
                return fail("This instance already uses a different loader; create a separate instance to switch families");
        if (!profile->setComponentVersion(uid, version)) return fail("Prism could not set this version");
        profile->saveNow();
        emit changed(); return ok();
    });
}
