/*
 * atari800_run.h — Public API for Atari 800 emulator on ESP32-P4
 */
#ifndef ATARI800_RUN_H
#define ATARI800_RUN_H

#ifdef __cplusplus
extern "C" {
#endif

void atari800_run(const char *rom_path);

#ifdef __cplusplus
}
#endif

#endif /* ATARI800_RUN_H */
