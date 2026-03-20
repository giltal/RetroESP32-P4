/*
 * stella_run.h — Atari 2600 (Stella) emulator wrapper for ESP32-P4
 */
#ifndef STELLA_RUN_H
#define STELLA_RUN_H

#ifdef __cplusplus
extern "C" {
#endif

void stella_run(const char *rom_path);

#ifdef __cplusplus
}
#endif

#endif /* STELLA_RUN_H */
