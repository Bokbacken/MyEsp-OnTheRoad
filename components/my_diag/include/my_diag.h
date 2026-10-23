// my_diag.h - system diagnostics shared by all MyEsp projects.
//
// - begin() counts boots in NVS, records the reset reason as a persistent event (my_log) and
//   checks for a stored core dump (crash on the previous run).
// - Components register providers that add their own block to the diagnostics JSON
//   (e.g. "wifi", "display"). /api/info = system block + every provider.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

namespace MyDiag {

typedef void (*Provider)(cJSON *obj);   // Fill `obj` (an empty JSON object) with fields

void begin(const char *nvsNamespace, const char *fwName, const char *fwVersion);
bool add(const char *name, Provider fn);   // Max 16 providers
// Adds the "sys" fields (fw, heap, PSRAM, reset, tasks count ...) to `root` and one object
// per provider. Caller owns root.
void fill(cJSON *root);
// Task list as JSON array (name, state, prio, stack high-water mark, core, cpu %).
cJSON *tasks();

uint32_t bootCount();
const char *resetReasonText();   // Swedish, for the UI
const char *resetReasonCode();   // Short English code ("poweron", "panic", ...)
bool lastBootCrashed();          // Reset reason panic / watchdog / brownout
bool coreDumpPresent();

}  // namespace MyDiag
