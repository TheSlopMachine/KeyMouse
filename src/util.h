#ifndef UTIL_H
#define UTIL_H

#include "common.h"

bool util_exe_path(wchar_t *buf, DWORD cap);
bool util_is_elevated(void);

/* Start a new, elevated copy of this exe (UAC prompt). Returns false if the
 * user declined. The caller should exit on success. */
bool util_relaunch_elevated(void);

/* High priority class + opt out of EcoQoS / power throttling. */
void util_boost_process(void);

#endif
