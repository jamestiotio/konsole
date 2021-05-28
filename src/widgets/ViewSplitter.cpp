/*
    SPDX-FileCopyrightText: 2006-2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "widgets/ViewSplitter.h"
#include "KonsoleSettings.h"

// Qt
#include <QChildEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QApplication>
#include <QDebug>

// C++
#include <memory>

// Konsole
#include "widgets/ViewContainer.h"
#include "terminalDisplay/TerminalDisplay.h"

using Konsole::ViewSplitter;
using Konsole::TerminalDisplay;

//TODO: Connect the TerminalDisplay destroyed signal here.

namespace {
    int calculateHandleWidth(int settingsEnum) {
        switch (settingsEnum) {
            case Konsole::KonsoleSettings::SplitDragHandleLarge: return 10;
            case Konsole::KonsoleSettings::SplitDragHandleMedium: return  5;
            case Konsole::KonsoleSettings::SplitDragHandleSmall: return  1;
            default: return  1;
        }
    }
}

ViewSplitter::ViewSplitter(QWidget *parent) :
    QSplitter(parent)
{
    setAcceptDrops(true);
    connect(KonsoleSettings::self(), &KonsoleSettings::configChanged, this, [this]{
        setHandleWidth(calculateHandleWidth(KonsoleSettings::self()->splitDragHandleSize()));
    });
}

/* This function is called on the toplevel splitter, we need to look at the actual ViewSplitter inside it */
void ViewSplitter::adjustActiveTerminalDisplaySize(int percentage)
{
    auto focusedTerminalDisplay = activeTerminalDisplay();
    Q_ASSERT(focusedTerminalDisplay);

    auto parentSplitter = qobject_cast<ViewSplitter*>(focusedTerminalDisplay->parent());
    const int containerIndex = parentSplitter->indexOf(activeTerminalDisplay());
    Q_ASSERT(containerIndex != -1);

    QList<int> containerSizes = parentSplitter->sizes();

    const int oldSize = containerSizes[containerIndex];
    const auto newSize = static_cast<int>(oldSize * (1.0 + percentage / 100.0));
    const int perContainerDelta = (count() == 1) ? 0 : ((newSize - oldSize) / (count() - 1)) * (-1);

    for (int& size : containerSizes) {
        size += perContainerDelta;
    }
    containerSizes[containerIndex] = newSize;

    parentSplitter->setSizes(containerSizes);
}

// Get the first splitter that's a parent of the current focused widget.
ViewSplitter *ViewSplitter::activeSplitter()
{
    QWidget *widget = focusWidget() != nullptr ? focusWidget() : this;

    ViewSplitter *splitter = nullptr;

    while ((splitter == nullptr) && (widget != nullptr)) {
        splitter = qobject_cast<ViewSplitter *>(widget);
        widget = widget->parentWidget();
    }

    Q_ASSERT(splitter);
    return splitter;
}

void ViewSplitter::updateSizes()
{
    const int space = (orientation() == Qt::Horizontal ? width() : height()) / count();
    setSizes(QVector<int>(count(), space).toList());
}

void ViewSplitter::addTerminalDisplay(TerminalDisplay *terminalDisplay, Qt::Orientation containerOrientation, AddBehavior behavior)
{
    ViewSplitter *splitter = activeSplitter();
    const int currentIndex = splitter->activeTerminalDisplay() == nullptr ? splitter->count()
                            : splitter->indexOf(splitter->activeTerminalDisplay());

    if (splitter->count() < 2) {
        splitter->insertWidget(behavior == AddBehavior::AddBefore ? currentIndex : currentIndex + 1, terminalDisplay);
        splitter->setOrientation(containerOrientation);
    } else if (containerOrientation == splitter->orientation()) {
        splitter->insertWidget(currentIndex, terminalDisplay);
    } else {
        auto newSplitter = new ViewSplitter();
        TerminalDisplay *oldTerminalDisplay = splitter->activeTerminalDisplay();
        const int oldContainerIndex = splitter->indexOf(oldTerminalDisplay);
        splitter->m_blockPropagatedDeletion = true;
        newSplitter->addWidget(behavior == AddBehavior::AddBefore ? terminalDisplay : oldTerminalDisplay);
        newSplitter->addWidget(behavior == AddBehavior::AddBefore ? oldTerminalDisplay : terminalDisplay);
        newSplitter->setOrientation(containerOrientation);
        newSplitter->updateSizes();
        newSplitter->show();
        splitter->insertWidget(oldContainerIndex, newSplitter);
        splitter->m_blockPropagatedDeletion = false;
    }
    splitter->updateSizes();
}

void ViewSplitter::childEvent(QChildEvent *event)
{
    QSplitter::childEvent(event);

    if (event->removed()) {
        if (count() == 0) {
            auto *parent_splitter = qobject_cast<ViewSplitter *>(parentWidget());
            if (parent_splitter != nullptr) {
                setParent(nullptr);
            }
            deleteLater();
        }
        if (count() == 1) {
            if (!m_blockPropagatedDeletion) {
                auto *parent_splitter = qobject_cast<ViewSplitter *>(parentWidget());
                if (parent_splitter) {
                    parent_splitter->m_blockPropagatedDeletion = true;
                    auto *wdg = widget(0);
                    const int oldContainerIndex = parent_splitter->indexOf(this);
                    wdg->setParent(nullptr);
                    parent_splitter->insertWidget(oldContainerIndex, wdg);
                    parent_splitter->m_blockPropagatedDeletion = false;
                    wdg->setFocus();
                    deleteLater();
                }
            }
        }

        const int numOfTerminals = findChildren<TerminalDisplay*>().count();
        if (numOfTerminals == 0) {
            deleteLater();
        }
    }

    auto terminals = getToplevelSplitter()->findChildren<TerminalDisplay*>();
    for(auto terminal : terminals) {
        terminal->headerBar()->applyVisibilitySettings();
    }
}

void ViewSplitter::handleFocusDirection(Qt::Orientation orientation, int direction)
{
    auto terminalDisplay = activeTerminalDisplay();
    auto parentSplitter = qobject_cast<ViewSplitter*>(terminalDisplay->parentWidget());
    auto topSplitter = parentSplitter->getToplevelSplitter();

    // Find the theme's splitter width + extra space to find valid terminal
    // See https://bugs.kde.org/show_bug.cgi?id=411387 for more info
    const auto handleWidth = parentSplitter->handleWidth() + 3;

    const auto start = QPoint(terminalDisplay->x(), terminalDisplay->y());
    const auto startMapped = parentSplitter->mapTo(topSplitter, start);

    const int newX = orientation != Qt::Horizontal ? startMapped.x() + handleWidth
             : direction == 1 ? startMapped.x() + terminalDisplay->width() + handleWidth
             : startMapped.x() - handleWidth;

    const int newY = orientation != Qt::Vertical ? startMapped.y() + handleWidth
                    : direction == 1 ? startMapped.y() + terminalDisplay->height() + handleWidth
                    : startMapped.y() - handleWidth;

    const auto newPoint = QPoint(newX, newY);
    auto child = topSplitter->childAt(newPoint);

    TerminalDisplay *focusTerminal = nullptr;
    if (auto* terminal = qobject_cast<TerminalDisplay*>(child)) {
        focusTerminal = terminal;
    } else if (qobject_cast<QSplitterHandle*>(child) != nullptr) {
        auto targetSplitter = qobject_cast<QSplitter*>(child->parent());
        focusTerminal = qobject_cast<TerminalDisplay*>(targetSplitter->widget(0));
    } else if (qobject_cast<QWidget*>(child) != nullptr) {
        while(child != nullptr && focusTerminal == nullptr) {
            focusTerminal = qobject_cast<TerminalDisplay*>(child->parentWidget());
            child = child->parentWidget();
        }
    }
    if (focusTerminal != nullptr) {
        focusTerminal->setFocus(Qt::OtherFocusReason);
    }
}

void ViewSplitter::focusUp()
{
    handleFocusDirection(Qt::Vertical, -1);
}

void ViewSplitter::focusDown()
{
    handleFocusDirection(Qt::Vertical, +1);
}

void ViewSplitter::focusLeft()
{
    handleFocusDirection(Qt::Horizontal, -1);
}

void ViewSplitter::focusRight()
{
    handleFocusDirection(Qt::Horizontal, +1);
}

TerminalDisplay *ViewSplitter::activeTerminalDisplay() const
{
    auto focusedWidget = focusWidget();
    auto focusedTerminalDisplay = qobject_cast<TerminalDisplay*>(focusedWidget);

    // TD's child can be focused - try to find parent.
    while (focusedTerminalDisplay == nullptr && focusedWidget != nullptr && focusedWidget != this) {
        focusedWidget = focusedWidget->parentWidget();
        focusedTerminalDisplay = qobject_cast<TerminalDisplay*>(focusedWidget);
    }

    return focusedTerminalDisplay != nullptr ? focusedTerminalDisplay
                                             : findChild<TerminalDisplay*>();
}

void ViewSplitter::toggleMaximizeCurrentTerminal()
{
    m_terminalMaximized = !m_terminalMaximized;
    handleMinimizeMaximize(m_terminalMaximized);
}

namespace {
    void restoreAll(QList<TerminalDisplay*>&& terminalDisplays, QList<ViewSplitter*>&& splitters) {
        for (auto splitter : splitters) {
            splitter->setVisible(true);
        }
        for (auto terminalDisplay : terminalDisplays) {
            terminalDisplay->setVisible(true);
        }
    }
}

bool ViewSplitter::hideRecurse(TerminalDisplay *currentTerminalDisplay) {
    bool allHidden = true;

    for(int i = 0, end = count(); i < end; i++) {
        if (auto *maybeSplitter = qobject_cast<ViewSplitter*>(widget(i))) {
            allHidden = maybeSplitter->hideRecurse(currentTerminalDisplay) && allHidden;
            continue;
        }
        if (auto maybeTerminalDisplay = qobject_cast<TerminalDisplay*>(widget(i))) {
            if (maybeTerminalDisplay == currentTerminalDisplay) {
                allHidden = false;
            } else {
                maybeTerminalDisplay->setVisible(false);
            }
        }
    }

    if (allHidden) {
        setVisible(false);
    }
    return allHidden;
}

void ViewSplitter::handleMinimizeMaximize(bool maximize)
{
    auto topLevelSplitter = getToplevelSplitter();
    auto currentTerminalDisplay = topLevelSplitter->activeTerminalDisplay();
    currentTerminalDisplay->setExpandedMode(maximize);
    if (maximize) {
        for (int i = 0, end = topLevelSplitter->count(); i < end; i++) {
            auto widgetAt = topLevelSplitter->widget(i);
            if (auto *maybeSplitter = qobject_cast<ViewSplitter*>(widgetAt)) {
                maybeSplitter->hideRecurse(currentTerminalDisplay);
            }
            if (auto maybeTerminalDisplay = qobject_cast<TerminalDisplay*>(widgetAt)) {
                if (maybeTerminalDisplay != currentTerminalDisplay) {
                    maybeTerminalDisplay->setVisible(false);
                }
            }
        }
    } else {
        restoreAll(topLevelSplitter->findChildren<TerminalDisplay*>(),
                   topLevelSplitter->findChildren<ViewSplitter*>());
    }
}

ViewSplitter *ViewSplitter::getToplevelSplitter()
{
    ViewSplitter *current = this;
    while(qobject_cast<ViewSplitter*>(current->parentWidget()) != nullptr) {
        current = qobject_cast<ViewSplitter*>(current->parentWidget());
    }
    return current;
}

namespace {
    TerminalDisplay *currentDragTarget = nullptr;
}

void Konsole::ViewSplitter::dragEnterEvent(QDragEnterEvent* ev)
{
    const auto dragId = QStringLiteral("konsole/terminal_display");
    if (ev->mimeData()->hasFormat(dragId)) {
        auto other_pid = ev->mimeData()->data(dragId).toInt();
        // don't accept the drop if it's another instance of konsole
        if (qApp->applicationPid() != other_pid) {
            return;
        }
        if (getToplevelSplitter()->terminalMaximized()) {
            return;
        }
        ev->accept();
    }
}

void Konsole::ViewSplitter::dragMoveEvent(QDragMoveEvent* ev)
{
    auto currentWidget = childAt(ev->pos());
    if (auto terminal = qobject_cast<TerminalDisplay*>(currentWidget)) {
        if ((currentDragTarget != nullptr) && currentDragTarget != terminal) {
            currentDragTarget->hideDragTarget();
        }
        if (terminal == ev->source()) {
            return;
        }
        currentDragTarget = terminal;
        auto localPos = currentDragTarget->mapFromParent(ev->pos());
        currentDragTarget->showDragTarget(localPos);
    }
}

void Konsole::ViewSplitter::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event)
    if (currentDragTarget != nullptr) {
        currentDragTarget->hideDragTarget();
        currentDragTarget = nullptr;
    }
}

void Konsole::ViewSplitter::dropEvent(QDropEvent* ev)
{
    if (ev->mimeData()->hasFormat(QStringLiteral("konsole/terminal_display"))) {
        if (getToplevelSplitter()->terminalMaximized()) {
            return;
        }
        if (currentDragTarget != nullptr) {
            currentDragTarget->hideDragTarget();
            auto source = qobject_cast<TerminalDisplay*>(ev->source());
            source->setVisible(false);
            source->setParent(nullptr);

            currentDragTarget->setFocus(Qt::OtherFocusReason);
            const auto droppedEdge = currentDragTarget->droppedEdge();

            AddBehavior behavior = droppedEdge == Qt::LeftEdge || droppedEdge == Qt::TopEdge
                ? AddBehavior::AddBefore : AddBehavior::AddAfter;

            Qt::Orientation orientation = droppedEdge == Qt::LeftEdge || droppedEdge == Qt::RightEdge
                ? Qt::Horizontal : Qt::Vertical;

            // topLevel is the splitter that's connected with the ViewManager
            // that in turn can call the SessionController.
            Q_EMIT getToplevelSplitter()->terminalDisplayDropped(source);
            addTerminalDisplay(source, orientation, behavior);
            source->setVisible(true);
            currentDragTarget = nullptr;
        }
    }
}

void Konsole::ViewSplitter::showEvent(QShowEvent *)
{
    // Fixes lost focus in background mode.
    setFocusProxy(activeSplitter()->activeTerminalDisplay());
}
