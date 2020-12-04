/*
    SPDX-FileCopyrightText: 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwinglobals.h"

#include <QObject>

namespace KWin
{

class RenderLoopPrivate;

/**
 * The RenderLoop class represents the compositing scheduler on a particular output.
 *
 * The RenderLoop class drives the compositing. The frameRequested() signal is emitted
 * when the loop wants a new frame to be rendered. The frameCompleted() signal is
 * emitted when a previously rendered frame has been presented on the screen. In case
 * you want the compositor to repaint the scene, call the scheduleRepaint() function.
 */
class KWIN_EXPORT RenderLoop : public QObject
{
    Q_OBJECT

public:
    explicit RenderLoop(QObject *parent = nullptr);
    ~RenderLoop() override;

    /**
     * Pauses the render loop. While the render loop is inhibited, scheduleRepaint()
     * requests are queued.
     *
     * Once the render loop is uninhibited, the pending schedule requests are going to
     * be re-applied.
     */
    void inhibit();

    /**
     * Uninhibits the render loop.
     */
    void uninhibit();

    /**
     * This function must be called before the Compositor starts rendering the next
     * frame.
     */
    void beginFrame();

    /**
     * This function must be called after the Compositor has finished rendering the
     * next frame.
     */
    void endFrame();

    /**
     * This function must be called if the Compositor has decided not to render a
     * frame after the frameRequested() signal has been emitted.
     */
    void discardFrame();

    /**
     * Returns the refresh rate at which the output is being updated, in millihertz.
     */
    int refreshRate() const;

    /**
     * Sets the refresh rate of this RenderLoop to @a refreshRate, in millihertz.
     */
    void setRefreshRate(int refreshRate);

    /**
     * Schedules a compositing cycle at the next available moment.
     */
    void scheduleRepaint();

    /**
     * Returns the timestamp of the last frame that has been presented on the screen.
     */
    std::chrono::nanoseconds lastPresentationTimestamp() const;

    /**
     * If a repaint has been scheduled, this function returns the expected time when
     * the next frame will be presented on the screen.
     */
    std::chrono::nanoseconds nextPresentationTimestamp() const;

Q_SIGNALS:
    /**
     * This signal is emitted when the refresh rate of this RenderLoop has changed.
     */
    void refreshRateChanged();
    /**
     * This signal is emitted when a frame has been actually presented on the screen.
     * @a timestamp indicates the time when it took place.
     */
    void framePresented(RenderLoop *loop, std::chrono::nanoseconds timestamp);

    /**
     * This signal is emitted when the render loop wants a new frame to be composited.
     */
    void frameRequested(RenderLoop *loop);

private:
    QScopedPointer<RenderLoopPrivate> d;
    friend class RenderLoopPrivate;
};

} // namespace KWin
