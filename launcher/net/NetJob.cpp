// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "NetJob.h"
#include <QNetworkReply>
#include <QTimer>
#include "net/Logging.h"
#include "net/Request.h"
#include "tasks/ConcurrentTask.h"

#ifdef LAUNCHER_APPLICATION
#include "Application.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/NetworkJobFailedDialog.h"
#endif

NetJob::NetJob(const QString& jobName, QNetworkAccessManager* network, int maxConcurrent) : ConcurrentTask(jobName), m_network(network)
{
#ifdef LAUNCHER_APPLICATION
    if (APPLICATION_DYN) {
        if (maxConcurrent < 0) {
            maxConcurrent = APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt();
        }
        m_maxTries = APPLICATION->settings()->get("NetworkRetryLimit").toInt();
    }
#endif
    if (maxConcurrent > 0) {
        setMaxConcurrent(maxConcurrent);
    }
}

auto NetJob::addNetAction(const Net::Request::Ptr& action) -> bool
{
    action->setNetwork(m_network);

    addTask(action);

    return true;
}

void NetJob::executeNextSubTask()
{
    // Waiting to retry: other finished downloads must not end the job meanwhile.
    if (!m_retryWaiting.isEmpty()) {
        return;
    }
    // We're finished, check for failures and retry if we can (NetworkRetryLimit times)
    if (isRunning() && m_queue.isEmpty() && m_doing.isEmpty() && !m_failed.isEmpty() && m_try <= m_maxTries) {
        qint64 retryAfter = 0;
        m_failed.removeIf([this, &retryAfter](QHash<Task*, Task::Ptr>::iterator task) {
            // Worth repeating: no answer at all (timed out, refused or dropped connection), or a server saying it is
            // busy or briefly unavailable (429, 502, 503, 504). Any other answer would come back the same.
            const auto* request = static_cast<Net::Request*>(task->get());
            static const QSet<int> transient{ 0, 429, 502, 503, 504 };
            if (!transient.contains(request->replyStatusCode())) {
                return false;
            }
            retryAfter = qMax(retryAfter, request->retryAfter());
            m_done.remove(task->get());
            m_retryWaiting.append(*task);
            return true;
        });
        if (!m_retryWaiting.isEmpty()) {
            // A short pause first: a network that just dropped connections often lets the next ones through, and a
            // server's Retry-After (up to a minute) is honoured.
            const int delayMs = static_cast<int>(qMax<qint64>(2000 * m_try, qMin<qint64>(retryAfter, 60) * 1000));
            for (const auto& task : m_retryWaiting) {
                const auto* request = static_cast<Net::Request*>(task.get());
                qCWarning(taskNetLogC) << "Retrying" << request->url().toString() << "in" << delayMs / 1000 << "s (try" << m_try + 1
                                       << "of" << m_maxTries + 1 << "):" << request->errorString();
            }
            setStatus(tr("Connection problem, trying again in %1 s").arg(delayMs / 1000));
            m_try += 1;
            QTimer::singleShot(delayMs, this, [this] {
                if (m_retryWaiting.isEmpty() || !isRunning()) {
                    return;
                }
                for (const auto& task : std::exchange(m_retryWaiting, {})) {
                    m_restart.insert(task.get());
                    m_queue.enqueue(task);
                }
                ConcurrentTask::executeNextSubTask();
            });
            return;
        }
    }
    ConcurrentTask::executeNextSubTask();
}

auto NetJob::size() const -> int
{
    return m_queue.size() + m_doing.size() + m_done.size();
}

auto NetJob::canAbort() const -> bool
{
    bool canFullyAbort = true;

    // can abort the downloads on the queue?
    for (const auto& part : m_queue) {
        canFullyAbort &= part->canAbort();
    }

    // can abort the active downloads?
    for (const auto& part : m_doing) {
        canFullyAbort &= part->canAbort();
    }

    return canFullyAbort;
}

auto NetJob::abort() -> bool
{
    // fail all downloads on the queue, and those waiting to retry
    for (const auto& task : m_queue) {
        m_failed.insert(task.get(), task);
    }
    m_queue.clear();
    if (!m_retryWaiting.isEmpty()) {
        for (const auto& task : std::exchange(m_retryWaiting, {})) {
            m_failed.insert(task.get(), task);
        }
        if (m_doing.isEmpty()) {
            emitAborted();
            return true;
        }
    }

    if (m_doing.isEmpty()) {
        // no downloads to abort, NetJob is not running
        return true;
    }

    bool fullyAborted = true;

    // abort active downloads
    auto toKill = m_doing.values();
    for (const auto& part : toKill) {
        fullyAborted &= part->abort();
    }

    if (fullyAborted) {
        emitAborted();
    } else {
        emitFailed(tr("Failed to abort all tasks in the NetJob!"));
    }

    return fullyAborted;
}

auto NetJob::getFailedActions() -> QList<Net::Request*>
{
    QList<Net::Request*> failed;
    for (const auto& index : m_failed) {
        failed.push_back(dynamic_cast<Net::Request*>(index.get()));
    }
    return failed;
}

auto NetJob::getFailedFiles() -> QList<QString>
{
    QList<QString> failed;
    for (const auto& index : m_failed) {
        failed.append(static_cast<Net::Request*>(index.get())->url().toString());
    }
    return failed;
}

void NetJob::updateState()
{
    emit progress(m_done.count(), totalSize());
    setStatus(tr("Executing %1 task(s) (%2 out of %3 are done)")
                  .arg(QString::number(m_doing.count()), QString::number(m_done.count()), QString::number(totalSize())));
}

bool NetJob::isOnline()
{
    return std::ranges::any_of(getFailedActions(), [](const auto& job) {
        auto err = job->error();
        // check some errors that are usually associated with the lack of internet
        return err != QNetworkReply::HostNotFoundError && err != QNetworkReply::NetworkSessionFailedError;
    });
}

void NetJob::emitFailed(QString reason)
{
#ifdef LAUNCHER_APPLICATION

    if (APPLICATION_DYN && m_askRetry && isOnline()) {
        auto failed = getFailedActions();
        QWidget* activeWindow = QApplication::activeWindow();
        auto* dialog = new NetworkJobFailedDialog(objectName(), m_try, m_done.size(), failed.size(), activeWindow);
        dialog->setAttribute(Qt::WA_DeleteOnClose);

        for (const auto& request : failed) {
            dialog->addFailedRequest(request->url(), request->errorString());
        }

        dialog->open();

        connect(dialog, &QDialog::finished, this, [this, reason = std::move(reason)](int result) {
            if (result == QDialog::Accepted) {
                m_maxTries++;
                executeNextSubTask();
            } else {
                ConcurrentTask::emitFailed(reason);
            }
        });

        return;
    }
#endif

    ConcurrentTask::emitFailed(reason);
}

void NetJob::setAskRetry(bool askRetry)
{
    m_askRetry = askRetry;
}
