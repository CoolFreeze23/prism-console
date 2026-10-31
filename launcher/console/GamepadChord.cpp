// SPDX-License-Identifier: GPL-3.0-only
#include "GamepadChord.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace {
constexpr int kHoldMs = 1000;
#ifdef Q_OS_LINUX
// A gamepad for our purposes: reports View (BTN_SELECT), Menu (BTN_START) and a face button.
bool isGamepad(int fd)
{
    constexpr size_t bitsPerLong = 8 * sizeof(unsigned long);
    unsigned long bits[KEY_MAX / bitsPerLong + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
        return false;
    const auto has = [&](int code) { return bits[code / bitsPerLong] & (1UL << (code % bitsPerLong)); };
    return has(BTN_SELECT) && has(BTN_START) && has(BTN_SOUTH);
}
#endif
}  // namespace

GamepadChord::GamepadChord(QObject* parent) : QObject(parent)
{
#ifdef Q_OS_LINUX
    m_hold = new QTimer(this);
    m_hold->setInterval(50);
    connect(m_hold, &QTimer::timeout, this, &GamepadChord::evaluate);
    // Steam creates its virtual pads when a tile starts, so new devices are picked up as they appear.
    auto* scan = new QTimer(this);
    connect(scan, &QTimer::timeout, this, &GamepadChord::rescan);
    scan->start(3000);
    rescan();
#endif
}

GamepadChord::~GamepadChord()
{
    for (const auto fd : m_pads.keys())
        drop(fd);
}

void GamepadChord::rescan()
{
#ifdef Q_OS_LINUX
    const auto present = QDir("/dev/input").entryList({ "event*" }, QDir::System);
    for (const auto& path : m_paths.keys())
        if (!present.contains(QFileInfo(path).fileName())) {
            if (const int fd = m_paths.value(path); fd >= 0)
                drop(fd);
            m_paths.remove(path);
        }
    for (const auto& name : present) {
        const auto path = "/dev/input/" + name;
        if (m_paths.contains(path))
            continue;
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;  // not readable for this user (yet); tried again on the next scan
        if (!isGamepad(fd)) {
            ::close(fd);
            m_paths.insert(path, -1);
            continue;
        }
        auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this, fd] { read(fd); });
        m_paths.insert(path, fd);
        m_pads.insert(fd, Pad{ notifier });
    }
#endif
}

void GamepadChord::read(int fd)
{
#ifdef Q_OS_LINUX
    input_event events[64];
    for (;;) {
        const auto bytes = ::read(fd, events, sizeof events);
        if (bytes <= 0) {
            if (bytes == 0 || (errno != EAGAIN && errno != EINTR))
                drop(fd);  // unplugged
            break;
        }
        auto& pad = m_pads[fd];
        for (size_t i = 0; i < size_t(bytes) / sizeof(input_event); ++i) {
            if (events[i].type != EV_KEY)
                continue;
            if (events[i].code == BTN_SELECT)
                pad.view = events[i].value != 0;
            else if (events[i].code == BTN_START)
                pad.menu = events[i].value != 0;
        }
    }
    evaluate();
#else
    Q_UNUSED(fd)
#endif
}

void GamepadChord::drop(int fd)
{
#ifdef Q_OS_LINUX
    if (auto pad = m_pads.take(fd); pad.notifier) {
        pad.notifier->setEnabled(false);
        pad.notifier->deleteLater();
    }
    for (auto it = m_paths.begin(); it != m_paths.end(); ++it)
        if (it.value() == fd) {
            m_paths.erase(it);
            break;
        }
    ::close(fd);
    evaluate();
#else
    Q_UNUSED(fd)
#endif
}

void GamepadChord::evaluate()
{
    bool held = false;
    for (const auto& pad : std::as_const(m_pads))
        held = held || (pad.view && pad.menu);
    if (!held) {
        if (m_since || m_fired) {
            m_since = 0;
            m_fired = false;
            m_hold->stop();
            emit progress(0);
        }
        return;
    }
    if (m_fired)
        return;  // one switch per hold; release to arm again
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!m_since) {
        m_since = now;
        m_hold->start();
    }
    const double fraction = qMin(1.0, double(now - m_since) / kHoldMs);
    emit progress(fraction);
    if (fraction >= 1.0) {
        m_fired = true;
        m_hold->stop();
        emit triggered();
        emit progress(0);
    }
}
