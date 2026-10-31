// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QHash>
#include <QObject>
class QSocketNotifier;
class QTimer;

// Watches every readable gamepad for View + Menu held together, so the player can switch between the game
// and the launcher while the game owns the screen. Devices are read, never grabbed: games still receive the
// buttons. In Gaming Mode that is Steam Input's virtual pad; a pad Steam keeps for itself cannot be read.
class GamepadChord final : public QObject {
    Q_OBJECT
   public:
    explicit GamepadChord(QObject* parent);
    ~GamepadChord() override;

   signals:
    void progress(double fraction);  // while the chord is held, 0..1; back to 0 on release
    void triggered();                // once per hold, after a second

   private:
    struct Pad {
        QSocketNotifier* notifier = nullptr;
        bool view = false, menu = false;
    };
    void rescan();
    void read(int fd);
    void drop(int fd);
    void evaluate();
    QHash<QString, int> m_paths;  // /dev/input/eventN -> fd, or -1 for devices that are not usable gamepads
    QHash<int, Pad> m_pads;
    QTimer* m_hold = nullptr;
    qint64 m_since = 0;
    bool m_fired = false;
};
