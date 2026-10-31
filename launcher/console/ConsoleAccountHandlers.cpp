// SPDX-License-Identifier: GPL-3.0-only
// Account requests from the console renderer. Registered by ConsoleBridge; bodies run on the GUI thread.
#include "ConsoleBridge.h"
#include "ConsoleBridgeSupport.h"

using namespace consolebridge;

void ConsoleBridge::registerAccountHandlers()
{
    on({"beginLogin"}, [this](const QString&, const QVariantMap&) -> QVariantMap {
        if (m_loginTask && !m_loginTask->isFinished()) return fail("A sign-in is already in progress");
        if (!APPLICATION->capabilities().testFlag(Application::SupportsMSA)) return fail("This build has no Microsoft application client ID");
        m_loginAccount = MinecraftAccount::createBlankMSA();
        m_loginTask = m_loginAccount->login(true);
        m_loginState = {{"active", true}, {"status", "Requesting a Microsoft sign-in code…"}};
        connect(m_loginTask.get(), &Task::status, this, [this](const QString& status) {
            m_loginState["status"] = status; emit changed();
        });
        connect(m_loginTask.get(), &AuthFlow::authorizeWithBrowserWithExtra, this,
            [this](const QString& url, const QString& code, int expires) {
                m_loginState["url"] = url; m_loginState["code"] = code;
                m_loginState["expiresIn"] = expires; emit changed();
            });
        connect(m_loginTask.get(), &Task::succeeded, this, [this] {
            auto* accounts = APPLICATION->accounts();
            const int existing = accounts->findAccountByProfileId(m_loginAccount->profileId());
            if (existing >= 0) accounts->removeAccount(accounts->index(existing, 0));
            accounts->addAccount(m_loginAccount);
            accounts->setDefaultAccount(m_loginAccount);
            m_loginState = {{"active", false}, {"status", "Signed in"}};
            emit changed();
        });
        connect(m_loginTask.get(), &Task::failed, this, [this](const QString& reason) {
            m_loginState = {{"active", false}, {"status", reason}}; emit changed();
        });
        connect(m_loginTask.get(), &Task::aborted, this, [this] {
            m_loginState = {{"active", false}, {"status", "Sign-in cancelled"}}; emit changed();
        });
        QMetaObject::invokeMethod(m_loginTask.get(), &Task::start, Qt::QueuedConnection);
        return ok();
    });

    on({"cancelLogin"}, [this](const QString&, const QVariantMap&) -> QVariantMap {
        if (m_loginTask && !m_loginTask->isFinished()) m_loginTask->abort();
        return ok();
    });

    on({"selectAccount"}, [this](const QString&, const QVariantMap& args) -> QVariantMap {
        auto* accounts = APPLICATION->accounts();
        const auto index = accounts->findAccountByProfileId(args.value("id").toString());
        if (index < 0) return fail("Account not found");
        accounts->setDefaultAccount(accounts->at(index));
        emit changed();
        return ok();
    });

    on({"accountDetails", "removeAccount", "uploadSkin", "resetSkin", "changeCape"}, [this](const QString& method, const QVariantMap& args) -> QVariantMap {
        auto* list = APPLICATION->instances();
        auto* accounts = APPLICATION->accounts();
        const int index = accounts->findAccountByProfileId(args.value("account").toString());
        if (index < 0) return fail("Account not found");
        const auto account = accounts->at(index);
        const auto& profile = account->accountData()->minecraftProfile;
        if (method == "accountDetails") {
            QVariantList capes;
            for (const auto& cape : profile.capes) capes.append(QVariantMap{{"id", cape.id}, {"name", cape.alias}});
            return ok(QVariantMap{{"name", account->profileName()}, {"capes", capes}, {"cape", profile.currentCape},
                {"skin", profile.skin.data.isEmpty() ? QString() : "data:image/png;base64," + QString::fromLatin1(profile.skin.data.toBase64())}});
        }
        if (m_task && !m_task->isFinished()) return fail("Wait for the current task to finish");
        if (account->isActive() || (m_loginTask && !m_loginTask->isFinished())) return fail("Wait for account authentication to finish");
        for (int i = 0; i < list->count(); ++i) if (APPLICATION->instanceLaunchActive(list->at(i)->id())) return fail("Stop running games before changing accounts");
        if (method == "removeAccount") {
            if (args.value("confirmName").toString() != account->profileName()) return fail("Confirm the account name before removing it");
            accounts->removeAccount(accounts->index(index, 0)); emit changed(); return ok();
        }
        if (account->accountType() != AccountType::MSA || account->accessToken().isEmpty()) return fail("Sign in with Microsoft to manage your skin");
        Net::Request::Ptr request;
        if (method == "uploadSkin") {
            const QFileInfo file(args.value("path").toString());
            const auto variant = args.value("variant").toString();
            if (!file.isAbsolute() || !file.isFile() || !file.isReadable() || file.size() > 1024*1024 || file.suffix().toLower() != "png") return fail("Choose a readable PNG skin smaller than 1 MiB");
            QImageReader reader(file.absoluteFilePath());
            const auto size = reader.size();
            if (reader.format() != "png" || (size != QSize(64,64) && size != QSize(64,32)) || reader.read().isNull()) return fail("Skin must be a valid 64×64 or 64×32 PNG");
            if (variant != "classic" && variant != "slim") return fail("Choose Classic or Slim");
            request = makeSkinUploadRequest(account->accessToken(), file.absoluteFilePath(), variant);
        } else if (method == "resetSkin") {
            if (args.value("confirmed").metaType().id() != QMetaType::Bool || !args.value("confirmed").toBool()) return fail("Confirm resetting the skin");
            request = makeSkinDeleteRequest(account->accessToken());
        } else {
            const auto cape = args.value("cape").toString();
            if (!cape.isEmpty() && !profile.capes.contains(cape)) return fail("This cape is not owned by the selected account");
            request = makeCapeChangeRequest(account->accessToken(), cape);
        }
        request->setNetwork(APPLICATION->network());
        connect(request.get(), &Task::succeeded, this, [account] { auto refresh = account->refresh(); refresh->start(); });
        return startTask(request, "Update Minecraft appearance");
    });
}
