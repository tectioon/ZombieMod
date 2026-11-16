/**
 * See lang.h. The tables hold the formatted English line (after printf), without the chat prefix; regex groups
 * come back in the translation as $1, $2 ... Colour bytes are split off as their own literal ("\x07" "objavovať")
 * wherever the next character could be read as part of the hex escape.
 */
#include "lang.h"

#include "commands.h"
#include "common.h"
#include "zombiemod.h"
#include <regex>
#include <unordered_map>
#include <vector>

#include "tier0/memdbgon.h"

namespace
{
	enum ELang
	{
		LANG_EN,
		LANG_SK,
		LANG_CS
	};

	ELang g_arrLang[MAXPLAYERS] = {};

	struct Translation
	{
		const char* pszSk;
		const char* pszCs;
	};

	const std::unordered_map<std::string, Translation>& Exact()
	{
		static const std::unordered_map<std::string, Translation> map = {
			{"The game is \x05Humans vs. Zombies\x01, the goal for zombies is to infect all humans by knifing them.",
			 {"Hra je \x05\xC4\xBD" "udia vs. Zombíci\x01, cieľom zombíkov je nožom nakaziť všetkých ľudí.",
			  "Hra je \x05Lidé vs. Zombíci\x01, cílem zombíků je nožem nakazit všechny lidi."}},
			{"Classic spawn is enabled! Zombies will be \x07spawning between humans\x01!",
			 {"Klasický spawn je zapnutý! Zombíci sa budú \x07objavovať medzi ľuďmi\x01!",
			  "Klasický spawn je zapnutý! Zombíci se budou \x07objevovat mezi lidmi\x01!"}},
			{"There are no spawns!", {"Nie sú tu žiadne spawny!", "Nejsou tu žádné spawny!"}},
			{"First infection has started! Good luck, survivors!",
			 {"Prvá infekcia začala! Veľa šťastia, preživší!", "První infekce začala! Hodně štěstí, přeživší!"}},
			{"Respawning is enabled!", {"Respawn je zapnutý!", "Respawn je zapnutý!"}},
			{"Respawning is disabled!", {"Respawn je vypnutý!", "Respawn je vypnutý!"}},
			{"Zombie sounds\x04 enabled\x10 (Infect sounds only).",
			 {"Zvuky zombíkov\x04 zapnuté\x10 (len zvuky infekcie).", "Zvuky zombíků\x04 zapnuté\x10 (jen zvuky infekce)."}},
			{"Zombie sounds\x07 disabled.", {"Zvuky zombíkov\x07 vypnuté.", "Zvuky zombíků\x07 vypnuté."}},
			{"Zombie sounds\x04 enabled.", {"Zvuky zombíkov\x04 zapnuté.", "Zvuky zombíků\x04 zapnuté."}},
			{"You cannot use this command as a human.", {"Tento príkaz nemôžeš použiť ako človek.", "Tento příkaz nemůžeš použít jako člověk."}},
			{"You cannot use this command as a zombie.", {"Tento príkaz nemôžeš použiť ako zombík.", "Tento příkaz nemůžeš použít jako zombík."}},
			{"You can only use this command as a human before a zombie has been chosen.",
			 {"Tento príkaz môžeš použiť len ako človek, kým ešte nie je vybraný zombík.",
			  "Tento příkaz můžeš použít jen jako člověk, než je vybrán zombík."}},
			{"You can only use this command as a human after a zombie has been chosen.",
			 {"Tento príkaz môžeš použiť len ako človek, keď už je vybraný zombík.",
			  "Tento příkaz můžeš použít jen jako člověk, když už je vybrán zombík."}},
			{"You cannot teleport when dead!", {"Mŕtvy sa teleportovať nemôžeš!", "Mrtvý se teleportovat nemůžeš!"}},
			{"You have been teleported to spawn.", {"Bol si teleportovaný na spawn.", "Byl jsi teleportován na spawn."}},
			{"Teleport failed! You moved too far.", {"Teleport zlyhal! Pohol si sa príliš ďaleko.", "Teleport selhal! Pohnul ses příliš daleko."}},
			{"The round is already over!", {"Kolo už skončilo!", "Kolo už skončilo!"}},
			{"A round is not ongoing!", {"Neprebieha žiadne kolo!", "Neprobíhá žádné kolo!"}},
			{"There are no mother zombies.", {"Nie sú tu žiadni materskí zombíci.", "Nejsou tu žádní mateřští zombíci."}},
			{"You don't have access to this command.", {"K tomuto príkazu nemáš prístup.", "K tomuto příkazu nemáš přístup."}},
			{"You must be a human to use this command.", {"Tento príkaz môže použiť len človek.", "Tento příkaz může použít jen člověk."}},
			{"You must be a leader to use this command.", {"Tento príkaz môže použiť len vodca.", "Tento příkaz může použít jen vůdce."}},
		};
		return map;
	}

	struct Pattern
	{
		std::regex re;
		const char* pszSk;
		const char* pszCs;
	};

	const std::vector<Pattern>& Patterns()
	{
		static const std::vector<Pattern> patterns = [] {
			std::vector<Pattern> v;
			auto add = [&v](const char* pszEn, const char* pszSk, const char* pszCs) {
				v.push_back({std::regex(std::string("^") + pszEn + "$"), pszSk, pszCs});
			};
			add("First infection in \x07(\\d+) seconds?\x01!", "Prvá infekcia o \x07$1 s\x01!", "První infekce za \x07$1 s\x01!");
			add("Teleporting to spawn in ([0-9.]+) seconds\\.", "Teleport na spawn o $1 s.", "Teleport na spawn za $1 s.");
			add("You have already teleported this round more than the maximum allowed number of times for a human \\((\\d+)\\)!",
				"Toto kolo si sa ako človek už teleportoval maximálny počet krát ($1)!",
				"Toto kolo ses jako člověk už teleportoval maximální početkrát ($1)!");
			add("You have already teleported this round more than the maximum allowed number of times for a zombie \\((\\d+)\\)!",
				"Toto kolo si sa ako zombík už teleportoval maximálny počet krát ($1)!",
				"Toto kolo ses jako zombík už teleportoval maximální početkrát ($1)!");
			add("Mother zombies: (.+)", "Materskí zombíci: $1", "Mateřští zombíci: $1");
			return v;
		}();
		return patterns;
	}

	std::string TranslateCore(ELang lang, const std::string& text)
	{
		auto it = Exact().find(text);
		if (it != Exact().end())
			return lang == LANG_SK ? it->second.pszSk : it->second.pszCs;

		for (const Pattern& pattern : Patterns())
		{
			std::smatch match;
			if (std::regex_match(text, match, pattern.re))
				return match.format(lang == LANG_SK ? pattern.pszSk : pattern.pszCs);
		}
		return text;
	}
} // namespace

std::string TranslateForSlot(int iSlot, const char* pszText)
{
	std::string text = pszText ? pszText : "";
	if (iSlot < 0 || iSlot >= MAXPLAYERS || g_arrLang[iSlot] == LANG_EN)
		return text;
	ELang lang = g_arrLang[iSlot];

	// Every module's chat prefix is the same since 2026-11-16 (ZM_PREFIX == CHAT_PREFIX == ...)
	static const std::string prefix = ZM_PREFIX;
	if (text.compare(0, prefix.size(), prefix) == 0)
		return prefix + TranslateCore(lang, text.substr(prefix.size()));
	return TranslateCore(lang, text);
}

CON_COMMAND_F(zm_lang, "<slot> <sk|cs|en> - A player's language for CS2Fixes' chat lines (sent by LanguagePlugin)", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
	if (args.ArgC() < 3)
		return;
	int iSlot = V_StringToInt32(args[1], -1);
	if (iSlot < 0 || iSlot >= MAXPLAYERS)
		return;
	g_arrLang[iSlot] = !V_stricmp(args[2], "sk") ? LANG_SK : (!V_stricmp(args[2], "cs") ? LANG_CS : LANG_EN);
}
