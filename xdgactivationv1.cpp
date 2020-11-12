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
#include "effects.h"
#include "abstract_client.h"

Q_DECLARE_LOGGING_CATEGORY(KWIN_ACTIVATION)
Q_LOGGING_CATEGORY(KWIN_ACTIVATION, "kwin_xdgactivationv1_integration")

using namespace KWaylandServer;

namespace KWin
{

class ActivationTracker : public QObject
{
public:
    void update(const QString &token, const QString &appId) {
        const auto iconName = AbstractClient::iconFromDesktopFile(appId);
        const auto icon = QIcon::fromTheme(iconName, QIcon::fromTheme(QStringLiteral("system-run")));

        const auto it = m_activating.find(token);
        if (it != m_activating.end()) {
            *it = icon;
            Q_EMIT effects->startupAdded(token.toUtf8(), icon);
        } else {
            m_activating[token] = icon;
            Q_EMIT effects->startupChanged(token.toUtf8(), icon);
        }
    }

    void remove(const QString &token) {
        auto removed = m_activating.remove(token);
        if (removed) {
            effects->startupRemoved(token);
        }
    }

    static ActivationTracker* self() {
        static ActivationTracker* s_self = nullptr;

        if (!s_self) {
            s_self = new ActivationTracker;
        }
        return s_self;
    }

private:
    QHash<QString, QIcon> m_activating;
};

XdgActivationV1Integration::XdgActivationV1Integration(QObject *parent)
    : QObject(parent)
{
    auto activation = waylandServer()->display()->createXdgActivationV1(waylandServer()->display());

    Workspace *ws = Workspace::self();
    connect(ws, &Workspace::clientActivated, this, [this] (AbstractClient *client) {
        m_currentActivationToken.reset();

        auto waylandClient = client->surface()->client();
        const auto token = waylandClient->property("activationToken").toString();
        if (!token.isEmpty()) {
            ActivationTracker::self()->remove(token);
        }
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
        ActivationTracker::self()->update(newToken, {});

        return newToken;
    });
    connect(activation, &KWaylandServer::XdgActivationV1Interface::associate, this, [this] (ClientConnection *client, const QString &token, const QString &appId) {
        if (!m_currentActivationToken || m_currentActivationToken->token != token) {
            qCWarning(KWIN_ACTIVATION) << "Could not find the token" << token << "launching" << appId;
            return;
        }

        qCDebug(KWIN_ACTIVATION) << "according to" << client << "token:" << token << "is launching" << appId;
        m_currentActivationToken->applicationId = appId;

        ActivationTracker::self()->update(token, appId);
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
