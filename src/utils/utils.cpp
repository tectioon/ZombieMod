/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2026 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include "../cs2fixes.h"
#include "../gameconfig.h"

void Message(const char* msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[1024] = {};
	V_vsnprintf(buf, sizeof(buf) - 1, msg, args);

	ConColorMsg(Color(255, 0, 255, 255), "[CS2Fixes] %s", buf);

	va_end(args);
}

void Panic(const char* msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[1024] = {};
	V_vsnprintf(buf, sizeof(buf) - 1, msg, args);

	Warning("[CS2Fixes] %s", buf);

	va_end(args);
}

CUtlVector<CServerSideClient*>* GetClientList()
{
	// TEMPORARY crash-diagnostic logging (2026-09-23 post-CS2-update investigation) - narrowed the
	// crash down to somewhere in GetClientBySlot via ConMsg bracketing in playermanager.cpp; this
	// pins down whether it's the offset lookup, the pointer arithmetic, or something after. Remove
	// once the actual culprit is found and fixed.
	INetworkGameServer* pServer = GetNetworkGameServer();
	ConMsg("[CrashDebug] GetClientList: pServer=%p sizeof(INetworkGameServer)=%zu\n", (void*)pServer, sizeof(INetworkGameServer));
	if (!pServer)
		return nullptr;

	static int offset = g_GameConfig->GetOffset("CNetworkGameServer_ClientList");
	ConMsg("[CrashDebug] GetClientList: offset=%d\n", offset);
	CUtlVector<CServerSideClient*>* pResult = (CUtlVector<CServerSideClient*>*)(&pServer[offset]);
	ConMsg("[CrashDebug] GetClientList: computed pResult=%p, about to return\n", (void*)pResult);
	return pResult;
}

CServerSideClient* GetClientBySlot(CPlayerSlot slot)
{
	ConMsg("[CrashDebug] GetClientBySlot: start\n");
	CUtlVector<CServerSideClient*>* pClients = GetClientList();
	ConMsg("[CrashDebug] GetClientBySlot: GetClientList returned %p\n", (void*)pClients);

	if (!pClients)
		return nullptr;

	CServerSideClient* pResult = pClients->Element(slot.Get());
	ConMsg("[CrashDebug] GetClientBySlot: Element() returned %p\n", (void*)pResult);
	return pResult;
}

uint32 GetSoundEventHash(const char* pszSoundEventName)
{
	return MurmurHash2LowerCase(pszSoundEventName, 0x53524332);
}

std::string StringToLower(std::string strValue)
{
	for (int i = 0; strValue[i]; i++)
		strValue[i] = tolower(strValue[i]);

	return strValue;
}

ISteamUGC* GetSteamUGC()
{
	if (g_pEngineServer2->IsDedicatedServer())
		return SteamGameServerUGC();
	else
		return SteamUGC();
}

ISteamHTTP* GetSteamHTTP()
{
	if (g_pEngineServer2->IsDedicatedServer())
		return SteamGameServerHTTP();
	else
		return SteamHTTP();
}