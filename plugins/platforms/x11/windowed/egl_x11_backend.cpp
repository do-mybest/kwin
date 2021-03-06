/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_x11_backend.h"
// kwin
#include "screens.h"
#include "x11windowed_backend.h"
// kwin libs
#include <kwinglplatform.h>

namespace KWin
{

EglX11Backend::EglX11Backend(X11WindowedBackend *backend)
    : EglOnXBackend(backend->connection(), backend->display(), backend->rootWindow(), backend->screenNumer(), XCB_WINDOW_NONE)
    , m_backend(backend)
{
    setX11TextureFromPixmapSupported(false);
}

EglX11Backend::~EglX11Backend() = default;

void EglX11Backend::cleanupSurfaces()
{
    for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
        eglDestroySurface(eglDisplay(), *it);
    }
}

bool EglX11Backend::createSurfaces()
{
    for (int i = 0; i < screens()->count(); ++i) {
        EGLSurface s = createSurface(m_backend->windowForScreen(i));
        if (s == EGL_NO_SURFACE) {
            return false;
        }
        m_surfaces << s;
    }
    if (m_surfaces.isEmpty()) {
        return false;
    }
    setSurface(m_surfaces.first());
    return true;
}

void EglX11Backend::present()
{
    for (int i = 0; i < screens()->count(); ++i) {
        EGLSurface s = m_surfaces.at(i);
        makeContextCurrent(s);
        setupViewport(i);
        presentSurface(s, screens()->geometry(i), screens()->geometry(i));
    }
    eglWaitGL();
    xcb_flush(m_backend->connection());
}

bool EglX11Backend::usesOverlayWindow() const
{
    return false;
}

QRegion EglX11Backend::beginFrame(int screenId)
{
    makeContextCurrent(m_surfaces.at(screenId));
    setupViewport(screenId);
    return screens()->geometry(screenId);
}

void EglX11Backend::setupViewport(int screenId)
{
    // TODO: ensure the viewport is set correctly each time
    const QSize &overall = screens()->size();
    const QRect &v = screens()->geometry(screenId);
    // TODO: are the values correct?

    qreal scale = screens()->scale(screenId);
    glViewport(-v.x(), v.height() - overall.height() + v.y(), overall.width() * scale, overall.height() * scale);
}

void EglX11Backend::endFrame(int screenId, const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    Q_UNUSED(damagedRegion)
    const QRect &outputGeometry = screens()->geometry(screenId);
    presentSurface(m_surfaces.at(screenId), renderedRegion, outputGeometry);
}

} // namespace
