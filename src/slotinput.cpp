/**
 * Menu keys 1-9 without binds, ported from Source2Mod's src/core/slotinput.cpp
 * (Copyright (C) 2025-2026 Michal "Slynx" Přikryl, GNU GPL v3 - same licence as this project).
 * See slotinput.h. Differences from Source2Mod: the menus live in EconomyShopPlugin (C#), which tells us the keys of
 * the open page with zm_menu_keys and gets the keys back as the server command es_menu_key; players who bound
 * "css_N; slotN" get both the command and the fake's key, and the plugin drops the second of the two.
 */
#include "slotinput.h"

#include "cchecktransmitinfo.h"
#include "commands.h"
#include "common.h"
#include "cs2fixes.h"
#include "cs_usercmd.pb.h"
#include "entity/ccsplayercontroller.h"
#include "entity/ccsplayerpawn.h"
#include "entity/ccsweaponbase.h"
#include "entity/services.h"
#include "gameconfig.h"
#include "playermanager.h"
#include "utils/entity.h"
#include <algorithm>
#include <vector>

#include "tier0/memdbgon.h"

CConVar<bool> g_cvarMenuSlotKeys("zm_menu_slotkeys", FCVAR_NONE, "Menu keys 1-9 without binds (fake weapons, see slotinput.cpp)", true);
CConVar<bool> g_cvarMenuSlotKeysDormant("zm_menu_slotkeys_dormant", FCVAR_NONE, "Keep the menu-key fakes dormant on the owner's client after a few sends, so CS2's weapon list and the wheel skip them (crashed the owner's game twice on 2026-10-19)", false);

namespace slotinput
{
	namespace
	{
		struct KeyDef
		{
			int Key;
			const char* Classname;
			uint16 DefIndex;	  // item definition index
			const char* Subclass; // the vdata the fake gets (m_nSubclassID)
		};

		// How the client finds the weapon for each key (CCSHudWeaponSelection):
		//   1..5 -> vdata m_GearSlot == key-1 with m_GearSlotPosition in 0..20, lowest position wins, the LAST one on a tie
		//   6..9 -> econ item definition (44 HE, 43 flash, 45 smoke, 47 decoy), the first match in m_hMyWeapons
		// 6..9 are found by definition only, so their fakes carry the knife's vdata: gear slot knife, where the player
		// always has a real knife. "4" needs the econ HE vdata "44" (grenade position 0..3; the class-named
		// "weapon_hegrenade" has -1 and is never found by the slot). Inject() puts 6..9 in front of the real weapons
		// and 1..5 behind them, so every key finds its fake before any real weapon.
		constexpr KeyDef kKeys[] = {
			{1, "weapon_ak47", 7, "weapon_ak47"},
			// Any pistol does (gear slot 1); the client plays the fake's draw sound when it predicts the switch
			{2, "weapon_p250", 36, "weapon_p250"},
			{3, "weapon_knife", 42, "weapon_knife"},
			{6, "weapon_hegrenade", 44, "weapon_knife"},
			{4, "weapon_hegrenade", 44, "44"},
			{5, "weapon_c4", 49, "weapon_c4"},
			{7, "weapon_flashbang", 43, "weapon_knife"},
			{8, "weapon_smokegrenade", 45, "weapon_knife"},
			{9, "weapon_decoy", 47, "weapon_knife"},
		};

		struct Fake
		{
			int Key;
			CHandle<CBasePlayerWeapon> Weapon;
			// Snapshots its owner has been sent it in; after a few it is kept dormant on the owner's client
			int Sent = 0;
		};

		constexpr int kSendsBeforeDormant = 3;

		struct Session
		{
			bool Active = false;
			CHandle<CCSPlayerPawn> Pawn;
			std::vector<Fake> Fakes;
			bool Injected = false;
			uint32 FailedKeys = 0; // keys whose fake could not be created - not retried every frame
			uint32 MenuKeys = 0;   // what the open menu page does something with (bit = key)
			uint64 SentSignature = 0;
			int Handover = 0; // frames left before the fakes go
		};

		Session g_Sessions[MAXPLAYERS];
		constexpr int kMenuSources = 4;		// one per plugin with menus (EconomyShopPlugin 0, MapVotePlugin 1)
		uint32 g_MenuKeys[MAXPLAYERS][kMenuSources]; // set by the plugins with zm_menu_keys
		bool g_bAnyInjected = false;

		// NetworkStateChanged for the whole of m_hMyWeapons: its size AND every element. The plain field NSC only
		// sends the size, so a list that kept its length but changed its contents stayed on the client as it was.
		void MarkListChanged(CPlayer_WeaponServices* pWeaponServices)
		{
			static const int16_t chainOffset = schema::FindChainOffset("CPlayer_WeaponServices", hash_32_fnv1a_const("CPlayer_WeaponServices"));
			static const SchemaKey key = schema::GetOffset("CPlayer_WeaponServices", hash_32_fnv1a_const("CPlayer_WeaponServices"),
														   "m_hMyWeapons", hash_32_fnv1a_const("m_hMyWeapons"));
			if (chainOffset == 0 || key.offset <= 0)
				return;

			auto* pChainer = reinterpret_cast<CNetworkVarChainer*>(reinterpret_cast<uintptr_t>(pWeaponServices) + chainOffset);
			CEntityInstance* pEntity = pChainer->m_pEntity;
			if (!pEntity)
				return;

			pEntity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(key.offset), -1, pChainer->m_PathIndex));
			int count = pWeaponServices->m_hMyWeapons()->Count();
			for (int i = 0; i < count; i++)
				pEntity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(key.offset), i, pChainer->m_PathIndex));
		}

		CPlayer_WeaponServices* WeaponServicesOf(Session& s)
		{
			CCSPlayerPawn* pPawn = s.Pawn.Get();
			return pPawn ? pPawn->m_pWeaponServices() : nullptr;
		}

		// CBaseEntity::UpdateSubclassVData: looks the vdata up for the entity's m_nSubclassID and stores it. A bare
		// CreateEntityByName leaves the vdata null and CCSWeaponBase::Spawn reads it straight away.
		using UpdateSubclassVData_t = void (*)(CBaseEntity* pEntity);

		UpdateSubclassVData_t GetUpdateSubclassVData()
		{
			static UpdateSubclassVData_t fn = [] {
				auto found = (UpdateSubclassVData_t)g_GameConfig->ResolveSignature("CBaseEntity::UpdateSubclassVData");
				if (!found)
					Panic("SlotInput: CBaseEntity::UpdateSubclassVData not found; menu keys need binds\n");
				return found;
			}();
			return fn;
		}

		CBasePlayerWeapon* CreateFake(CCSPlayerPawn* pPawn, const KeyDef& def)
		{
			UpdateSubclassVData_t updateSubclassVData = GetUpdateSubclassVData();
			if (!updateSubclassVData)
				return nullptr;

			// Always a knife, carrying the vdata of the weapon the key stands for, and no owner while the menu is open:
			// the client's HUD lets a knife be selected without an owner, so the key still leaves as weaponselect,
			// and its prediction cannot deploy a weapon without one - no switch on the press. Before the fakes go,
			// they get the pawn as owner for one snapshot (HandOver), or the client keeps them in its slot selection.
			CBasePlayerWeapon* pWeapon = CreateEntityByName<CBasePlayerWeapon>("weapon_knife");
			if (!pWeapon)
				return nullptr;

			*pWeapon->m_nSubclassID() = CUtlStringToken(def.Subclass);
			updateSubclassVData(pWeapon);
			if (!pWeapon->GetVData())
			{
				Panic("SlotInput: no vdata for %s (%s)\n", def.Classname, def.Subclass);
				pWeapon->Remove();
				return nullptr;
			}

			// Before the spawn, the way the econ item system sets it up
			pWeapon->m_AttributeManager().m_Item().m_iItemDefinitionIndex = def.DefIndex;
			pWeapon->m_AttributeManager().m_Item().m_bInitialized = true;

			pWeapon->DispatchSpawn();

			// The client only selects a grenade that has ammo, and checks the clip first
			pWeapon->m_iClip1 = 1;

			Vector vecOrigin = pPawn->GetAbsOrigin();
			pWeapon->Teleport(&vecOrigin, nullptr, nullptr);

			pWeapon->m_fEffects = pWeapon->m_fEffects() | EF_NODRAW;
			// An ownerless weapon is a dropped one to the client, which draws its world model whatever EF_NODRAW says
			pWeapon->m_nRenderMode = kRenderNone;
			pWeapon->m_clrRender = Color(255, 255, 255, 0);

			if (pWeapon->m_pCollision())
			{
				pWeapon->m_pCollision->m_nSolidType = SOLID_NONE;
				pWeapon->m_pCollision->m_usSolidFlags = pWeapon->m_pCollision->m_usSolidFlags() | FSOLID_NOT_SOLID;
				pWeapon->CollisionRulesChanged();
			}

			// Follows the pawn, so it stays in the owner's PVS
			pWeapon->SetParent(pPawn);
			return pWeapon;
		}

		void RemoveFromList(CPlayer_WeaponServices* pWeaponServices, Session& s)
		{
			CUtlVector<CHandle<CBasePlayerWeapon>>* pMyWeapons = pWeaponServices->m_hMyWeapons();
			for (const Fake& fake : s.Fakes)
				pMyWeapons->FindAndRemove(fake.Weapon);
		}

		// Closing, in two steps: the fakes are sent again (out of dormancy) still ownerless, and only once the client
		// has them active do they get the pawn as owner, then they go. The client drops a fake from its slot
		// selection only on an owner change of an ACTIVE entity.
		constexpr int kHandoverFrames = 8;
		constexpr int kHandoverOwnerAt = 4;

		void HandOver(Session& s)
		{
			CCSPlayerPawn* pPawn = s.Pawn.Get();
			if (!pPawn)
				return;

			for (const Fake& fake : s.Fakes)
			{
				if (CBasePlayerWeapon* pWeapon = fake.Weapon.Get())
					pWeapon->m_hOwnerEntity = CHandle<CBaseEntity>(pPawn);
			}
		}

		void Stop(int slot)
		{
			Session& s = g_Sessions[slot];
			if (!s.Active)
				return;

			if (CPlayer_WeaponServices* pWeaponServices = WeaponServicesOf(s))
			{
				if (s.Injected)
					RemoveFromList(pWeaponServices, s);

				// Once more with the real list, so the client drops the fakes too
				MarkListChanged(pWeaponServices);
			}

			for (const Fake& fake : s.Fakes)
			{
				if (CBasePlayerWeapon* pWeapon = fake.Weapon.Get())
					pWeapon->Remove();
			}

			s = Session{};
		}

		// One fake per key for the whole menu; the page only decides what Inject() puts in the list (deleting a fake
		// on a page change left the client with the one it had just switched to gone, and it then ignored a new one)
		void Sync(Session& s)
		{
			CCSPlayerPawn* pPawn = s.Pawn.Get();
			if (!pPawn)
				return;

			for (const KeyDef& def : kKeys)
			{
				auto it = std::find_if(s.Fakes.begin(), s.Fakes.end(), [&](const Fake& f) { return f.Key == def.Key; });

				if (it != s.Fakes.end() && !it->Weapon.Get())
				{
					s.Fakes.erase(it);
					it = s.Fakes.end();
				}

				if (it == s.Fakes.end() && !(s.FailedKeys & (1u << def.Key)))
				{
					CBasePlayerWeapon* pWeapon = CreateFake(pPawn, def);
					if (!pWeapon)
					{
						Panic("SlotInput: could not create %s for key %d\n", def.Classname, def.Key);
						s.FailedKeys |= 1u << def.Key;
						continue;
					}

					s.Fakes.push_back({def.Key, CHandle<CBasePlayerWeapon>(pWeapon)});
				}
			}
		}

		// Keys the player's open menu does something with right now; 0 = no menu, or nobody alive to hold a fake
		uint32 MenuKeysOf(int slot, CCSPlayerController* pController)
		{
			if (!g_cvarMenuSlotKeys.Get() || !pController || pController->IsBot() || !pController->m_bPawnIsAlive())
				return 0;

			CCSPlayerPawn* pPawn = pController->GetPlayerPawn();
			if (!pPawn || !pPawn->m_pWeaponServices() || !GetUpdateSubclassVData())
				return 0;

			uint32 mask = 0;
			for (int source = 0; source < kMenuSources; source++)
				mask |= g_MenuKeys[slot][source];
			return mask;
		}

		// The key goes to every plugin whose open page takes it, each through its own server command
		constexpr const char* kKeyCommands[kMenuSources] = {"es_menu_key", "mv_menu_key", "zm_menu_key_2", "zm_menu_key_3"};

		void DeliverKey(int slot, int key)
		{
			for (int source = 0; source < kMenuSources; source++)
			{
				if (!(g_MenuKeys[slot][source] & (1u << key)))
					continue;

				char cmd[64];
				V_snprintf(cmd, sizeof(cmd), "%s %d %d\n", kKeyCommands[source], slot, key);
				g_pEngineServer2->ServerCommand(cmd);
			}
		}
	} // namespace

	bool OnWeaponSelect(CCSPlayerController* pController, CSGOUserCmdPB& cmd)
	{
		if (!pController || !cmd.has_base())
			return false;

		const int index = cmd.base().weaponselect();
		const int slot = pController->GetPlayerSlot();
		if (index <= 0 || slot < 0 || slot >= MAXPLAYERS)
			return false;

		Session& s = g_Sessions[slot];
		if (!s.Active)
			return false;

		for (const Fake& fake : s.Fakes)
		{
			CBasePlayerWeapon* pWeapon = fake.Weapon.Get();
			if (pWeapon && pWeapon->entindex() == index)
			{
				cmd.mutable_base()->clear_weaponselect();
				if (s.MenuKeys & (1u << fake.Key))
					DeliverKey(slot, fake.Key);
				return true;
			}
		}

		// A real weapon (Q, the wheel) always switches, menu open or not: Source2Mod blocked the slots of the menu's keys
		// for players who bind "slotN;N", but nobody here binds menu keys, so that only broke Q (2026-10-19).
		return false;
	}

	void Shutdown()
	{
		if (!g_pEntitySystem)
		{
			for (Session& s : g_Sessions)
				s = Session{};
			g_bAnyInjected = false;
			return;
		}

		Eject();
		for (int slot = 0; slot < MAXPLAYERS; slot++)
			Stop(slot);
	}

	void SetMenuKeys(int slot, int source, uint32 mask)
	{
		if (slot >= 0 && slot < MAXPLAYERS && source >= 0 && source < kMenuSources)
			g_MenuKeys[slot][source] = mask;
	}

	void ResetPlayer(CPlayerSlot slot)
	{
		const int s = slot.Get();
		if (s < 0 || s >= MAXPLAYERS)
			return;

		for (uint32& mask : g_MenuKeys[s])
			mask = 0;
	}

	void OnFrame()
	{
		if (!GetGlobals())
			return;

		for (int slot = 0; slot < GetGlobals()->maxClients && slot < MAXPLAYERS; slot++)
		{
			CCSPlayerController* pController = CCSPlayerController::FromSlot(slot);
			Session& s = g_Sessions[slot];

			const uint32 menuKeys = MenuKeysOf(slot, pController);
			if (!menuKeys)
			{
				// Closing: out of dormancy, then the owner, then the fakes go
				if (s.Active && s.Handover == 0 && s.Pawn.Get())
				{
					s.Handover = kHandoverFrames;
					continue;
				}
				if (s.Handover > 0 && --s.Handover > 0)
				{
					if (s.Handover == kHandoverOwnerAt)
						HandOver(s);
					continue;
				}
				Stop(slot);
				continue;
			}

			CCSPlayerPawn* pPawn = pController->GetPlayerPawn();
			if (s.Active && (s.Pawn.Get() != pPawn || s.Handover > 0))
				Stop(slot);

			if (!s.Active)
			{
				s.Active = true;
				s.Pawn = CHandle<CCSPlayerPawn>(pPawn);
			}

			s.MenuKeys = menuKeys;
			Sync(s);
		}
	}

	void Inject()
	{
		if (g_bAnyInjected)
			Eject();

		for (Session& s : g_Sessions)
		{
			if (!s.Active || s.Fakes.empty())
				continue;

			CCSPlayerPawn* pPawn = s.Pawn.Get();
			if (!pPawn || !pPawn->m_pWeaponServices())
				continue;

			// Only the keys the page uses. 6..9 are found by item definition, the FIRST match wins: in front.
			// 1..5 by gear slot, and on a tie the LAST one wins: at the end.
			CUtlVector<CHandle<CBasePlayerWeapon>>* pMyWeapons = pPawn->m_pWeaponServices()->m_hMyWeapons();
			int front = 0;
			for (const KeyDef& def : kKeys)
			{
				if (!(s.MenuKeys & (1u << def.Key)))
					continue;

				for (const Fake& fake : s.Fakes)
				{
					if (fake.Key != def.Key || !fake.Weapon.Get())
						continue;

					if (def.Key >= 6)
						pMyWeapons->InsertBefore(front++, fake.Weapon);
					else
						pMyWeapons->AddToTail(fake.Weapon);
				}
			}

			// Marked only when the list differs from the last one sent: marking it every snapshot made the client
			// rebuild its loadout HUD every tick (it flickered)
			uint64 signature = 1469598103934665603ull;
			for (int i = 0; i < pMyWeapons->Count(); i++)
				signature = (signature ^ static_cast<uint32>((*pMyWeapons)[i].ToInt())) * 1099511628211ull;
			if (signature != s.SentSignature)
			{
				MarkListChanged(pPawn->m_pWeaponServices());
				s.SentSignature = signature;
			}
			s.Injected = true;
			g_bAnyInjected = true;
		}
	}

	void Eject()
	{
		if (!g_bAnyInjected)
			return;

		for (Session& s : g_Sessions)
		{
			if (!s.Active || !s.Injected)
				continue;

			if (CPlayer_WeaponServices* pWeaponServices = WeaponServicesOf(s))
				RemoveFromList(pWeaponServices, s);

			s.Injected = false;
		}

		g_bAnyInjected = false;
	}

	// Nobody else ever gets a player's fakes. Source2Mod also keeps them dormant on the owner's client after a few
	// sends (transmit bit off + the "keep a record" bit on), so CS2's weapon list skips them. Here that crashed the
	// owner's game on 2026-10-19 ("FATAL ERROR: OnPreserveEntity: missing client entity 149", after buying from the
	// shop): the record bit had been set for a fake the client didn't have. So the record bit is only ever set for a
	// fake the owner was really sent kSendsBeforeDormant times, and only with zm_menu_slotkeys_dormant 1 (off by
	// default: the owner simply always gets them). Other viewers never had a fake, so it's just kept off their wire.
	void BlockForViewer(CCheckTransmitInfoExtended* pInfo)
	{
		if (!pInfo || !pInfo->m_pTransmitEntity || !pInfo->m_pTransmitNonPlayers)
			return;

		const int viewerSlot = pInfo->m_nPlayerSlot.Get();
		for (int slot = 0; slot < MAXPLAYERS; slot++)
		{
			Session& s = g_Sessions[slot];
			if (!s.Active)
				continue;

			const bool own = slot == viewerSlot;
			for (Fake& fake : s.Fakes)
			{
				CBasePlayerWeapon* pWeapon = fake.Weapon.Get();
				if (!pWeapon)
					continue;

				const int index = pWeapon->entindex();

				if (!own)
				{
					pInfo->m_pTransmitEntity->Clear(index);
					continue;
				}

				if (pInfo->m_bFullUpdate || s.Handover > 0)
					fake.Sent = 0;

				if (!g_cvarMenuSlotKeysDormant.Get() || s.Handover > 0 || fake.Sent < kSendsBeforeDormant)
				{
					// Counted only when the engine really sends it this snapshot
					if (pInfo->m_pTransmitEntity->IsBitSet(index))
						fake.Sent++;
					continue;
				}

				pInfo->m_pTransmitEntity->Clear(index);
				pInfo->m_pTransmitNonPlayers->Set(index);
			}
		}
	}
} // namespace slotinput

// EconomyShopPlugin (source 0) / MapVotePlugin (1): the keys (bit = key 1..9) the player's open menu page takes; 0 = no
// menu. The keys come back as es_menu_key / mv_menu_key <slot> <key>.
CON_COMMAND_F(zm_menu_keys, "<slot> <key mask> [source 0-3] - Menu keys 1-9 without binds for the open menu page", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
	if (args.ArgC() < 3)
		return;

	int slot = V_StringToInt32(args[1], -1);
	if (slot < 0 || slot >= MAXPLAYERS)
		return;

	int source = args.ArgC() > 3 ? V_StringToInt32(args[3], 0) : 0;
	slotinput::SetMenuKeys(slot, source, (uint32)V_StringToInt32(args[2], 0) & 0x3FE);
}
