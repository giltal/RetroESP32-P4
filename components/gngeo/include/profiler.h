/* profiler.h — stub for ESP32-P4 */
#ifndef _PROFILER_H_
#define _PROFILER_H_

enum {
    PROF_ALL = 0,
    PROF_VIDEO,
    PROF_68K,
    PROF_Z80,
    PROF_SDLBLIT,
    PROF_SOUND,
    MAX_BLOCK
};

#define profiler_start(a) do {} while(0)
#define profiler_stop(a)  do {} while(0)
#define profiler_show_stat() do {} while(0)

#endif /* _PROFILER_H_ */
