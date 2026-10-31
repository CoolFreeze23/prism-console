// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QHash>
#include <functional>
class QWidget;
class MainWindow;
// Semantic adapter: native widgets keep Prism's workflow/model logic, while
// their controls are rendered by the controller UI, never as desktop windows.
class ConsoleWorkflow final : public QObject {
public:
    ConsoleWorkflow(MainWindow* main, std::function<void()> changed);
    QVariantMap snapshot();
    QVariantMap dispatch(const QVariantMap& args);
    QVariantList commands(const QString& instance);
    bool eventFilter(QObject*, QEvent*) override;
private:
    QString id(QObject* object);
    QVariantMap node(QWidget* widget, int depth = 0);
    QPointer<QWidget> active();
    QPointer<MainWindow> m_main;
    QList<QPointer<QWidget>> m_windows;
    QHash<QString, QPointer<QObject>> m_objects;
    std::function<void()> m_changed;
    quint64 m_serial = 0;
};
