// SPDX-License-Identifier: GPL-3.0-only
// Shared by ConsoleBridge and its per-area request handlers (Console*Handlers.cpp).
#pragma once
#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include "Application.h"
#include "InstanceCopyTask.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "LaunchController.h"
#include "ResourceDownloadTask.h"
#include "SteamShortcutTask.h"
#include "archive/ExportToZipTask.h"
#include "icons/IconList.h"
#include "java/JavaInstallList.h"
#include "launch/LaunchTask.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "minecraft/WorldList.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/skins/SkinModel.h"
#include "minecraft/skins/SkinRequests.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "settings/SettingsObject.h"
#include "ui/MainWindow.h"

namespace consolebridge {
inline QVariantMap ok(const QVariant& data = {}) { return {{"ok", true}, {"data", data}}; }
inline QVariantMap fail(const QString& error) { return {{"ok", false}, {"error", error}}; }

// Images reach the renderer as data URLs; it may not read local files or the network.
inline QString imageDataUrl(const QImage& image, const char* format = "PNG") {
    if (image.isNull()) return {};
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format, qstrcmp(format, "JPG") == 0 ? 82 : -1);
    return QString("data:image/%1;base64,").arg(qstrcmp(format, "JPG") == 0 ? "jpeg" : "png") + QString::fromLatin1(bytes.toBase64());
}

// The Steam tile created for an instance by "Add to Steam" (0 when none), kept in the instance's settings.
inline qint64 steamAppId(BaseInstance* inst) {
    auto settings = inst->settings();
    if (!settings->contains("ConsoleSteamAppId")) settings->registerSetting("ConsoleSteamAppId", 0);
    return settings->get("ConsoleSteamAppId").toLongLong();
}

// Seconds from Play until the game's window appeared on the last launch (0 if never recorded).
inline qint64 typicalLaunchSeconds(BaseInstance* inst) {
    auto settings = inst->settings();
    if (!settings->contains("ConsoleLaunchSeconds")) settings->registerSetting("ConsoleLaunchSeconds", 0);
    return settings->get("ConsoleLaunchSeconds").toLongLong();
}

inline ResourceFolderModel* resources(MinecraftInstance* inst, const QString& kind) {
    if (kind == "mods") return inst->loaderModList();
    if (kind == "resourcepacks") return inst->resourcePackList();
    if (kind == "shaderpacks") return inst->shaderPackList();
    return nullptr;
}
}  // namespace consolebridge
