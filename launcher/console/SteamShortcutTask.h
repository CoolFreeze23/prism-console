// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QImage>
#include <QJsonArray>
#include "tasks/Task.h"
class BaseInstance;
class QWebSocket;

// Adds, or refreshes, a Steam library tile that launches one instance directly. It talks to the running
// Steam client through its local CEF debugging endpoint (127.0.0.1:8080, the interface Decky Loader uses),
// so the tile appears without restarting Steam. Artwork is rendered from the instance icon and name.
class SteamShortcutTask final : public Task {
    Q_OBJECT
   public:
    SteamShortcutTask(BaseInstance* instance, qint64 existingAppId);
    qint64 appId() const { return m_appId; }

   protected:
    void executeTask() override;

   private:
    void evaluate(const QString& debuggerUrl);
    QString script() const;
    void fail(const QString& reason);

    QString m_instanceId, m_name;
    QImage m_icon;
    qint64 m_existingAppId = 0, m_appId = 0;
    QWebSocket* m_socket = nullptr;
};
