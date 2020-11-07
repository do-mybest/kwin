/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "xdgactivationv1.h"
#include "wayland_server.h"
#include <KWaylandServer/display.h>
#include <KWaylandServer/surface_interface.h>
#include <KWaylandServer/xdgactivation_v1_interface.h>
#include "workspace.h"
#include "utils.h"
#include "abstract_client.h"

Q_DECLARE_LOGGING_CATEGORY(KWIN_ACTIVATION)
Q_LOGGING_CATEGORY(KWIN_ACTIVATION, "kwin_xdgactivationv1_integration")

using namespace KWaylandServer;

namespace KWin
{

XdgActivationV1Integration::XdgActivationV1Integration(QObject *parent)
    : QObject(parent)
{
    auto activation = waylandServer()->display()->createXdgActivationV1(waylandServer()->display());

    Workspace *ws = Workspace::self();
    connect(ws, &Workspace::clientActivated, this, [this] {
        m_currentActivationToken.reset();
    });
    activation->setActivationTokenCreator([this] (KWaylandServer::SurfaceInterface *surface, uint serial, KWaylandServer::SeatInterface *seat) -> QString {
        Workspace *ws = Workspace::self();
        if (ws->activeClient()->surface() != surface) {
            qCWarning(KWIN_ACTIVATION) << "Inactive surfaces cannot be granted a token";
            return {};
        }

        static int i = 0;
        const auto newToken = QStringLiteral("kwin-%1").arg(++i);

        m_currentActivationToken.reset(new ActivationToken{ newToken, surface, serial, seat, {} });

        return newToken;
    });
    connect(activation, &KWaylandServer::XdgActivationV1Interface::associate, this, [this] (ClientConnection *client, const QString &token, const QString &appId) {
        if (!m_currentActivationToken || m_currentActivationToken->token != token) {
            qCWarning(KWIN_ACTIVATION) << "Could not find the token" << token << "launching" << appId;
            return;
        }

        qCDebug(KWIN_ACTIVATION) << "according to" << client << "token:" << token << "is launching" << appId;
        m_currentActivationToken->applicationId = appId;
    });
    connect(activation, &KWaylandServer::XdgActivationV1Interface::activate, this, &XdgActivationV1Integration::activateSurface);
    connect(activation, &KWaylandServer::XdgActivationV1Interface::setActivation, this, [this] (ClientConnection *c, const QString &token) {
        if (m_currentActivationToken && m_currentActivationToken->token == token) {
            c->setProperty("activationToken", token);
        } else {
            qCWarning(KWIN_ACTIVATION) << "Setting the wrong token" << token;
        }
    });
}

void XdgActivationV1Integration::activateSurface(ClientConnection *c, SurfaceInterface* surface)
{
    Workspace *ws = Workspace::self();
    auto client = ws->findAbstractClient([surface] (const Toplevel* toplevel) {
        return toplevel->surface() == surface;
    });
    if (!client) {
        qCWarning(KWIN_ACTIVATION) << "could not find the token for" << surface;
        return;
    }

    const auto token = c->property("activationToken").toString();
    if (token.isEmpty()) {
        qCWarning(KWIN_ACTIVATION) << "no token for" << c;
        return;
    }
    if (!m_currentActivationToken || m_currentActivationToken->token != token) {
        qCWarning(KWIN_ACTIVATION) << "token not set" << token;
        return;
    }

    auto ownerSurfaceClient = waylandServer()->findClient(m_currentActivationToken->surface);

    qCDebug(KWIN_ACTIVATION) << "activating" << client << surface << "on behalf of" << m_currentActivationToken->surface << "into" << ownerSurfaceClient;
    if (ownerSurfaceClient->desktopFileName() == m_currentActivationToken->applicationId
        || m_currentActivationToken->applicationId.isEmpty()
        || ownerSurfaceClient->desktopFileName().isEmpty()
    ) {
        ws->activateClient(client);
    } else {
        qCWarning(KWIN_ACTIVATION) << "Activation requested while owner isn't active" << ownerSurfaceClient->desktopFileName() << m_currentActivationToken->applicationId;
        client->demandAttention();
    }
}

}
