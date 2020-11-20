/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2009 Michael Zanetti <michael_zanetti@gmx.net>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "slideback.h"

namespace KWin
{

SlideBackEffect::SlideBackEffect()
{
    m_tabboxActive = 0;
    m_justMapped = m_upmostWindow = nullptr;
    connect(effects, &EffectsHandler::windowAdded, this, &SlideBackEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &SlideBackEffect::slotWindowDeleted);
    connect(effects, &EffectsHandler::windowUnminimized, this, &SlideBackEffect::slotWindowUnminimized);
    connect(effects, &EffectsHandler::tabBoxAdded, this, &SlideBackEffect::slotTabBoxAdded);
    connect(effects, &EffectsHandler::stackingOrderChanged, this, &SlideBackEffect::slotStackingOrderChanged);
    connect(effects, &EffectsHandler::tabBoxClosed, this, &SlideBackEffect::slotTabBoxClosed);
}

void SlideBackEffect::slotStackingOrderChanged()
{
    if (effects->activeFullScreenEffect() || m_tabboxActive) {
        oldStackingOrder = effects->stackingOrder();
        usableOldStackingOrder = usableWindows(oldStackingOrder);
        return;
    }

    EffectWindowList newStackingOrder = effects->stackingOrder(),
                     usableNewStackingOrder = usableWindows(newStackingOrder);
    if (usableNewStackingOrder == usableOldStackingOrder || usableNewStackingOrder.isEmpty()) {
        oldStackingOrder = newStackingOrder;
        usableOldStackingOrder = usableNewStackingOrder;
        return;
    }

    m_upmostWindow = usableNewStackingOrder.last();

    if (m_upmostWindow == m_justMapped ) // a window was added, got on top, stacking changed. Nothing impressive
        m_justMapped = nullptr;
    else if (!usableOldStackingOrder.isEmpty() && m_upmostWindow != usableOldStackingOrder.last())
        windowRaised(m_upmostWindow);

    oldStackingOrder = newStackingOrder;
    usableOldStackingOrder = usableNewStackingOrder;

}

void SlideBackEffect::windowRaised(EffectWindow *w)
{
    // Determine all windows on top of the activated one
    bool currentFound = false;
    foreach (EffectWindow * tmp, oldStackingOrder) {
        if (!currentFound) {
            if (tmp == w) {
                currentFound = true;
            }
        } else {
            if (isWindowUsable(tmp) && tmp->isOnCurrentDesktop() && w->isOnCurrentDesktop()) {
                // Do we have to move it?
                if (intersects(w, tmp->geometry())) {
                    QRect slideRect;
                    slideRect = getSlideDestination(getModalGroupGeometry(w), tmp->geometry());
                    effects->setElevatedWindow(tmp, true);
                    elevatedList.append(tmp);
                    motionManager.manage(tmp);
                    motionManager.moveWindow(tmp, slideRect);
                    destinationList.insert(tmp, slideRect);
                    coveringWindows.append(tmp);
                } else {
                    //Does it intersect with a moved (elevated) window and do we have to elevate it too?
                    foreach (EffectWindow * elevatedWindow, elevatedList) {
                        if (tmp->geometry().intersects(elevatedWindow->geometry())) {
                            effects->setElevatedWindow(tmp, true);
                            elevatedList.append(tmp);
                            break;
                        }
                    }

                }
            }
            if (tmp->isDock() || tmp->keepAbove()) {
                effects->setElevatedWindow(tmp, true);
                elevatedList.append(tmp);
            }
        }
    }
    // If a window is minimized it could happen that the panels stay elevated without any windows sliding.
    // clear all elevation settings
    if (!motionManager.managingWindows()) {
        foreach (EffectWindow * tmp, elevatedList) {
            effects->setElevatedWindow(tmp, false);
        }
    }
}

QRect SlideBackEffect::getSlideDestination(const QRect &windowUnderGeometry, const QRect &windowOverGeometry)
{
    // Determine the shortest way:
    int leftSlide = windowUnderGeometry.left() - windowOverGeometry.right() - 20;
    int rightSlide = windowUnderGeometry.right() - windowOverGeometry.left() + 20;
    int upSlide = windowUnderGeometry.top() -  windowOverGeometry.bottom() - 20;
    int downSlide = windowUnderGeometry.bottom() - windowOverGeometry.top() + 20;

    int horizSlide = leftSlide;
    if (qAbs(horizSlide) > qAbs(rightSlide)) {
        horizSlide = rightSlide;
    }
    int vertSlide = upSlide;
    if (qAbs(vertSlide) > qAbs(downSlide)) {
        vertSlide = downSlide;
    }

    QRect slideRect = windowOverGeometry;
    if (qAbs(horizSlide) < qAbs(vertSlide)) {
        slideRect.moveLeft(slideRect.x() + horizSlide);
    } else {
        slideRect.moveTop(slideRect.y() + vertSlide);
    }
    return slideRect;
}

void SlideBackEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    int time = 0;
    if (m_lastPresentTime.count()) {
        time = (presentTime - m_lastPresentTime).count();
    }
    m_lastPresentTime = presentTime;

    if (motionManager.managingWindows()) {
        motionManager.calculate(time);
        data.mask |= Effect::PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }

    for (auto const &w : effects->stackingOrder()) {
        w->setData(WindowForceBlurRole, QVariant(true));
    }

    effects->prePaintScreen(data, presentTime);
}

void SlideBackEffect::postPaintScreen()
{
    if (motionManager.areWindowsMoving()) {
        effects->addRepaintFull();
    }

    for (auto &w : effects->stackingOrder()) {
        w->setData(WindowForceBlurRole, QVariant());
    }

    effects->postPaintScreen();
}

void SlideBackEffect::prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime)
{
    if (motionManager.isManaging(w)) {
        data.setTransformed();
    }

    effects->prePaintWindow(w, data, presentTime);
}

void SlideBackEffect::paintWindow(EffectWindow *w, int mask, QRegion region, WindowPaintData &data)
{
    if (motionManager.isManaging(w)) {
        motionManager.apply(w, data);
    }
    foreach (const QRegion &r, clippedRegions) {
        region = region.intersected(r);
    }
    effects->paintWindow(w, mask, region, data);
    for (int i = clippedRegions.count() - 1; i > -1; --i)
        PaintClipper::pop(clippedRegions.at(i));
    clippedRegions.clear();
}

void SlideBackEffect::postPaintWindow(EffectWindow* w)
{
    if (motionManager.isManaging(w)) {
        if (destinationList.contains(w)) {
            if (!motionManager.isWindowMoving(w)) { // has window reched its destination?
                // If we are still intersecting with the upmostWindow it is moving. slide to somewhere else
                // restore the stacking order of all windows not intersecting any more except panels
                if (coveringWindows.contains(w)) {
                    EffectWindowList tmpList;
                    foreach (EffectWindow * tmp, elevatedList) {
                        QRect elevatedGeometry = tmp->geometry();
                        if (motionManager.isManaging(tmp)) {
                            elevatedGeometry = motionManager.transformedGeometry(tmp).toAlignedRect();
                        }
                        if (m_upmostWindow && !tmp->isDock() && !tmp->keepAbove() && m_upmostWindow->geometry().intersects(elevatedGeometry)) {
                            QRect newDestination;
                            newDestination = getSlideDestination(getModalGroupGeometry(m_upmostWindow), elevatedGeometry);
                            if (!motionManager.isManaging(tmp)) {
                                motionManager.manage(tmp);
                            }
                            motionManager.moveWindow(tmp, newDestination);
                            destinationList[tmp] = newDestination;
                        } else {
                            if (!tmp->isDock()) {
                                bool keepElevated = false;
                                foreach (EffectWindow * elevatedWindow, tmpList) {
                                    if (tmp->geometry().intersects(elevatedWindow->geometry())) {
                                        keepElevated = true;
                                    }
                                }
                                if (!keepElevated) {
                                    effects->setElevatedWindow(tmp, false);
                                    elevatedList.removeAll(tmp);
                                }
                            }
                        }
                        tmpList.append(tmp);
                    }
                } else {
                    // Move the window back where it belongs
                    motionManager.moveWindow(w, w->geometry());
                    destinationList.remove(w);
                }
            }
        } else {
            // is window back at its original position?
            if (!motionManager.isWindowMoving(w)) {
                motionManager.unmanage(w);
                effects->addRepaintFull();
            }
        }
        if (coveringWindows.contains(w)) {
            // It could happen that there is no aciveWindow() here if the user clicks the close-button on an inactive window.
            // Just skip... the window will be removed in windowDeleted() later
            if (m_upmostWindow && !intersects(m_upmostWindow, motionManager.transformedGeometry(w).toAlignedRect())) {
                coveringWindows.removeAll(w);
                if (coveringWindows.isEmpty()) {
                    // Restore correct stacking order
                    foreach (EffectWindow * tmp, elevatedList) {
                        effects->setElevatedWindow(tmp, false);
                    }
                    elevatedList.clear();
                }
            }
        }
    }
    if (!isActive()) {
        m_lastPresentTime = std::chrono::milliseconds::zero();
    }
    effects->postPaintWindow(w);
}

void SlideBackEffect::slotWindowDeleted(EffectWindow* w)
{
    if (w == m_upmostWindow)
        m_upmostWindow = nullptr;
    if (w == m_justMapped)
        m_justMapped = nullptr;
    usableOldStackingOrder.removeAll(w);
    oldStackingOrder.removeAll(w);
    coveringWindows.removeAll(w);
    elevatedList.removeAll(w);
    if (motionManager.isManaging(w)) {
        motionManager.unmanage(w);
    }
}

void SlideBackEffect::slotWindowAdded(EffectWindow *w)
{
    m_justMapped = w;
}

void SlideBackEffect::slotWindowUnminimized(EffectWindow* w)
{
    // SlideBack should not be triggered on an unminimized window. For this we need to store the last unminimized window.
    m_justMapped = w;
    // the stackingOrderChanged() signal came before the window turned an effect window
    // usually this is no problem as the change shall not be caught anyway, but
    // the window may have changed its stack position, bug #353745
    slotStackingOrderChanged();
}

void SlideBackEffect::slotTabBoxAdded()
{
    ++m_tabboxActive;
}

void SlideBackEffect::slotTabBoxClosed()
{
    m_tabboxActive = qMax(m_tabboxActive-1, 0);
}

bool SlideBackEffect::isWindowUsable(EffectWindow* w)
{
    return w && (w->isNormalWindow() || w->isDialog()) && !w->keepAbove() && !w->isDeleted() && !w->isMinimized()
           && w->isPaintingEnabled();
}

bool SlideBackEffect::intersects(EffectWindow* windowUnder, const QRect &windowOverGeometry)
{
    QRect windowUnderGeometry = getModalGroupGeometry(windowUnder);
    return windowUnderGeometry.intersects(windowOverGeometry);
}

EffectWindowList SlideBackEffect::usableWindows(const EffectWindowList & allWindows)
{
    EffectWindowList retList;
    auto isWindowVisible = [] (const EffectWindow *window) {
        return window && effects->virtualScreenGeometry().intersects(window->geometry());
    };
    foreach (EffectWindow * tmp, allWindows) {
        if (isWindowUsable(tmp) && isWindowVisible(tmp)) {
            retList.append(tmp);
        }
    }
    return retList;
}

QRect SlideBackEffect::getModalGroupGeometry(EffectWindow *w)
{
    QRect modalGroupGeometry = w->geometry();
    if (w->isModal()) {
        foreach (EffectWindow * modalWindow, w->mainWindows()) {
            modalGroupGeometry = modalGroupGeometry.united(getModalGroupGeometry(modalWindow));
        }
    }
    return modalGroupGeometry;
}

bool SlideBackEffect::isActive() const
{
    return motionManager.managingWindows();
}

} //Namespace
