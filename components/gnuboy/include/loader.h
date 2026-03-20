#ifndef __LOADER_H__
#define __LOADER_H__


typedef struct loader_s
{
	char *rom;
	char *base;
	char *sram;
	char *state;
	int ramloaded;
} loader_t;


extern loader_t loader;

void loader_init(char *s);
void loader_unload();
int rom_load();
int sram_load();
int sram_save();
const char *sram_get_savefile_path(void);
void state_load(int n);
void state_save(int n);



#endif
