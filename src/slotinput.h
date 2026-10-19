/**
 * Menu keys 1-9 without binds, ported from Source2Mod's src/core/slotinput.cpp
 * (Copyright (C) 2025-2026 Michal "Slynx" Přikryl, GNU GPL v3 - same licence as this project).
 *
 * The number keys run slot1..slot9 on the client, which never reaches the server as such - only as the weapon the
 * client picked for that slot (usercmd weaponselect). While a living player has a menu open (EconomyShopPlugin sets
 * the keys it takes with zm_menu_keys), every key gets a fake weapon: invisible, never networked to anyone else, and
 * in m_hMyWeapons only while a snapshot is packed. The client picks the fake for the key, the server sees its index
 * in weaponselect, drops the switch and hands the key to the plugin (es_menu_key <slot> <key>).
 *
 * Only works while CS2's own weapon selection HUD is shown (m_iHideHUD bit 1 off): with it hidden the client sends
 * no weaponselect at all (tested 2026-10-19).
 */
#pragma once

#include "platform.h"
#include "playerslot.h"

class CCheckTransmitInfoExtended;
class CCSPlayerController;
class CSGOUserCmdPB;

namespace slotinput
{
	void Shutdown();

	// GameFrame pre, and before any command or usercmd is handled: the fakes leave m_hMyWeapons
	void Eject();
	// GameFrame post: fakes created / dropped for the menus open now
	void OnFrame();
	// CheckTransmit: the fakes go into m_hMyWeapons for this pack, and only their owner gets them
	void Inject();
	void BlockForViewer(CCheckTransmitInfoExtended* pInfo);

	// A usercmd's weaponselect; true when it was a menu key (the switch is dropped)
	bool OnWeaponSelect(CCSPlayerController* pController, CSGOUserCmdPB& cmd);

	// zm_menu_keys: the keys (bit = key 1..9) the player's open menu page takes (per plugin); 0 = no menu
	void SetMenuKeys(int slot, int source, uint32 mask);
	void ResetPlayer(CPlayerSlot slot);
} // namespace slotinput
