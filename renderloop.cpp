/*
    SPDX-FileCopyrightText: 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "renderloop.h"
#include "options.h"
#include "renderloop_p.h"
#include "utils.h"

namespace KWin
{

RenderLoopPrivate *RenderLoopPrivate::get(RenderLoop *loop)
{
    return loop->d.data();
}

RenderLoopPrivate::RenderLoopPrivate(RenderLoop *q)
    : q(q)
{
    compositeTimer.setSingleShot(true);
    QObject::connect(&compositeTimer, &QTimer::timeout, q, [this]() { emitFrameRequested(); });
}

template <typename T>
T align(const T &timestamp, const T &alignment)
{
    return timestamp + ((alignment - (timestamp % alignment)) % alignment);
}

void RenderLoopPrivate::scheduleRepaint()
{
    if (compositeTimer.isActive()) {
        return;
    }

    const std::chrono::nanoseconds currentTime(std::chrono::steady_clock::now().time_since_epoch());
    const std::chrono::nanoseconds vblankInterval(1'000'000'000'000ull / refreshRate);

    // Estimate when the next presentation will occur. Note that this is a prediction.
    nextPresentationTimestamp = lastPresentationTimestamp
            + align(currentTime - lastPresentationTimestamp, vblankInterval);

    // Estimate when it's a good time to perform the next compositing cycle.
    const std::chrono::nanoseconds safetyMargin = std::chrono::milliseconds(3);

    std::chrono::nanoseconds renderTime;
    switch (options->latencyPolicy()) {
    case LatencyExteremelyLow:
        renderTime = std::chrono::nanoseconds(long(vblankInterval.count() * 0.1));
        break;
    case LatencyLow:
        renderTime = std::chrono::nanoseconds(long(vblankInterval.count() * 0.25));
        break;
    case LatencyMedium:
        renderTime = std::chrono::nanoseconds(long(vblankInterval.count() * 0.5));
        break;
    case LatencyHigh:
        renderTime = std::chrono::nanoseconds(long(vblankInterval.count() * 0.75));
        break;
    case LatencyExtremelyHigh:
        renderTime = std::chrono::nanoseconds(long(vblankInterval.count() * 0.9));
        break;
    }

    std::chrono::nanoseconds nextRenderTimestamp = nextPresentationTimestamp - renderTime - safetyMargin;

    // If we can't render the frame before the deadline, start compositing immediately.
    if (nextRenderTimestamp < currentTime) {
        nextRenderTimestamp = currentTime;
    }

    const std::chrono::nanoseconds waitInterval = nextRenderTimestamp - currentTime;
    compositeTimer.start(std::chrono::duration_cast<std::chrono::milliseconds>(waitInterval));
}

void RenderLoopPrivate::delayScheduleRepaint()
{
    pendingReschedule = true;
}

void RenderLoopPrivate::maybeScheduleRepaint()
{
    if (pendingReschedule) {
        scheduleRepaint();
        pendingReschedule = false;
    }
}

void RenderLoopPrivate::notifyFrameCompleted(std::chrono::nanoseconds timestamp)
{
    Q_ASSERT(pendingFrameCount > 0);
    pendingFrameCount--;

    if (lastPresentationTimestamp <= timestamp) {
        lastPresentationTimestamp = timestamp;
    } else {
        qCWarning(KWIN_CORE, "Got invalid presentation timestamp: %ld (current %ld)",
                  timestamp.count(), lastPresentationTimestamp.count());
        lastPresentationTimestamp = std::chrono::steady_clock::now().time_since_epoch();
    }

    maybeScheduleRepaint();

    emit q->framePresented(q, timestamp);
}

void RenderLoopPrivate::emitFrameRequested()
{
    pendingRepaint = true;

    emit q->frameRequested(q);
}

RenderLoop::RenderLoop(QObject *parent)
    : QObject(parent)
    , d(new RenderLoopPrivate(this))
{
    qRegisterMetaType<std::chrono::nanoseconds>();
}

RenderLoop::~RenderLoop()
{
}

void RenderLoop::inhibit()
{
    d->inhibitCount++;

    if (d->inhibitCount == 1) {
        d->compositeTimer.stop();
    }
}

void RenderLoop::uninhibit()
{
    Q_ASSERT(d->inhibitCount > 0);
    d->inhibitCount--;

    if (d->inhibitCount == 0) {
        d->maybeScheduleRepaint();
    }
}

void RenderLoop::beginFrame()
{
    d->pendingFrameCount++;
    d->pendingRepaint = false;
}

void RenderLoop::endFrame()
{
}

void RenderLoop::discardFrame()
{
    d->pendingRepaint = false;
}

int RenderLoop::refreshRate() const
{
    return d->refreshRate;
}

void RenderLoop::setRefreshRate(int refreshRate)
{
    if (d->refreshRate == refreshRate) {
        return;
    }
    d->refreshRate = refreshRate;
    emit refreshRateChanged();
}

void RenderLoop::scheduleRepaint()
{
    if (d->pendingRepaint) {
        return;
    }
    if (!d->pendingFrameCount && !d->inhibitCount) {
        d->scheduleRepaint();
    } else {
        d->delayScheduleRepaint();
    }
}

std::chrono::nanoseconds RenderLoop::lastPresentationTimestamp() const
{
    return d->lastPresentationTimestamp;
}

std::chrono::nanoseconds RenderLoop::nextPresentationTimestamp() const
{
    return d->nextPresentationTimestamp;
}

} // namespace KWin
