// stubs for IO.... bjs

// sound related stubs...
void open_snd() {}

// keyboard stubs...
void keyboard_close() {}
void keyboard_getstate() {}
void keyboard_init() {}
void keyboard_translatekeys() {}

int keyboard = 0;  // on-screen keyboard active (always 0 on P4)
int menu() { return 0; }
void kb_blank() {}
void kb_set() {}

// graphics stubs.....
void vga_drawscanline() {}
void vga_getgraphmem() {}
void vga_setmode() {}
void vga_setpalvec() {}


// misc stubs...

void setbufsize() {} // keyboard and sound
