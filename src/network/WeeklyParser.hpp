#pragma once
#include "models/Types.hpp"
// Prozesszeitzone muss UTC sein. Das Ergebnis wird erst nach vollständiger Prüfung übernommen.
bool parseWeekly(const char *body, SavedState &result, int64_t now);
