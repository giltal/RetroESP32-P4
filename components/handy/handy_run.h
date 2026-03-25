/*
 * handy_run.h — Atari Lynx (Handy) emulator wrapper for ESP32-P4
 */
#ifndef HANDY_RUN_H
#define HANDY_RUN_H

#ifdef __cplusplus
extern "C" {
#endif

void handy_run(const char *rom_path);

#ifdef __cplusplus
}
#endif

#endif /* HANDY_RUN_H */
