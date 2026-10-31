// SPDX-License-Identifier: GPL-3.0-only
#include "ConsoleBridge.h"
#include "ConsoleBridgeSupport.h"
#include "ConsoleWorkflow.h"
#include "ExternalLinks.h"
#include "GamepadChord.h"
#include "console/ConsoleMode.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QEventLoop>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>

using namespace consolebridge;

bool ConsoleBridge::s_stopConfirmed = false;

namespace {
QPointer<ConsoleBridge> activeConsole;
// Never expose the bridge to network content or arbitrary local pages.
class LocalPage final : public QWebEnginePage {
public:
    explicit LocalPage(QWebEngineProfile* profile, QObject* parent) : QWebEnginePage(profile, parent) {}
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool) override {
        return url.scheme() == "qrc" && url.path().startsWith("/console/");
    }
    QWebEnginePage* createWindow(WebWindowType) override { return nullptr; }
};
class LocalRequests final : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto url = info.requestUrl();
        const bool allowed = (url.scheme() == "qrc" &&
            (url.path().startsWith("/console/") || url.path() == "/qtwebchannel/qwebchannel.js")) || url.scheme() == "data";
        if (!allowed) info.block(true);
    }
};
}

ConsoleBridge::ConsoleBridge(MainWindow* window) : QObject(window), m_window(window) {
    activeConsole = this;
    m_workflow = new ConsoleWorkflow(window, [this] { emit changed(); }, [this](const QUrl& url) { openLink(url, "link"); });
    // Console-only preferences, registered up front so values saved in an earlier session read back at once.
    for (const auto& [key, fallback] : std::initializer_list<std::pair<const char*, QVariant>>{{"ConsoleSoundEnabled", true}, {"ConsoleSoundVolume", 35}})
        if (!APPLICATION->settings()->contains(key)) APPLICATION->settings()->registerSetting(key, fallback);
    registerLibraryHandlers();
    registerAccountHandlers();
    registerSettingsHandlers();
    registerContentHandlers();
    registerModpackHandlers();
    registerUpdateHandlers();
    window->installEventFilter(this);
    if (consoleMode()) {
        auto* chord = new GamepadChord(this);
        connect(chord, &GamepadChord::progress, this, &ConsoleBridge::chordProgress);
        connect(chord, &GamepadChord::triggered, this, [this] { toggleGame(m_window && m_window->isVisible()); });
    }
    on({"backToGame"}, [this](const QString&, const QVariantMap&) -> QVariantMap {
        if (gameOnScreen().isEmpty()) return fail("No game is running");
        toggleGame(true);
        return ok();
    });
    connect(APPLICATION, &Application::consoleLaunchCreated, this, [this](LaunchController* controller) {
        const auto id = controller->id();
        m_launchTasks[id] = controller;
        m_gameStarted.remove(id);
        m_handOffScheduled.remove(id);
        m_stopRequested.remove(id);
        // Stages follow Prism's launch structure rather than its status text: "account" until the launch task
        // exists, "files" while it prepares libraries, assets and Java, "starting" once the game process runs,
        // "running" when its window has the screen. The renderer adds timing and stall messages.
        m_launchStates[id] = QVariantMap{{"status", "Preparing launch"}, {"active", true}, {"stage", "account"},
            {"startedAt", QDateTime::currentMSecsSinceEpoch()}, {"stageAt", QDateTime::currentMSecsSinceEpoch()}};
        auto update = [this, id](const QVariantMap& changes) { updateLaunch(id, changes); };
        connect(controller, &Task::status, this, [update](const QString& status) { update({{"status", status}}); });
        connect(controller, &Task::progress, this, [update](qint64 current, qint64 total) { update({{"current", current}, {"total", total}}); });
        connect(controller, &Task::failed, this, [this, id, update](const QString& reason) {
            // Prism reports a stopped game as failed ("Game crashed."); a stop the user asked for is a normal close.
            if (m_stopRequested.remove(id)) {
                update({{"active", false}, {"status", "Game exited"}, {"stage", "closed"}, {"stopped", true}, {"endedAt", QDateTime::currentMSecsSinceEpoch()}});
                return;
            }
            // After the game had its window this is a crash, not a launch problem; include the log's last lines.
            QStringList tail;
            if (const auto log = m_logs.value(id))
                for (int i = qMax(0, log->rowCount() - 12); i < log->rowCount(); ++i)
                    tail << log->data(log->index(i, 0), Qt::DisplayRole).toString();
            update({{"active", false}, {"status", reason}, {"failed", true}, {"crashed", m_gameStarted.contains(id)}, {"logTail", tail}});
        });
        connect(controller, &Task::succeeded, this, [update] {
            update({{"active", false}, {"status", "Game exited"}, {"stage", "closed"}, {"endedAt", QDateTime::currentMSecsSinceEpoch()}});
        });
        connect(controller, &Task::aborted, this, [update] { update({{"active", false}, {"status", "Launch cancelled"}, {"stage", "cancelled"}}); });
        // Back to the launcher when the game stops, however it stops. Not tied to the controller's finished
        // signal: Application destroys the controller inside that emission, so later receivers never run.
        // Queued so a Steam-tile launch that quits the application on exit never flashes the launcher first.
        connect(controller->instance(), &BaseInstance::runningStatusChanged, controller, [this](bool running) {
            if (running) return;
            QTimer::singleShot(0, this, [this] { if (m_window && !m_window->isVisible()) m_window->showFullScreen(); });
        });
        connect(controller->instance(), &BaseInstance::launchTaskChanged, controller, [this, id](LaunchTask* task) {
            if (!task) return;
            m_logs[id] = task->getLogModel();
            updateLaunch(id, {{"stage", "files"}});
            watchGameWindow(id, task->getLogModel().get());
        });
    });
    connect(APPLICATION->instances(), &InstanceList::instancesChanged, this, &ConsoleBridge::changed);
    connect(APPLICATION->accounts(), &AccountList::listChanged, this, &ConsoleBridge::changed);
    connect(APPLICATION->accounts(), &AccountList::defaultAccountChanged, this, &ConsoleBridge::changed);
    auto* timer = new QTimer(this);
    // Launch/exit and asynchronous resource parsing updates; idle while the game has the screen.
    connect(timer, &QTimer::timeout, this, [this] { if (m_window && m_window->isVisible()) emit changed(); });
    timer->start(2000);
}

// Gaming Mode presents one window per Steam tile. Once the game opens its window the launcher steps aside,
// so gamescope shows the game full screen instead of as a small window over the launcher.
void ConsoleBridge::watchGameWindow(const QString& id, LogModel* log) {
    connect(log, &QAbstractItemModel::rowsInserted, this, [this, id, log](const QModelIndex&, int first, int last) {
        for (int row = first; row <= last; ++row) {
            const auto line = log->data(log->index(row, 0), Qt::DisplayRole).toString();
            // NeoForge's early loading window, or vanilla's window creation.
            if (line.contains("ImmediateWindowProvider") || line.contains("Backend library: LWJGL")) handOffToGame(id);
            // Loaders that log neither: step aside shortly after the process starts.
            else if (line.contains("Minecraft process ID")) {
                updateLaunch(id, {{"stage", "starting"}});
                QTimer::singleShot(10000, this, [this, id] { handOffToGame(id); });
            }
        }
    });
}

void ConsoleBridge::handOffToGame(const QString& id) {
    auto* inst = APPLICATION->instances()->getInstanceById(id);
    if (!inst || !inst->isRunning() || m_gameStarted.contains(id)) return;
    m_gameStarted.insert(id);
    // Remember how long this instance takes to reach its window, so the next launch can say what to expect.
    const auto started = m_launchStates.value(id).toMap().value("startedAt").toLongLong();
    if (started) {
        const auto seconds = (QDateTime::currentMSecsSinceEpoch() - started) / 1000;
        typicalLaunchSeconds(inst);  // registers the setting
        inst->settings()->set("ConsoleLaunchSeconds", qint64(seconds));
    }
    updateLaunch(id, {{"stage", "running"}, {"runningAt", QDateTime::currentMSecsSinceEpoch()}});
    if (m_window && m_window->isVisible()) m_window->hide();
}

// The running instance whose game window has had the screen, if any.
QString ConsoleBridge::gameOnScreen() const {
    for (const auto& id : m_gameStarted)
        if (auto* inst = APPLICATION->instances()->getInstanceById(id); inst && inst->isRunning()) return id;
    return {};
}

void ConsoleBridge::toggleGame(bool toGame) {
    if (!m_window || gameOnScreen().isEmpty()) return;
    if (toGame) {
        m_window->hide();  // gamescope gives the screen back to the game's window
    } else {
        m_window->showFullScreen();
        m_window->raise();
        m_window->activateWindow();
        emit changed();
    }
}

// Opens a link; when nothing could open it, the renderer says so (with the address for a web link).
void ConsoleBridge::openLink(const QUrl& url, const QString& kind) {
    QPointer<ConsoleBridge> self(this);
    openExternal(this, url, [self, url, kind](bool opened) {
        if (opened || !self) return;
        self->m_notice = QVariantMap{{"seq", self->m_notice.value("seq").toInt() + 1}, {"kind", kind}, {"url", url.toString()}};
        emit self->changed();
    });
}

void ConsoleBridge::updateLaunch(const QString& id, const QVariantMap& changes) {
    auto state = m_launchStates.value(id).toMap();
    if (changes.contains("stage") && changes.value("stage") != state.value("stage")) state["stageAt"] = QDateTime::currentMSecsSinceEpoch();
    if (changes.contains("status") || changes.contains("current")) state["changedAt"] = QDateTime::currentMSecsSinceEpoch();
    for (auto it = changes.begin(); it != changes.end(); ++it) state[it.key()] = it.value();
    m_launchStates[id] = state;
    emit changed();
}

void ConsoleBridge::on(std::initializer_list<const char*> methods, Handler handler) {
    for (const auto* method : methods) m_handlers.insert(QString::fromLatin1(method), handler);
}

void ConsoleBridge::onInstance(std::initializer_list<const char*> methods, InstanceHandler handler) {
    for (const auto* method : methods) m_instanceHandlers.insert(QString::fromLatin1(method), handler);
}

bool ConsoleBridge::takeStopConfirmation() {
    return std::exchange(s_stopConfirmed, false);
}

QVariantMap ConsoleBridge::ask(const QString& title, const QString& text, const QStringList& choices, const QString& input, bool textInput) {
    auto bridge = activeConsole;
    if (!bridge || bridge->m_promptLoop) return {};
    QEventLoop loop;
    bridge->m_answer.clear();
    bridge->m_prompt = {{"id", QUuid::createUuid().toString()}, {"title", title}, {"text", text}, {"choices", choices}, {"input", input}, {"textInput", textInput}};
    bridge->m_promptLoop = &loop;
    connect(bridge, &QObject::destroyed, &loop, &QEventLoop::quit);
    emit bridge->changed();
    loop.exec();
    if (!bridge) return {};
    const auto answer = bridge->m_answer;
    bridge->m_promptLoop = nullptr; bridge->m_prompt.clear(); bridge->m_answer.clear();
    emit bridge->changed();
    return answer;
}

bool ConsoleBridge::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_window && event->type() == QEvent::Close && (m_promptLoop || (m_task && !m_task->isFinished()))) {
        static_cast<QCloseEvent*>(event)->ignore();
        return true; // Do not tear down an in-flight staged instance transaction.
    }
    return QObject::eventFilter(watched, event);
}

QVariantMap ConsoleBridge::startTask(Task::Ptr task, const QString& title) {
    if (!task) return fail("Prism could not prepare this operation");
    if (m_task && !m_task->isFinished()) return fail("Wait for the current task to finish");
    m_task = std::move(task);
    m_taskState = {{"active", true}, {"title", title}, {"status", "Starting…"}, {"current", 0}, {"total", 0}};
    connect(m_task.get(), &Task::status, this, [this](const QString& status) { m_taskState["status"] = status; emit changed(); });
    connect(m_task.get(), &Task::progress, this, [this](qint64 current, qint64 total) {
        m_taskState["current"] = current; m_taskState["total"] = total; emit changed();
    });
    connect(m_task.get(), &Task::finished, this, [this] {
        m_taskState["active"] = false;
        m_taskState["status"] = m_task->wasSuccessful() ? "Complete" : m_task->getState() == Task::State::AbortedByUser ? "Cancelled" : m_task->failReason();
        emit changed();
    });
    QMetaObject::invokeMethod(m_task.get(), &Task::start, Qt::QueuedConnection);
    emit changed();
    return ok();
}

QVariantMap ConsoleBridge::request(const QString& method, const QVariantMap& args) {
    if (method == "workflow") return ok(m_workflow->snapshot());
    if (method == "workflowAction") return m_workflow->dispatch(args);
    if (method == "commands") {
        const auto instance = args.value("id").toString();
        if (!instance.isEmpty() && !APPLICATION->instances()->getInstanceById(instance)) return fail("Instance not found");
        return ok(m_workflow->commands(instance));
    }
    if (!m_window) return fail("Launcher window closed");
    if (method == "answerPrompt") {
        if (!m_promptLoop || args.value("promptId") != m_prompt.value("id") || !m_prompt.value("choices").toStringList().contains(args.value("choice").toString())) return fail("This prompt is no longer active");
        const auto input = args.value("input").toString();
        if (input.size() > 1000) return fail("Input is too long");
        m_answer = {{"choice", args.value("choice")}, {"input", input}};
        m_promptLoop->quit(); return ok();
    }
    if (m_promptLoop && method != "snapshot" && method != "keyboard") return fail("Answer the active prompt first");
    if (const auto handler = m_handlers.value(method)) return handler(method, args);
    if (const auto handler = m_instanceHandlers.value(method)) {
        auto* inst = APPLICATION->instances()->getInstanceById(args.value("id").toString());
        return inst ? handler(inst, method, args) : fail("Instance not found");
    }
    return fail("Unknown operation");
}

void installConsoleUI(MainWindow* window) {
    // Preserve MainWindow's existing action/model plumbing while replacing
    // its visible desktop surface. Old widgets remain owned, not dangling.
    if (auto* previous = window->takeCentralWidget()) { previous->setParent(window); previous->hide(); }
    window->menuBar()->hide();
    window->statusBar()->hide();
    for (auto* toolbar : window->findChildren<QToolBar*>()) toolbar->hide();
    auto* view = new QWebEngineView(window);
    auto* profile = new QWebEngineProfile(view); // off-the-record, no shared browser state
    profile->setUrlRequestInterceptor(new LocalRequests(profile));
    auto* page = new LocalPage(profile, view);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    // Interface sounds come from controller input, which Chromium does not count as a user gesture.
    page->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    auto* channel = new QWebChannel(page);
    channel->registerObject("prism", new ConsoleBridge(window));
    page->setWebChannel(channel);
    view->setPage(page);
    window->setCentralWidget(view);
    view->load(QUrl("qrc:/console/index.html"));
}
