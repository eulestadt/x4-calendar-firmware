#pragma once

// X4 timed-wake sleep. Keeps GPIO13 latched so the RTC can wake on schedule,
// and parks the panel, SD card, and mid-level ADC pins that otherwise stay awake.
void x4_release_sleep_holds(void);
void x4_prepare_timed_sleep(void);
