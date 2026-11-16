/**
 * Slovak / Czech for CS2Fixes' own chat lines (2026-11-16). LanguagePlugin (C#) decides each player's language and
 * sends it as the server command zm_lang <slot> <sk|cs|en>; ClientPrint / ClientPrintAll (commands.cpp) run every
 * formatted message through TranslateForSlot before it goes to that player. Lines not in lang.cpp's tables stay English.
 */
#pragma once

#include <string>

std::string TranslateForSlot(int iSlot, const char* pszText);
