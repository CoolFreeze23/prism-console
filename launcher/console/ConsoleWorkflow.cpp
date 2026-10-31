// SPDX-License-Identifier: GPL-3.0-only
#include "ConsoleWorkflow.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include <QtWidgets>
#include <QTextFragment>
#include <QBuffer>

namespace {
QVariantMap good(const QVariant& value = {}) { return {{"ok", true}, {"data", value}}; }
QVariantMap bad(const QString& reason) { return {{"ok", false}, {"error", reason}}; }
QString clean(QString value) { value.replace("&&", "\x01"); value.remove('&'); value.replace('\x01', '&'); return value; }
QString plain(const QString& value) { QTextDocument d; d.setHtml(value); return d.toPlainText(); }
QString picture(const QPixmap& pixmap) {
    if (pixmap.isNull()) return {};
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    pixmap.scaled(640, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}
// Field captions read better without Qt's trailing colons ("Skins:" -> "Skins").
QString caption(const QString& text) {
    auto value = Qt::mightBeRichText(text) ? plain(text) : clean(text);
    value = value.trimmed();
    while (value.endsWith(':') || value.endsWith(QChar(0xFF1A))) value.chop(1);
    return value.trimmed();
}
QLayout* owningLayout(QLayout* layout, QWidget* w) {
    if (!layout) return nullptr;
    if (layout->indexOf(w) >= 0) return layout;
    for (int i = 0; i < layout->count(); ++i) if (auto* found = owningLayout(layout->itemAt(i)->layout(), w)) return found;
    return nullptr;
}
// The label that captions a field: its buddy, or the label beside it in a form row / grid cell.
QLabel* partner(QWidget* w) {
    auto* parent = w->parentWidget(); if (!parent) return nullptr;
    for (auto* l : parent->findChildren<QLabel*>()) if (l->buddy() == w) return l;
    auto* layout = owningLayout(parent->layout(), w);
    if (auto* form = qobject_cast<QFormLayout*>(layout)) return qobject_cast<QLabel*>(form->labelForField(w));
    if (auto* grid = qobject_cast<QGridLayout*>(layout)) {
        int row, col, rowSpan, colSpan; grid->getItemPosition(grid->indexOf(w), &row, &col, &rowSpan, &colSpan);
        for (int c = col - 1; c >= 0; --c) if (auto* item = grid->itemAtPosition(row, c)) return qobject_cast<QLabel*>(item->widget());
    }
    if (auto* row = qobject_cast<QBoxLayout*>(layout); row && row->direction() == QBoxLayout::LeftToRight) {
        for (int i = row->indexOf(w) - 1; i >= 0; --i) if (auto* item = row->itemAt(i); item && item->widget()) return qobject_cast<QLabel*>(item->widget());
    }
    return nullptr;
}
bool isField(QWidget* w) {
    return qobject_cast<QLineEdit*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QTextEdit*>(w) ||
           qobject_cast<QPlainTextEdit*>(w) || (qobject_cast<QAbstractSlider*>(w) && !qobject_cast<QScrollBar*>(w));
}
QString label(QWidget* w) {
    if (!w->accessibleName().isEmpty()) return w->accessibleName();
    if (auto* l = partner(w)) return caption(l->text());
    // Never surface Qt object names ("additionalInstDirsList") as user-facing text.
    return plain(w->toolTip());
}
}

ConsoleWorkflow::ConsoleWorkflow(MainWindow* main, std::function<void()> changed, std::function<void(const QUrl&)> openLink)
    : QObject(main), m_main(main), m_changed(std::move(changed)), m_openLink(std::move(openLink)) { qApp->installEventFilter(this); }

QString ConsoleWorkflow::id(QObject* object) {
    QString value = object->property("consoleControlId").toString();
    if (value.isEmpty()) { value = QString::number(++m_serial); object->setProperty("consoleControlId", value); m_objects[value] = object; }
    return value;
}

bool ConsoleWorkflow::eventFilter(QObject* object, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || widget == m_main || !widget->isWindow()) return false;
    const bool managed = qobject_cast<QDialog*>(widget) || qobject_cast<QMenu*>(widget) || qobject_cast<QMainWindow*>(widget);
    if (!managed) return false;
    if (event->type() == QEvent::Polish || event->type() == QEvent::Show) {
        widget->setAttribute(Qt::WA_DontShowOnScreen);
        widget->setAttribute(Qt::WA_ShowWithoutActivating);
        // exec() may have made a dialog application-modal. Its renderer lives
        // in the main window, which must stay input-capable while exec waits.
        widget->setWindowModality(Qt::NonModal);
        widget->setAttribute(Qt::WA_ShowModal, false);
        if (event->type() == QEvent::Show) {
            m_windows.removeAll(widget); m_windows.append(widget); id(widget);
            QTimer::singleShot(0, this, m_changed);
        }
    }
    if (event->type() == QEvent::Hide || event->type() == QEvent::Close) QTimer::singleShot(0, this, m_changed);
    return false;
}

QPointer<QWidget> ConsoleWorkflow::active() {
    for (int i = m_windows.size() - 1; i >= 0; --i) {
        if (!m_windows[i]) { m_windows.removeAt(i); continue; }
        if (m_windows[i]->isVisible()) return m_windows[i];
    }
    return {};
}

QVariantMap ConsoleWorkflow::snapshot() {
    auto w = active(); if (!w) return {};
    return {{"id", id(w)}, {"title", w->windowTitle()}, {"content", node(w)}, {"blocked", QApplication::activeModalWidget() != nullptr}};
}

QVariantMap ConsoleWorkflow::node(QWidget* w, int depth) {
    if (!w || depth > 25) return {};
    // "name" is the Qt object name, for tests and diagnostics only; the UI never displays it.
    QVariantMap out{{"id", id(w)}, {"name", w->objectName()}, {"label", label(w)}, {"enabled", w->isEnabled()}, {"type", "group"}};
    if (auto* menu = qobject_cast<QMenu*>(w)) {
        out["type"] = "menu"; QVariantList actions;
        for (auto* a : menu->actions()) if (a->isVisible() && !a->isSeparator()) actions.append(QVariantMap{{"id", id(a)}, {"text", clean(a->text())}, {"enabled", a->isEnabled()}, {"checked", a->isChecked()}});
        out["options"] = actions; return out;
    }
    if (auto* b = qobject_cast<QAbstractButton*>(w)) {
        if (auto* tool = qobject_cast<QToolButton*>(b)) out["menu"] = tool->menu() != nullptr;
        out["type"] = "button"; out["text"] = clean(b->text()); out["checked"] = b->isCheckable() ? QVariant(b->isChecked()) : QVariant();
        if (b->text().isEmpty()) { out["text"] = label(w); out["image"] = picture(b->icon().pixmap(48)); }
        else if (!b->toolTip().isEmpty()) out["hint"] = plain(b->toolTip());
        if (b->isCheckable()) {
            // Radio buttons sharing a group render as one segmented choice.
            out["role"] = qobject_cast<QRadioButton*>(b) ? "radio" : qobject_cast<QCheckBox*>(b) ? "check" : "toggle";
            if (auto* group = b->group()) out["group"] = id(group);
            else if (b->autoExclusive() && b->parentWidget()) out["group"] = "parent-" + id(b->parentWidget());
        }
        return out;
    }
    if (auto* l = qobject_cast<QLabel*>(w)) {
        out["type"] = "label"; out["text"] = Qt::mightBeRichText(l->text()) ? plain(l->text()) : clean(l->text()); out["image"] = picture(l->pixmap());
        if (!out["text"].toString().contains('\n') && out["text"].toString().size() < 60) out["text"] = caption(out["text"].toString());
        QVariantList links; QTextDocument doc; doc.setHtml(l->text());
        for (auto block = doc.begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) {
            auto fragment = it.fragment(); auto format = fragment.charFormat();
            if (format.isAnchor() && !format.anchorHref().isEmpty()) links.append(QVariantMap{{"text", fragment.text()}, {"url", format.anchorHref()}});
        }
        out["links"] = links; return out;
    }
    if (auto* line = qobject_cast<QLineEdit*>(w)) {
        const bool secret = line->echoMode() != QLineEdit::Normal || QRegularExpression("pass|token|flamekey|secret", QRegularExpression::CaseInsensitiveOption).match(line->objectName()).hasMatch();
        out["type"] = "input"; out["secret"] = secret;
        out["value"] = !secret ? line->text() : QString();
        out["placeholder"] = secret && !line->text().isEmpty() ? "Saved value (leave unchanged to keep)" : line->placeholderText();
        out["readonly"] = line->isReadOnly(); out["maxLength"] = line->maxLength(); return out;
    }
    if (auto* combo = qobject_cast<QComboBox*>(w)) {
        out["type"] = "select"; out["value"] = combo->currentIndex(); out["editable"] = combo->isEditable(); out["text"] = combo->currentText();
        QVariantList options; for (int i = 0; i < combo->count(); ++i) options.append(QVariantMap{{"index", i}, {"text", combo->itemText(i)}, {"enabled", bool(combo->model()->flags(combo->model()->index(i, combo->modelColumn(), combo->rootModelIndex())) & Qt::ItemIsEnabled)}});
        out["options"] = options; return out;
    }
    if (auto* spin = qobject_cast<QSpinBox*>(w)) {
        out["type"] = "number"; out["value"] = spin->value(); out["min"] = spin->minimum(); out["max"] = spin->maximum(); out["step"] = spin->singleStep(); out["suffix"] = spin->suffix(); return out;
    }
    if (auto* spin = qobject_cast<QDoubleSpinBox*>(w)) {
        out["type"] = "number"; out["value"] = spin->value(); out["min"] = spin->minimum(); out["max"] = spin->maximum(); out["step"] = spin->singleStep(); out["suffix"] = spin->suffix(); return out;
    }
    if (auto* slider = qobject_cast<QAbstractSlider*>(w)) {
        if (qobject_cast<QScrollBar*>(w)) return {};
        out["type"] = "number"; out["value"] = slider->value(); out["min"] = slider->minimum(); out["max"] = slider->maximum(); out["step"] = slider->singleStep(); return out;
    }
    if (auto* edit = qobject_cast<QTextEdit*>(w)) {
        out["type"] = "text"; out["value"] = edit->toPlainText(); out["readonly"] = edit->isReadOnly();
        QVariantList links;
        for (auto block = edit->document()->begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) {
            auto fragment = it.fragment(); auto format = fragment.charFormat();
            if (format.isAnchor() && !format.anchorHref().isEmpty()) links.append(QVariantMap{{"text", fragment.text()}, {"url", format.anchorHref()}});
        }
        out["links"] = links; return out;
    }
    if (auto* edit = qobject_cast<QPlainTextEdit*>(w)) { out["type"] = "text"; out["value"] = edit->toPlainText(); out["readonly"] = edit->isReadOnly(); return out; }
    if (auto* progress = qobject_cast<QProgressBar*>(w)) { out["type"] = "progress"; out["value"] = progress->value(); out["max"] = progress->maximum(); out["text"] = progress->text(); return out; }
    if (auto* tabs = qobject_cast<QTabWidget*>(w)) {
        out["type"] = "tabs"; out["value"] = tabs->currentIndex(); QVariantList options;
        for (int i = 0; i < tabs->count(); ++i) if (tabs->isTabVisible(i)) options.append(QVariantMap{{"index", i}, {"text", clean(tabs->tabText(i))}, {"enabled", tabs->isTabEnabled(i)}});
        out["options"] = options; out["children"] = QVariantList{node(tabs->currentWidget(), depth + 1)}; return out;
    }
    if (auto* stack = qobject_cast<QStackedWidget*>(w)) { out["children"] = QVariantList{node(stack->currentWidget(), depth + 1)}; return out; }
    if (auto* view = qobject_cast<QAbstractItemView*>(w)) {
        out["type"] = "list"; out["sidebar"] = w->property("_kde_side_panel_view").toBool(); out["multi"] = view->selectionMode() != QAbstractItemView::SingleSelection; out["context"] = view->contextMenuPolicy() == Qt::CustomContextMenu; auto* model = view->model(); if (!model) return out;
        QVariantList rows; auto* tree = qobject_cast<QTreeView*>(w);
        int budget = 250;
        std::function<void(QModelIndex, QVariantList, int)> walk = [&](QModelIndex parent, QVariantList path, int level) {
            int offset = level == 0 ? w->property("consoleOffset").toInt() : 0;
            for (int row = offset; row < model->rowCount(parent) && budget > 0; ++row) {
                if (tree && tree->isRowHidden(row, parent)) continue;
                auto idx = model->index(row, 0, parent); if (!idx.isValid()) continue;
                --budget; auto next = path; next.append(row); QStringList cells;
                for (int col = 0; col < model->columnCount(parent); ++col) if (!tree || !tree->isColumnHidden(col)) cells.append(model->data(model->index(row, col, parent)).toString());
                auto flags = model->flags(idx); QVariantMap r{{"path", next}, {"text", cells.join(" · ")}, {"depth", level}, {"enabled", bool(flags & Qt::ItemIsEnabled)}, {"selected", view->selectionModel() && view->selectionModel()->isSelected(idx)}, {"children", model->hasChildren(idx)}, {"expanded", tree && tree->isExpanded(idx)}, {"editable", bool(flags & Qt::ItemIsEditable)}};
                r["editValue"] = model->data(idx, Qt::EditRole).toString();
                auto decoration = model->data(idx, Qt::DecorationRole);
                if (decoration.canConvert<QIcon>()) r["image"] = picture(qvariant_cast<QIcon>(decoration).pixmap(48));
                else if (decoration.canConvert<QPixmap>()) r["image"] = picture(qvariant_cast<QPixmap>(decoration).scaled(96,96,Qt::KeepAspectRatio));
                if (flags & Qt::ItemIsUserCheckable) r["checked"] = model->data(idx, Qt::CheckStateRole).toInt() == Qt::Checked;
                rows.append(r);
                if (tree && tree->isExpanded(idx) && level < 10) walk(idx, next, level + 1);
            }
        };
        walk(view->rootIndex(), {}, 0); out["rows"] = rows; out["offset"] = w->property("consoleOffset").toInt(); out["total"] = model->rowCount(view->rootIndex()); out["more"] = model->canFetchMore(view->rootIndex()); return out;
    }
    if (auto* group = qobject_cast<QGroupBox*>(w)) { out["label"] = clean(group->title()); if (group->isCheckable()) { out["checkable"] = true; out["checked"] = group->isChecked(); } }
    if (!qobject_cast<QGroupBox*>(w)) out["label"] = w->accessibleName();
    if (auto* skins = qobject_cast<SkinManageDialog*>(w)) {
        if (auto* skin = skins->getSelectedSkin()) {
            auto png = [](const QImage& image) { if (image.isNull()) return QString(); QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64()); };
            out["skin"] = png(skin->getTexture()); out["slim"] = skin->getModel() == SkinModel::SLIM;
            out["cape"] = png(skins->capes().value(skin->getCapeId()));
            auto* elytra = w->findChild<QCheckBox*>("elytraCB"); out["elytra"] = elytra && elytra->isChecked();
        }
    }
    if (qobject_cast<QDialogButtonBox*>(w)) out["type"] = "buttonbox";
    QList<QWidget*> visible;
    for (auto* child : w->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if (!child->isWindow() && !child->isHidden() && !qobject_cast<QSizeGrip*>(child) && !qobject_cast<QMenuBar*>(child)) visible.append(child);
    // Present children in on-screen reading order (rows, then columns), not creation order.
    std::stable_sort(visible.begin(), visible.end(), [](QWidget* a, QWidget* b) {
        const int rowA = a->geometry().center().y() / 12, rowB = b->geometry().center().y() / 12;
        return rowA != rowB ? rowA < rowB : a->x() < b->x();
    });
    // A caption folded into its field is not repeated as a separate label.
    QSet<QWidget*> captions;
    for (auto* child : visible) if (isField(child)) if (auto* l = partner(child)) captions.insert(l);
    QVariantList children;
    for (auto* child : visible) {
        if (captions.contains(child)) continue;
        auto n = node(child, depth + 1); if (!n.isEmpty()) children.append(n);
    }
    out["children"] = children;
    if (w->inherits("PageContainer")) out["type"] = "pages";
    return out;
}

QVariantList ConsoleWorkflow::commands(const QString& instance) {
    if (!instance.isEmpty()) QMetaObject::invokeMethod(m_main, "instanceSelectRequest", Qt::DirectConnection, Q_ARG(QString, instance));
    QVariantList commands;
    for (auto* a : m_main->findChildren<QAction*>()) {
        if (!a->isVisible() || a->isSeparator() || a->text().isEmpty() || a->menu() || a->objectName().isEmpty()) continue;
        commands.append(QVariantMap{{"id", id(a)}, {"text", clean(a->text())}, {"enabled", a->isEnabled()}, {"name", a->objectName()}});
    }
    return commands;
}

QVariantMap ConsoleWorkflow::dispatch(const QVariantMap& args) {
    auto* object = m_objects.value(args.value("control").toString()).data();
    if (!object) return bad("This control is no longer available; refresh the page");
    const auto op = args.value("op").toString();
    auto top = active(); auto* widget = qobject_cast<QWidget*>(object);
    auto* action = qobject_cast<QAction*>(object);
    if (widget && (!top || (widget != top && !top->isAncestorOf(widget)) || !widget->isEnabled() || widget->isHidden())) return bad("Control is not active");
    if (action) {
        if (!action->isEnabled()) return bad("Action is unavailable");
        if (top && !top->findChildren<QAction*>().contains(action) && action->parent() != top) return bad("Finish the open dialog first");
        if (op != "click") return bad("Invalid action");
        QPointer<QAction> safe(action); QPointer<QMenu> menu(qobject_cast<QMenu*>(top.data()));
        QTimer::singleShot(0, this, [safe, menu] { if (safe) {
            if (safe->menu()) safe->menu()->popup(QPoint());
            else if (menu) { menu->setActiveAction(safe); QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier); QApplication::sendEvent(menu, &press); }
            else safe->trigger();
        } }); return good();
    }
    if (!widget) return bad("Unknown control");
    if (op == "close" && widget == top) { QPointer<QWidget> safe(widget); QTimer::singleShot(0, this, [safe] { if (safe) safe->close(); }); return good(); }
    const auto value = args.value("value");
    if (auto* tool = qobject_cast<QToolButton*>(widget); tool && op == "menu" && tool->menu()) {
        QPointer<QMenu> safe(tool->menu()); QTimer::singleShot(0, this, [safe] { if (safe) safe->popup(QPoint()); });
    } else if (auto* line = qobject_cast<QLineEdit*>(widget); line && op == "enter" && !line->isReadOnly()) {
        QPointer<QLineEdit> safe(line); QTimer::singleShot(0, this, [safe] { if (safe) { QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier); QApplication::sendEvent(safe, &event); } });
    } else if (auto* label = qobject_cast<QLabel*>(widget); label && op == "link") {
        const QUrl url(value.toString()); bool found = false; QTextDocument doc; doc.setHtml(label->text());
        for (auto block = doc.begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) if (it.fragment().charFormat().anchorHref() == url.toString()) found = true;
        if (!found) return bad("Link is no longer available");
        QPointer<QLabel> safe(label); QTimer::singleShot(0, this, [this, safe, url] { if (safe) { if (safe->openExternalLinks() && (url.scheme() == "https" || url.scheme() == "http")) m_openLink(url); else emit safe->linkActivated(url.toString()); } });
    } else if (auto* button = qobject_cast<QAbstractButton*>(widget); button && op == "click") {
        QPointer<QAbstractButton> safe(button); QTimer::singleShot(0, this, [safe] { if (safe) { if (auto* tool = qobject_cast<QToolButton*>(safe.data()); tool && tool->menu() && tool->popupMode() == QToolButton::InstantPopup) tool->menu()->popup(QPoint()); else safe->click(); } });
    } else if (auto* line = qobject_cast<QLineEdit*>(widget); line && op == "value" && !line->isReadOnly()) {
        if (value.toString().size() > line->maxLength()) return bad("Text is too long");
        line->setText(value.toString()); emit line->textEdited(line->text()); emit line->editingFinished();
    } else if (auto* edit = qobject_cast<QPlainTextEdit*>(widget); edit && op == "value" && !edit->isReadOnly()) { edit->setPlainText(value.toString());
    } else if (auto* edit = qobject_cast<QTextEdit*>(widget); edit && op == "value" && !edit->isReadOnly()) { edit->setPlainText(value.toString());
    } else if (auto* combo = qobject_cast<QComboBox*>(widget); combo && op == "select") {
        int index = value.toInt(); if (index < 0 || index >= combo->count()) return bad("Invalid option");
        if (!(combo->model()->flags(combo->model()->index(index, combo->modelColumn(), combo->rootModelIndex())) & Qt::ItemIsEnabled)) return bad("Option is unavailable");
        combo->setCurrentIndex(index); emit combo->activated(index);
    } else if (auto* combo = qobject_cast<QComboBox*>(widget); combo && op == "value" && combo->isEditable()) { combo->setEditText(value.toString());
    } else if (auto* spin = qobject_cast<QSpinBox*>(widget); spin && op == "value") { spin->setValue(value.toInt()); emit spin->editingFinished();
    } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget); spin && op == "value") { spin->setValue(value.toDouble()); emit spin->editingFinished();
    } else if (auto* slider = qobject_cast<QAbstractSlider*>(widget); slider && op == "value") { slider->setValue(value.toInt());
    } else if (auto* tabs = qobject_cast<QTabWidget*>(widget); tabs && op == "select") {
        int index = value.toInt(); if (index < 0 || index >= tabs->count() || !tabs->isTabEnabled(index)) return bad("Tab is unavailable"); tabs->setCurrentIndex(index);
    } else if (auto* group = qobject_cast<QGroupBox*>(widget); group && op == "click" && group->isCheckable()) { group->setChecked(!group->isChecked());
    } else if (auto* browser = qobject_cast<QTextBrowser*>(widget); browser && op == "link") {
        const QUrl url(value.toString()); bool found = false;
        for (auto block = browser->document()->begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) if (it.fragment().charFormat().anchorHref() == url.toString()) found = true;
        if (!found) return bad("Link is no longer available");
        QPointer<QTextBrowser> safe(browser); QTimer::singleShot(0, this, [this, safe, url] { if (safe) { if (safe->openExternalLinks() && (url.scheme() == "https" || url.scheme() == "http")) m_openLink(url); else emit safe->anchorClicked(url); } });
    } else if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
        auto* model = view->model(); if (!model) return bad("List is unavailable");
        if (op == "page") { int offset = value.toInt(); if (offset < 0 || offset >= qMax(1, model->rowCount(view->rootIndex()))) return bad("Invalid page"); widget->setProperty("consoleOffset", offset);
        } else if (op == "fetch") { if (model->canFetchMore(view->rootIndex())) model->fetchMore(view->rootIndex());
        } else {
            auto index = view->rootIndex(); for (const auto& row : args.value("path").toList()) { index = model->index(row.toInt(), 0, index); if (!index.isValid()) return bad("List changed; refresh it"); }
            if (!index.isValid() || !(model->flags(index) & Qt::ItemIsEnabled)) return bad("Item is unavailable");
            QStringList currentCells; auto* treeView = qobject_cast<QTreeView*>(view);
            for (int col = 0; col < model->columnCount(index.parent()); ++col) if (!treeView || !treeView->isColumnHidden(col)) currentCells.append(model->data(index.siblingAtColumn(col)).toString());
            if (args.value("expected").toString() != currentCells.join(" · ")) return bad("List changed; select the item again");
            if (op == "expand") { auto* tree = qobject_cast<QTreeView*>(view); if (!tree) return bad("Not a tree"); tree->setExpanded(index, !tree->isExpanded(index));
            } else if (op == "check" && (model->flags(index) & Qt::ItemIsUserCheckable)) { model->setData(index, value.toBool() ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
            } else if (op == "edit" && (model->flags(index) & Qt::ItemIsEditable)) { if (!model->setData(index, value.toString(), Qt::EditRole)) return bad("Prism rejected the edit");
            } else if (op == "select" || op == "activate" || op == "context") {
                if (!view->selectionModel()) return bad("Selection unavailable");
                auto flags = QItemSelectionModel::ClearAndSelect;
                if (args.value("multi").toBool() && view->selectionMode() != QAbstractItemView::SingleSelection) flags = QItemSelectionModel::Toggle;
                view->selectionModel()->setCurrentIndex(index, flags | QItemSelectionModel::Rows);
                emit view->clicked(index);
                if (op == "activate") { emit view->doubleClicked(index); }
                if (op == "context") { QPointer<QAbstractItemView> safe(view); QPersistentModelIndex persistent(index); QTimer::singleShot(0, this, [safe, persistent] { if (safe && persistent.isValid()) emit safe->customContextMenuRequested(safe->visualRect(persistent).center()); }); }
            } else return bad("Invalid list operation");
        }
    } else return bad("Unsupported operation for this control");
    m_changed(); return good();
}
