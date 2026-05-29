#pragma once

#include <string>

// Russian year phrases for TTS (1800–2099): ordinal year readings by scenario.
// mode: "neut" | "gen" | "fem" | "nom" | "loc" (prepositional, в … году)
std::string russianYearOrdinal(int year, const std::string& mode);

// Parse a spoken year phrase (cardinal/ordinal mix) back to 1800–2099, or -1.
int parseRussianSpokenYear(const std::string& phraseUtf8);
