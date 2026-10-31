// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <functional>
#include <initializer_list>
#include "minecraft/auth/MinecraftAccount.h"
#include "launch/LogModel.h"
#include "modplatform/ModIndex.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
#include "modplatform/CheckUpdateTask.h"
class QEventLoop;
class MainWindow;
class ConsoleWorkflow;
class MinecraftInstance;
class QJsonDocument;
// The renderer's only entry point into Prism. request() routes each method name to a handler registered
// by one of the per-area files (Console*Handlers.cpp); handlers return {ok, data} or {ok: false, error}.
class ConsoleBridge : public QObject {
    Q_OBJECT
public:
    explicit ConsoleBridge(MainWindow* window);
    Q_INVOKABLE QVariantMap request(const QString& method, const QVariantMap& args);
    bool eventFilter(QObject* watched, QEvent* event) override;
    static QVariantMap ask(const QString& title, const QString& text, const QStringList& choices, const QString& input = {}, bool textInput = false);
    // True once per stop the user already confirmed in the console UI, so Prism does not ask again.
    static bool takeStopConfirmation();
signals:
    void changed();
    // View + Menu held on a gamepad, 0..1, for the on-screen ring (see GamepadChord).
    void chordProgress(double fraction);
private:
    using Handler = std::function<QVariantMap(const QString& method, const QVariantMap& args)>;
    // Instance handlers receive the instance named by args.id; request() rejects unknown IDs first.
    using InstanceHandler = std::function<QVariantMap(MinecraftInstance* inst, const QString& method, const QVariantMap& args)>;
    void on(std::initializer_list<const char*> methods, Handler handler);
    void onInstance(std::initializer_list<const char*> methods, InstanceHandler handler);
    void registerLibraryHandlers();
    void registerAccountHandlers();
    void registerSettingsHandlers();
    void registerContentHandlers();
    void registerModpackHandlers();
    void modrinthGet(const QUrl& url, std::function<void(const QJsonDocument&, const QString& error)> done);
    void fetchModpackIcon(const QString& project, const QUrl& url);
    QVariantMap m_modpacks;                     // browser state: query, hits, versions, loading flags, error
    QHash<QString, QString> m_modpackIcons;     // project id -> PNG data URL ("" while loading)
    QHash<QString, QPair<qint64, QString>> m_instanceIcons;  // icon key -> (QIcon cache key, PNG data URL)
    int m_modpackSearchSerial = 0;
    int m_modpacksRevision = 0;
    void modpacksChanged();
    void registerUpdateHandlers();
    void checkModUpdates(MinecraftInstance* inst);
    void collectUpdateCandidates(MinecraftInstance* inst);
    void runUpdateCheck(ResourceFolderModel* model);
    // Mod update check state. The candidate list and versions are held here because Prism's check task keeps
    // references to them for its whole run.
    QVariantMap m_modUpdates;  // instance, phase (checking/ready/failed), managed, total, checked, updates[], error
    QString m_updateInstance;
    QList<Resource*> m_updateCandidates, m_updateNoMetadata;
    std::vector<Version> m_updateMcVersions;
    QList<ModPlatform::ModLoaderType> m_updateLoaders;
    std::vector<CheckUpdateTask::Update> m_updateList;
    QHash<QString, Handler> m_handlers;
    QHash<QString, InstanceHandler> m_instanceHandlers;
    static bool s_stopConfirmed;

    QPointer<MainWindow> m_window;
    ConsoleWorkflow* m_workflow = nullptr;
    MinecraftAccountPtr m_loginAccount;
    shared_qobject_ptr<AuthFlow> m_loginTask;
    QVariantMap m_loginState{{"active", false}};
    Task::Ptr m_task;
    QVariantMap m_taskState{{"active", false}};
    QVariantMap m_launchStates;
    QMap<QString, QPointer<Task>> m_launchTasks;
    QSet<QString> m_gameStarted;      // launches whose game window now has the screen
    QSet<QString> m_handOffScheduled; // launches with a fallback hand-off pending
    QSet<QString> m_stopRequested;    // games the user chose to stop, so their exit is not a crash
    void watchGameWindow(const QString& id, LogModel* log);
    void handOffToGame(const QString& id);
    void updateLaunch(const QString& id, const QVariantMap& changes);
    // Brings the launcher forward over a running game, or hands the screen back to the game.
    void toggleGame(bool toGame);
    QString gameOnScreen() const;
    QMap<QString, shared_qobject_ptr<LogModel>> m_logs;
    QMap<QString, ModPlatform::IndexedPack::Ptr> m_catalog;
    QVector<ModPlatform::IndexedVersion> m_catalogVersions;
    QString m_catalogInstance, m_catalogKind, m_catalogProject;
    QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> m_downloadPlan;
    QVariantMap m_prompt, m_answer;
    QEventLoop* m_promptLoop = nullptr;
    QVariantMap startTask(Task::Ptr task, const QString& title);
};
void installConsoleUI(MainWindow* window);
