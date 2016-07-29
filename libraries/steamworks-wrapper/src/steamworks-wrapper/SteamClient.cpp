//
//  SteamClient.cpp
//  steamworks-wrapper/src/steamworks-wrapper
//
//  Created by Clement Brisset on 6/8/16.
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SteamClient.h"

#include <atomic>

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QMimeData>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtGui/qevent.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif

#include <steam/steam_api.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif


static const Ticket INVALID_TICKET = Ticket();

class SteamTicketRequests {
public:
    SteamTicketRequests();
    ~SteamTicketRequests();

    HAuthTicket startRequest(TicketRequestCallback callback);
    void stopRequest(HAuthTicket authTicket);
    void stopAll();

    STEAM_CALLBACK(SteamTicketRequests, onGetAuthSessionTicketResponse,
                   GetAuthSessionTicketResponse_t, _getAuthSessionTicketResponse);

private:
    struct PendingTicket {
        HAuthTicket authTicket;
        Ticket ticket;
        TicketRequestCallback callback;
    };

    std::vector<PendingTicket> _pendingTickets;
};

SteamTicketRequests::SteamTicketRequests() :
    _getAuthSessionTicketResponse(this, &SteamTicketRequests::onGetAuthSessionTicketResponse)
{
}

SteamTicketRequests::~SteamTicketRequests() {
    stopAll();
}

HAuthTicket SteamTicketRequests::startRequest(TicketRequestCallback callback) {
    static const uint32 MAX_TICKET_SIZE { 1024 };
    uint32 ticketSize { 0 };
    char ticket[MAX_TICKET_SIZE];

    auto authTicket = SteamUser()->GetAuthSessionTicket(ticket, MAX_TICKET_SIZE, &ticketSize);
    qDebug() << "Got Steam auth session ticket:" << authTicket;

    if (authTicket == k_HAuthTicketInvalid) {
        qWarning() << "Auth session ticket is invalid.";
        callback(INVALID_TICKET);
    } else {
        PendingTicket pendingTicket{ authTicket, QByteArray(ticket, ticketSize).toHex(), callback };
        _pendingTickets.push_back(pendingTicket);
    }

    return authTicket;
}

void SteamTicketRequests::stopRequest(HAuthTicket authTicket) {
    auto it = std::find_if(_pendingTickets.begin(), _pendingTickets.end(), [&authTicket](const PendingTicket& pendingTicket) {
        return pendingTicket.authTicket == authTicket;
    });

    if (it != _pendingTickets.end()) {
        SteamUser()->CancelAuthTicket(it->authTicket);
        it->callback(INVALID_TICKET);
        _pendingTickets.erase(it);
    }
}

void SteamTicketRequests::stopAll() {
    auto steamUser = SteamUser();
    if (steamUser) {
        for (const auto& pendingTicket : _pendingTickets) {
            steamUser->CancelAuthTicket(pendingTicket.authTicket);
            pendingTicket.callback(INVALID_TICKET);
        }
    }
    _pendingTickets.clear();
}

void SteamTicketRequests::onGetAuthSessionTicketResponse(GetAuthSessionTicketResponse_t* pCallback) {
    auto authTicket = pCallback->m_hAuthTicket;

    auto it = std::find_if(_pendingTickets.begin(), _pendingTickets.end(), [&authTicket](const PendingTicket& pendingTicket) {
        return pendingTicket.authTicket == authTicket;
    });


    if (it != _pendingTickets.end()) {

        if (pCallback->m_eResult == k_EResultOK) {
            qDebug() << "Got steam callback, auth session ticket is valid. Send it." << authTicket;
            it->callback(it->ticket);
        } else {
            qWarning() << "Steam auth session ticket callback encountered an error:" << pCallback->m_eResult;
            it->callback(INVALID_TICKET);
        }

        _pendingTickets.erase(it);
    } else {
        qWarning() << "Could not find steam auth session ticket in list of pending tickets:" << authTicket;
    }
}


const QString CONNECT_PREFIX = "--url \"";
const QString CONNECT_SUFFIX = "\"";

class SteamCallbackManager {
public:
    SteamCallbackManager();

    STEAM_CALLBACK(SteamCallbackManager, onGameRichPresenceJoinRequested,
                   GameRichPresenceJoinRequested_t, _gameRichPresenceJoinRequestedResponse);

    STEAM_CALLBACK(SteamCallbackManager, onLobbyCreated,
                   LobbyCreated_t, _lobbyCreatedResponse);

    STEAM_CALLBACK(SteamCallbackManager, onGameLobbyJoinRequested,
                   GameLobbyJoinRequested_t, _gameLobbyJoinRequestedResponse);

    STEAM_CALLBACK(SteamCallbackManager, onLobbyEnter,
                   LobbyEnter_t, _lobbyEnterResponse);

    SteamTicketRequests& getTicketRequests() { return _steamTicketRequests; }

private:
    SteamTicketRequests _steamTicketRequests;
};

SteamCallbackManager::SteamCallbackManager() :
    _gameRichPresenceJoinRequestedResponse(this, &SteamCallbackManager::onGameRichPresenceJoinRequested),
    _lobbyCreatedResponse(this, &SteamCallbackManager::onLobbyCreated),
    _gameLobbyJoinRequestedResponse(this, &SteamCallbackManager::onGameLobbyJoinRequested),
    _lobbyEnterResponse(this, &SteamCallbackManager::onLobbyEnter)
{
}

void SteamCallbackManager::onGameRichPresenceJoinRequested(GameRichPresenceJoinRequested_t* pCallback) {
    auto url = QString::fromLocal8Bit(pCallback->m_rgchConnect);

    if (url.startsWith(CONNECT_PREFIX) && url.endsWith(CONNECT_SUFFIX)) {
        url.remove(0, CONNECT_PREFIX.size());
        url.remove(-CONNECT_SUFFIX.size(), CONNECT_SUFFIX.size());
    }

    qDebug() << "Joining Steam Friend at:" << url;
    auto mimeData = new QMimeData();
    mimeData->setUrls(QList<QUrl>() << QUrl(url));
    auto event = new QDropEvent(QPointF(0,0), Qt::MoveAction, mimeData, Qt::LeftButton, Qt::NoModifier);

    QCoreApplication::postEvent(qApp, event);
}

void SteamCallbackManager::onLobbyCreated(LobbyCreated_t* pCallback) {
    qDebug() << pCallback->m_eResult << pCallback->m_ulSteamIDLobby;
    if (pCallback->m_eResult == k_EResultOK) {
        qDebug() << "Inviting steam friends";

        SteamMatchmaking()->SetLobbyData(pCallback->m_ulSteamIDLobby, "connect",
                                         SteamFriends()->GetFriendRichPresence(SteamUser()->GetSteamID(), "connect"));
        SteamMatchmaking()->SetLobbyMemberData(pCallback->m_ulSteamIDLobby,
                                               "Creator", "true");
        SteamFriends()->ActivateGameOverlayInviteDialog(pCallback->m_ulSteamIDLobby);
    }
}

void SteamCallbackManager::onGameLobbyJoinRequested(GameLobbyJoinRequested_t* pCallback) {
    qDebug() << "onGameLobbyJoinRequested";
    SteamMatchmaking()->JoinLobby(pCallback->m_steamIDLobby);
}

void SteamCallbackManager::onLobbyEnter(LobbyEnter_t* pCallback) {
    qDebug() << "onLobbyEnter";
    auto creator = SteamMatchmaking()->GetLobbyMemberData(pCallback->m_ulSteamIDLobby, SteamUser()->GetSteamID(), "creator");
    if (strcmp(creator, "true") == 0) {
        qDebug() << "Created lobby";
        SteamMatchmaking()->LeaveLobby(pCallback->m_ulSteamIDLobby);
    } else if (pCallback->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess) {
        qDebug() << "Success";
        auto connectValue = SteamMatchmaking()->GetLobbyData(pCallback->m_ulSteamIDLobby, "connect");
        qDebug() << connectValue;
    }
}


static std::atomic_bool initialized { false };
static SteamCallbackManager steamCallbackManager;


bool SteamClient::isRunning() {
    if (!initialized) {
        init();
    }
    return initialized;
}

bool SteamClient::init() {
    if (SteamAPI_IsSteamRunning() && !initialized) {
        initialized = SteamAPI_Init();
    }
    return initialized;
}

void SteamClient::shutdown() {
    if (initialized) {
        SteamAPI_Shutdown();
    }

    steamCallbackManager.getTicketRequests().stopAll();
}

void SteamClient::runCallbacks() {
    if (!initialized) {
        return;
    }

    auto steamPipe = SteamAPI_GetHSteamPipe();
    if (!steamPipe) {
        qDebug() << "Could not get SteamPipe";
        return;
    }

    Steam_RunCallbacks(steamPipe, false);
}

void SteamClient::requestTicket(TicketRequestCallback callback) {
    if (!initialized) {
        if (SteamAPI_IsSteamRunning()) {
            init();
        } else {
            qWarning() << "Steam is not running";
            callback(INVALID_TICKET);
            return;
        }
    }

    if (!initialized) {
        qDebug() << "Steam not initialized";
        return;
    }

    steamCallbackManager.getTicketRequests().startRequest(callback);
}

void SteamClient::updateLocation(QString status, QUrl locationUrl) {
    if (!initialized) {
        return;
    }

    auto connectStr = locationUrl.isEmpty() ? "" : CONNECT_PREFIX + locationUrl.toString() + CONNECT_SUFFIX;

    SteamFriends()->SetRichPresence("status", status.toLocal8Bit().data());
    SteamFriends()->SetRichPresence("connect", connectStr.toLocal8Bit().data());
}

void SteamClient::openInviteOverlay() {
    if (!initialized) {
        return;
    }

    qDebug() << "Creating steam lobby";
    static const int MAX_LOBBY_SIZE = 20;
    SteamMatchmaking()->CreateLobby(k_ELobbyTypePrivate, MAX_LOBBY_SIZE);
}
