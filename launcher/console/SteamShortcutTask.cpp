// SPDX-License-Identifier: GPL-3.0-only
#include "SteamShortcutTask.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPainter>
#include <QWebSocket>
#include "Application.h"
#include "BaseInstance.h"
#include "icons/IconList.h"

namespace {
const QUrl kDebugTargets("http://127.0.0.1:8080/json");
// Steam's custom artwork slots for SetCustomArtworkForApp.
enum ArtworkType { Capsule = 0, Hero = 1, Logo = 2, WideCapsule = 3 };

QString base64Png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

QColor averageColor(const QImage& icon)
{
    const auto pixel = icon.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
    return pixel.alpha() ? pixel : QColor("#3b8526");
}

// Dark Ore surface with a soft glow in the icon's colour, the icon as crisp pixel art, and optionally the name.
QImage artwork(const QSize& size, const QImage& icon, const QString& name, double iconScale, bool withName)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#1c1d1d"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor glow = averageColor(icon);
    glow.setAlpha(150);
    QRadialGradient gradient(QPointF(size.width() / 2.0, size.height() * 0.42), qMax(size.width(), size.height()) * 0.7);
    gradient.setColorAt(0, glow);
    gradient.setColorAt(1, QColor(0x1c, 0x1d, 0x1d, 0));
    painter.fillRect(image.rect(), gradient);

    const int side = int(qMin(size.width(), size.height()) * iconScale);
    const QRect iconRect((size.width() - side) / 2, withName ? int(size.height() * 0.42) - side / 2 : (size.height() - side) / 2, side, side);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(iconRect, icon);

    if (withName) {
        QFont font(QStringLiteral("Noto Sans"));
        font.setBold(true);
        font.setPixelSize(qMax(24, size.width() / 12));
        painter.setFont(font);
        painter.setPen(Qt::white);
        const QRect textRect(size.width() / 12, iconRect.bottom() + size.height() / 20, size.width() * 10 / 12, size.height() / 4);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, name);
    }
    return image;
}

QImage logo(const QString& name)
{
    QImage image(QSize(1280, 360), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font(QStringLiteral("Noto Sans"));
    font.setBold(true);
    font.setPixelSize(132);
    painter.setFont(font);
    painter.setPen(QColor(0, 0, 0, 170));
    painter.drawText(image.rect().translated(0, 8), Qt::AlignCenter | Qt::TextWordWrap, name);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter | Qt::TextWordWrap, name);
    return image;
}
}  // namespace

SteamShortcutTask::SteamShortcutTask(BaseInstance* instance, qint64 existingAppId)
    : m_instanceId(instance->id()), m_name(instance->name()), m_existingAppId(existingAppId)
{
    m_icon = APPLICATION->icons()->getIcon(instance->iconKey()).pixmap(256, 256).toImage();
}

void SteamShortcutTask::fail(const QString& reason)
{
    if (m_socket)
        m_socket->abort();
    if (isRunning())
        emitFailed(reason);
}

void SteamShortcutTask::executeTask()
{
    setStatus(tr("Connecting to Steam…"));
    auto* manager = new QNetworkAccessManager(this);
    manager->setProxy(QNetworkProxy::NoProxy);  // the endpoint is local; never route it through a user proxy
    auto* reply = manager->get(QNetworkRequest(kDebugTargets));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("Steam's local debug interface is not enabled, so Prism cannot add tiles while Steam is running."));
            return;
        }
        for (const auto& value : QJsonDocument::fromJson(reply->readAll()).array()) {
            const auto target = value.toObject();
            if (target.value("title").toString() == QLatin1String("SharedJSContext")) {
                evaluate(target.value("webSocketDebuggerUrl").toString());
                return;
            }
        }
        fail(tr("Steam is still starting. Try again in a moment."));
    });
}

QString SteamShortcutTask::script() const
{
    // Steam runs on the host, so the tile starts this Flatpak by ID; outside Flatpak it runs this binary.
    const auto flatpak = qEnvironmentVariable("FLATPAK_ID");
    const auto exe = flatpak.isEmpty() ? QCoreApplication::applicationFilePath() : QStringLiteral("/usr/bin/flatpak");
    const auto launch = QStringLiteral("--launch \"%1\"").arg(m_instanceId);
    const auto options = flatpak.isEmpty() ? launch : QStringLiteral("run %1 %2").arg(flatpak, launch);

    const QDir iconDir(APPLICATION->dataRoot() + "/steam");
    QDir().mkpath(iconDir.path());
    const auto iconPath = iconDir.absoluteFilePath(m_instanceId + "-icon.png");
    m_icon.scaled(256, 256, Qt::KeepAspectRatio, Qt::FastTransformation).save(iconPath, "PNG");

    const QJsonArray art{
        QJsonArray{ Capsule, base64Png(artwork(QSize(600, 900), m_icon, m_name, 0.6, true)) },
        QJsonArray{ WideCapsule, base64Png(artwork(QSize(920, 430), m_icon, m_name, 0.55, false)) },
        QJsonArray{ Hero, base64Png(artwork(QSize(1920, 620), m_icon, m_name, 0.35, false)) },
        QJsonArray{ Logo, base64Png(logo(m_name)) },
    };
    const QJsonObject params{ { "existing", m_existingAppId },
                              { "name", m_name },
                              { "exe", "\"" + exe + "\"" },
                              { "startDir", "\"" + QDir::homePath() + "\"" },
                              { "options", options },
                              { "icon", iconPath },
                              { "art", art } };
    // Reuse the tile recorded for this instance if it still exists, otherwise create one; then make every
    // field match so a renamed instance or moved install updates in place.
    return QStringLiteral(R"((async () => {
  const p = %1;
  const exists = p.existing && window.appStore?.GetAppOverviewByAppID?.(p.existing);
  const appId = exists ? p.existing : await SteamClient.Apps.AddShortcut(p.name, p.exe, p.startDir, p.options);
  SteamClient.Apps.SetShortcutName(appId, p.name);
  SteamClient.Apps.SetShortcutExe(appId, p.exe);
  SteamClient.Apps.SetShortcutStartDir(appId, p.startDir);
  SteamClient.Apps.SetShortcutLaunchOptions(appId, p.options);
  SteamClient.Apps.SetShortcutIcon(appId, p.icon);
  for (const [type, data] of p.art) await SteamClient.Apps.SetCustomArtworkForApp(appId, data, 'png', type);
  return appId;
})())")
        .arg(QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact)));
}

void SteamShortcutTask::evaluate(const QString& debuggerUrl)
{
    setStatus(tr("Adding %1 to Steam…").arg(m_name));
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_socket->setProxy(QNetworkProxy::NoProxy);
    connect(m_socket, &QWebSocket::connected, this, [this] {
        const QJsonObject message{ { "id", 1 },
                                   { "method", "Runtime.evaluate" },
                                   { "params", QJsonObject{ { "expression", script() }, { "awaitPromise", true }, { "returnByValue", true } } } };
        m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString& text) {
        const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
        if (message.value("id").toInt() != 1)
            return;
        m_socket->close();
        const auto result = message.value("result").toObject();
        if (message.contains("error") || result.contains("exceptionDetails")) {
            const auto details = result.value("exceptionDetails").toObject();
            const auto reason = details.value("exception").toObject().value("description").toString();
            fail(tr("Steam rejected the shortcut: %1").arg(reason.isEmpty() ? message.value("error").toObject().value("message").toString() : reason));
            return;
        }
        m_appId = result.value("result").toObject().value("value").toVariant().toLongLong();
        if (m_appId)
            emitSucceeded();
        else
            fail(tr("Steam did not return the new tile."));
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this] { fail(tr("Lost the connection to Steam: %1").arg(m_socket->errorString())); });
    m_socket->open(QUrl(debuggerUrl));
}
