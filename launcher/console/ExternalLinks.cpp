// SPDX-License-Identifier: GPL-3.0-only
#include "ExternalLinks.h"
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>
#include <memory>
#if defined(WITH_QTDBUS)
#include <QDBusConnection>
#include <QDBusMessage>
#endif

namespace {
const QUrl kDebugTargets("http://127.0.0.1:8080/json");

// Runs SteamClient.URL.ExecuteSteamURL in Steam's SharedJSContext; done(false) when Steam's debug endpoint is
// off, Steam is not running, or it throws. Gives up after a few seconds so a stuck endpoint never hangs a button.
void openInSteam(QObject* owner, const QString& steamUrl, std::function<void(bool)> done)
{
    auto finish = std::make_shared<std::function<void(bool)>>(std::move(done));
    auto report = [finish](bool opened) {
        if (*finish) {
            auto callback = std::move(*finish);
            *finish = nullptr;
            callback(opened);
        }
    };
    auto* manager = new QNetworkAccessManager(owner);
    manager->setProxy(QNetworkProxy::NoProxy);  // the endpoint is local; never route it through a user proxy
    QTimer::singleShot(4000, manager, [report, manager] { report(false); manager->deleteLater(); });
    auto* reply = manager->get(QNetworkRequest(kDebugTargets));
    QObject::connect(reply, &QNetworkReply::finished, manager, [reply, manager, steamUrl, report] {
        reply->deleteLater();
        QString debugger;
        if (reply->error() == QNetworkReply::NoError)
            for (const auto& value : QJsonDocument::fromJson(reply->readAll()).array())
                if (value.toObject().value("title").toString() == QLatin1String("SharedJSContext"))
                    debugger = value.toObject().value("webSocketDebuggerUrl").toString();
        if (debugger.isEmpty()) {
            report(false);
            return;
        }
        auto* socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, manager);
        socket->setProxy(QNetworkProxy::NoProxy);
        const auto expression = QStringLiteral("SteamClient.URL.ExecuteSteamURL(%1)")
                                    .arg(QString::fromUtf8(QJsonDocument(QJsonArray{ steamUrl }).toJson(QJsonDocument::Compact)).mid(1).chopped(1));
        QObject::connect(socket, &QWebSocket::connected, socket, [socket, expression] {
            const QJsonObject message{ { "id", 1 }, { "method", "Runtime.evaluate" }, { "params", QJsonObject{ { "expression", expression } } } };
            socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
        });
        QObject::connect(socket, &QWebSocket::textMessageReceived, socket, [socket, report](const QString& text) {
            const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
            if (message.value("id").toInt() != 1)
                return;
            socket->close();
            report(!message.contains("error") && !message.value("result").toObject().contains("exceptionDetails"));
        });
        QObject::connect(socket, &QWebSocket::errorOccurred, socket, [report] { report(false); });
        socket->open(QUrl(debugger));
    });
}

// The desktop's handler. Inside Flatpak that is the OpenURI portal, asked directly: Qt's openUrl falls back to
// xdg-open when the portal refuses, which inside the sandbox asks the same portal and fails silently.
bool openWithDesktop(const QUrl& url)
{
#if defined(WITH_QTDBUS)
    if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
        auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                   "org.freedesktop.portal.OpenURI", "OpenURI");
        call << QString() << url.toString(QUrl::FullyEncoded) << QVariantMap();
        return QDBusConnection::sessionBus().call(call, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage;
    }
#endif
    return QDesktopServices::openUrl(url);
}
}  // namespace

void openExternal(QObject* owner, const QUrl& url, std::function<void(bool opened)> done)
{
    const bool steamUrl = url.scheme() == QLatin1String("steam");
    const bool web = url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http");
    if (!steamUrl && !web) {
        done(openWithDesktop(url));
        return;
    }
    const auto inSteam = steamUrl ? url.toString(QUrl::FullyEncoded) : QStringLiteral("steam://openurl/") + url.toString(QUrl::FullyEncoded);
    const bool gamingMode = qgetenv("XDG_CURRENT_DESKTOP") == "gamescope" || qgetenv("SteamGamepadUI") == "1";
    QPointer<QObject> alive(owner);
    if (steamUrl || gamingMode) {
        openInSteam(owner, inSteam, [alive, url, done](bool opened) {
            if (alive)
                done(opened || openWithDesktop(url));
        });
        return;
    }
    if (openWithDesktop(url)) {
        done(true);
        return;
    }
    openInSteam(owner, inSteam, [alive, done](bool opened) {
        if (alive)
            done(opened);
    });
}
