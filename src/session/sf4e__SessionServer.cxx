#include <windows.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <GameNetworkingSockets/steam/isteamnetworkingutils.h>

#include "../Dimps/Dimps.hxx"
#include "../Dimps/Dimps__Event.hxx"
#include "../Dimps/Dimps__GameEvents.hxx"
#include "../sf4e/sf4e__GameEvents.hxx"
#include "sf4e__SessionProtocol.hxx"
#include "sf4e__SessionServer.hxx"

using nlohmann::json;

namespace SessionProtocol = sf4e::SessionProtocol;
using Dimps::Event::EventBase;
using Dimps::Event::EventController;
using rVsMode = Dimps::GameEvents::VsMode;
using fMainMenu = sf4e::GameEvents::MainMenu;
using fVsMode = sf4e::GameEvents::VsMode;
using fVsPreBattle = sf4e::GameEvents::VsPreBattle;
using sf4e::SessionServer;

SessionServer::SessionServer(uint16 nPort)
{
	myConditions = { 0 };
	myStageID = 0;
	memset(confirmedConditions, 0, sizeof(rVsMode::ConfirmedCharaConditions));
	confirmedStageID = 0;

	m_pInterface = SteamNetworkingSockets();
	m_hClient = k_HSteamNetConnection_Invalid;

	// Start listening
	SteamNetworkingIPAddr serverLocalAddr;
	serverLocalAddr.Clear();
	serverLocalAddr.m_port = nPort;
	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);
	m_hListenSock = m_pInterface->CreateListenSocketIP(serverLocalAddr, 1, &opt);
	if (m_hListenSock == k_HSteamListenSocket_Invalid) {
		spdlog::error("Failed to listen on port {}", nPort);
		return;
	}
	spdlog::info("Server listening on port {}", nPort);
}

void SessionServer::Step()
{
	PollIncomingMessages();
	PollConnectionStateChanges();
	SendQueuedMessages();
}

void SessionServer::Close()
{
	json shutdownMsg;
	shutdownMsg["type"] = "shutdown";

	spdlog::info("Closing connections...");
	// Close the connection.  We use "linger mode" to ask SteamNetworkingSockets
	// to flush this out and close gracefully.
	m_pInterface->CloseConnection(m_hClient, 0, "Server Shutdown", true);
	m_hClient = k_HSteamNetConnection_Invalid;
	m_pInterface->CloseListenSocket(m_hListenSock);
}


void SessionServer::PollIncomingMessages()
{
	// Nothing to do- no remote client.
	if (m_hClient == k_HSteamNetConnection_Invalid) {
		return;
	}

	ISteamNetworkingMessage* pIncomingMsgs[SESSION_SERVER_MAX_MESSAGES_PER_POLL] = { 0 };
	int numMsgs = m_pInterface->ReceiveMessagesOnConnection(m_hClient, pIncomingMsgs, SESSION_SERVER_MAX_MESSAGES_PER_POLL);

	if (numMsgs < 0) {
		spdlog::error("Session client error checking for messages: {}", numMsgs);
		return;
	}

	for (int i = 0; i < numMsgs; i++) {
		ISteamNetworkingMessage* pIncomingMsg = pIncomingMsgs[i];
		if (!pIncomingMsg) {
			spdlog::error("Client: incoming message enumerated, but not data retrieved");
			return;
		}

		const char* start = (const char*)pIncomingMsg->m_pData;
		json msg = json::parse(start, start + pIncomingMsg->m_cbSize);
		pIncomingMsg->Release();

		std::string type;
		try {
			msg.at("type").get_to(type);
		}
		catch (json::exception e) {
			spdlog::info("Server: got a message without a type, or a type that was not a string");
			continue;
		}

		if (type == "request") {
			// Since handling a request forces the process to load into a battle,
			// handling the request can only reasonably be done if the process is
			// currently on the main menu.
			if (fMainMenu::instance == NULL) {
				spdlog::info("Server: ignoring request because we're not on the main menu");
				continue;
			}

			SessionProtocol::SessionRequest request;
			try {
				msg.get_to(request);
			}
			catch (json::exception e) {
				spdlog::info("Server: could not deserialize request");
				continue;
			}
			
			size_t charaConditionSize = sizeof(rVsMode::ConfirmedCharaConditions);
			memcpy_s(&confirmedConditions[1], charaConditionSize, &request.chara, charaConditionSize);

			SessionProtocol::SessionResponse response;
			memcpy_s(&confirmedConditions[0], charaConditionSize, &myConditions, charaConditionSize);
			confirmedStageID = myStageID;
			memcpy_s(&response.chara, charaConditionSize, &myConditions, charaConditionSize);
			response.stageID = myStageID;
			json responseMsg = response;
			queuedMessages.push_back(responseMsg);

			s_pCallbackInstance = this;
			fVsPreBattle::bSkipToVersus = true;
			fVsPreBattle::OnTasksRegistered = _OnVsPreBattleTasksRegistered;
			fMainMenu::GoToVersusBattle();
		}
		else {
			spdlog::warn("Server: got unrecognized message type: {}", type);
		}
	}
}

void SessionServer::SendQueuedMessages() {
	std::string buf;

	for (auto iter = queuedMessages.begin(); iter != queuedMessages.end(); iter++) {
		buf = iter->dump();
		m_pInterface->SendMessageToConnection(
			m_hClient, buf.c_str(), (uint32)buf.length(),
			k_nSteamNetworkingSend_Reliable, nullptr
		);
	}

	queuedMessages.clear();
}

void SessionServer::_OnVsPreBattleTasksRegistered()
{
	SessionServer* _this = s_pCallbackInstance;
	size_t charaConditionSize = sizeof(rVsMode::ConfirmedCharaConditions);

	// XXX (adanducci): this is fragile- passing in the VsPreBattle event and
	// traversing the parent events would avoid the need to track state that
	// could potentially interleave with other event construction and
	// destruction.
	rVsMode* mode = fVsMode::instance;
	Dimps::Platform::dString* stageName = rVsMode::GetStageName(mode);
	rVsMode::ConfirmedPlayerConditions* conditions = rVsMode::GetConfirmedPlayerConditions(mode);
	for (int i = 0; i < 2; i++) {
		*(rVsMode::ConfirmedPlayerConditions::GetCharaID(&conditions[i])) = _this->confirmedConditions[i].charaID;
		*(rVsMode::ConfirmedPlayerConditions::GetSideActive(&conditions[i])) = 1;
		rVsMode::ConfirmedCharaConditions* charaConditions = rVsMode::ConfirmedPlayerConditions::GetCharaConditions(&conditions[i]);
		memcpy_s(charaConditions, charaConditionSize, &_this->confirmedConditions[i], charaConditionSize);
	}

	(stageName->*Dimps::Platform::dString::publicMethods.assign)(Dimps::stageCodes[_this->confirmedStageID], 4);
	*(rVsMode::GetStageCode(mode)) = _this->confirmedStageID;
}

void SessionServer::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	switch (pInfo->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
	{
		// Ignore if they were not previously connected.  (If they disconnected
		// before we accepted the connection.)
		if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connected)
		{
			// Select appropriate log messages
			const char* pszDebugLogAction;
			if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
			{
				pszDebugLogAction = "problem detected locally";
			}
			else
			{
				// Note that here we could check the reason code to see if
				// it was a "usual" connection or an "unusual" one.
				pszDebugLogAction = "closed by peer";
			}

			spdlog::info("Connection {} {}, reason {}: {}",
				pInfo->m_info.m_szConnectionDescription,
				pszDebugLogAction,
				pInfo->m_info.m_eEndReason,
				pInfo->m_info.m_szEndDebug
			);
		}

		// Clean up the connection.  This is important!
		// The connection is "closed" in the network sense, but
		// it has not been destroyed.  We must close it on our end, too
		// to finish up.  The reason information do not matter in this case,
		// and we cannot linger because it's already closed on the other end,
		// so we just pass 0's.
		m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
		break;
	}

	case k_ESteamNetworkingConnectionState_Connecting:
	{
		// Try to accept the connection.
		if (m_pInterface->AcceptConnection(pInfo->m_hConn) != k_EResultOK)
		{
			// This could fail.  If the remote host tried to connect, but then
			// disconnected, the connection may already be half closed.  Just
			// destroy whatever we have on our side.
			m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
			spdlog::error("Can't accept connection.  (It was already closed?)");
			break;
		}
		m_hClient = pInfo->m_hConn;
		break;
	}

	default:
		break;
	}
}

SessionServer* SessionServer::s_pCallbackInstance;

void SessionServer::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	s_pCallbackInstance->OnSteamNetConnectionStatusChanged(pInfo);
}

void SessionServer::PollConnectionStateChanges()
{
	s_pCallbackInstance = this;
	m_pInterface->RunCallbacks();
}