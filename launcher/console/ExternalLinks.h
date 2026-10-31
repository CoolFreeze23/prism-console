// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QUrl>
#include <functional>

class QObject;

// Opens a link for the controller interface and reports whether it was opened.
// Steam's own URLs (its keyboard, store pages) and, in Gaming Mode, web links (Steam's browser) go to the running
// Steam client through its local CEF endpoint; everything else uses the desktop's link handler, called directly
// so a refusal is seen. Gaming Mode portals can refuse links: a screen-sharing helper that takes over the portal
// name may answer only screen capture, and Qt's own fallback would then fail without a word.
void openExternal(QObject* owner, const QUrl& url, std::function<void(bool opened)> done);
