/* 
 * Copyright (C) 1996-1998 Szeredi Miklos
 * Email: mszeredi@inf.bme.hu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See the file COPYING. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "spconf_p.h"
#include "spver.h"
#include "misc.h"
#include "interf.h"
#include "spscr_p.h"
#include "spkey.h"

#include "snapshot.h"   /* for SN_Z80  and SN_SNA  */
#include "tapefile.h"   /* for TAP_TAP and TAP_TZX */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"

extern const char *spcf_keynames_ascii[];
extern const char *spcf_keynames_misc[];

char *spcf_init_snapshot = NULL;
int   spcf_init_snapshot_type;
char *spcf_init_tapefile = NULL;
int   spcf_init_tapefile_type;

#define MAXLINELEN 512
static int linectr;
static FILE *conffp;
static const char *conffile;

static void strip_spaces(char **sp)
{
  char *s;
  
  s = *sp;
  while(isspace((int)*s)) s++;
  *sp = s;

  return;
}

int spcf_parse_conf_line(char *line, char **attrp, char **valp)
{
  char *s;

  for(s = line; *s; s++) 
    if(*s == '#') {
      *s = '\0';
      break;
    }
  for(s--; s >= line && isspace((int)*s); s--) *s = '\0';

  s = line;

  if(!*s) return 0;

  strip_spaces(&s);

  if(!isalpha((int)*s)) return -1;
  *attrp = s;

  for(; *s; s++) 
    if(!isalnum((int)*s) && *s != '_' && *s != '-') break;
  if(!*s) return -1;

  if(isspace((int)*s)) {
    *s='\0';
    s++;
    strip_spaces(&s);
  }
  if(*s != '=') return -1;
  *s='\0';  
  s++;
  strip_spaces(&s);
  
  if(!*s) return -1;
  *valp = s;
  
  return 1;
}

static int read_conf_line(char **attrp, char **valp)
{
  char *res;
  int ret;

  static char line[MAXLINELEN];

  do {
    linectr++;

    res = fgets(line, MAXLINELEN, conffp);
    if(res == NULL) return 0;

    if(line[0] && line[strlen(line)-1] != '\n') {
      int c;
      
      c = getc(conffp);
      if(c == EOF) 
	fprintf(stderr, "%s: File does not end with a newline\n", conffile);
      else 
	fprintf(stderr, "%s:%i: Line too long\n", conffile ,linectr);
      
      while(c != EOF && c != '\n') c = getc(conffp);
      ret = -1;
    }
    else {
      ret = spcf_parse_conf_line(line, attrp, valp);
      if(ret < 0) fprintf(stderr, "%s:%i: Parse error\n", conffile, linectr);
    }
  } while(ret <= 0);

  return 1;
}

static const char *vgamodes[] =  
{ "320x240", "320x200", NULL };

static const char *keybtypes[] = 
{ "extended", "spectrum", "compat", "custom", NULL };

static const char *curstypes[] = 
{ "shifted", "raw", "joystick", NULL };

static const char *colrtypes[] = 
{ "normal", "grayscale", "custom", NULL };

static const char *modifkeys[] = 
{ "none", "shift", "lock", "control", "alt", "mod2", "mod3", "mod4", "mod5", 
    NULL };


struct sp_options spcf_options[] = {
  {"frameSkip",       SA_INT,  &showframe,          NULL,      1 },
  {"rr",              SA_INT,  &showframe,          NULL,      0 },
  {"scale",           SA_INT,  &scrmul,             NULL,      1 },
  {"privateMap",      SA_BOOL, &privatemap,         NULL,      1 },
  {"mitShm",          SA_BOOL, &use_shm,            NULL,      1 },
  {"vgaMode",         SA_ENUM, &small_screen,       vgamodes,  1 },
  {"sound",           SA_BOOL, &sound_on,           NULL,      1 },
  {"soundDelay",      SA_INT,  &bufframes,          NULL,      1 },
  {"soundDevice",     SA_STR,  &sound_dev_name,     NULL,      1 },
  {"audioDev",        SA_STR,  &sound_dev_name,     NULL,      0 },
  {"soundSampleRate", SA_INT,  &sound_sample_rate,  NULL,      1 },
  {"soundAutoclose",  SA_BOOL, &sound_to_autoclose, NULL,      1 },
  {"soundDspSetfrag", SA_BOOL, &sound_dsp_setfrag,  NULL,      0 },
  {"keyboardType",    SA_ENUM, &keyboard_type,      keybtypes, 1 },
  {"cursorType",      SA_ENUM, &cursor_type,        curstypes, 1 },
  {"allowAscii",      SA_BOOL, &spkb_allow_ascii,   NULL,      0 },
  {"trueShift",       SA_ENUM, &spkb_trueshift,     modifkeys, 0 },
  {"funcShift",       SA_ENUM, &spkb_funcshift,     modifkeys, 0 },
  {"colorType",       SA_ENUM, &color_type,         colrtypes, 1 },
  {"pauseOnIconify",  SA_BOOL, &pause_on_iconify,   NULL,      1 },
  {"vgaPauseBg",      SA_BOOL, &vga_pause_bg,       NULL,      1 },
  {"quickLoad",       SA_BOOL, &sp_quick_load,      NULL,      1 },
  {"autoStop",        SA_BOOL, &spt_auto_stop,      NULL,      1 },
  {"loadImmed",       SA_BOOL, &load_immed,         NULL,      1 },
  {"pause",           SA_BOOL, &sp_paused,          NULL,      1 },

  {NULL, 0, NULL, NULL, 0 }
};

static int file_type = -1;
static int file_subtype;

struct ext_type {
  const char *ext;
  int type;
  int subtype;
};

static struct ext_type extensions[] = {
  {"z80", FT_SNAPSHOT, SN_Z80},
  {"sna", FT_SNAPSHOT, SN_SNA},
  {"tzx", FT_TAPEFILE, TAP_TZX},
  {"tap", FT_TAPEFILE, TAP_TAP},

  {NULL, 0, 0}
};

int spcf_find_file_type(char *filename, int *ftp, int *ftsubp)
{
  int i;
  int found;

  if(*ftp >= 0 && *ftsubp >= 0) return 1;

  found = 0;
  
  for(i = 0; extensions[i].ext != NULL; i++) 
    if((*ftp < 0 || *ftp == extensions[i].type) &&
       (*ftsubp < 0 || *ftsubp == extensions[i].subtype) &&
       check_ext(filename, extensions[i].ext)) {
      found = 1;
      *ftp = extensions[i].type;
      *ftsubp = extensions[i].subtype;
      break;
    }

  if(!found) for(i = 0; extensions[i].ext != NULL; i++) 
    if((*ftp < 0 || *ftp == extensions[i].type) &&
       (*ftsubp < 0 || *ftsubp == extensions[i].subtype) &&
       try_extension(filename, extensions[i].ext)) {
      found = 1;
      *ftp = extensions[i].type;
      *ftsubp = extensions[i].subtype;
      break;
    }
  
  return found;
}

static int find_extension(const char *ext)
{
  int i;

  for(i = 0; extensions[i].ext != NULL; i++)
    if(strcmp(extensions[i].ext, ext) == 0) return i;

  return -1;
}

static const char *spco[] = {
  "help",
  "version",
  NULL
};

#define OPT_HELP     0
#define OPT_VERSION  1

static char *progname;
static int atcol;
#define MAXCOL 70
#define STTAB  "   "

static void putopt(const char *opt)
{
  if(atcol + strlen(opt) + 2 >= MAXCOL) {
    fprintf(stderr, "\n");
    atcol = 0;
  }
  if(!atcol) fprintf(stderr, STTAB);
  
  fprintf(stderr, "%s, ", opt);
  atcol += strlen(opt) + 2;
}

static void makeopt(char *optstr, const char *instr)
{
  *optstr++ = '-';
  for(; *instr; instr++) {
    if(isupper((int)*instr)) {
      *optstr++ = '-';
      *optstr++ = tolower((int)*instr);
    }
    else *optstr++ = *instr;
  }
  *optstr = '\0';
}

static void usage(void)
{
  char optionstr[128];
  int i, j;
  
  fprintf(stderr, "usage: %s [options] [[filetype] filename]\n\n",
	  progname);
  fprintf(stderr, "The following file types can be specified:\n");

  atcol = 0;
  
  for(i = 0; extensions[i].ext != NULL; i++) {
    makeopt(optionstr, extensions[i].ext);
    putopt(optionstr);
  }
  fprintf(stderr, "\n\n");
 
  fprintf(stderr, "The following options are available:\n");

  atcol = 0;

  for(i = 0; spco[i] != NULL; i++) {
    makeopt(optionstr, spco[i]);
    putopt(optionstr);
  }
  
  for(i = 0; spcf_options[i].option != NULL; i++) 
    if(spcf_options[i].disp) switch(spcf_options[i].argtype) {
    case SA_BOOL:
/*
      makeopt(optionstr, spcf_options[i].option);
      putopt(optionstr);
*/
      strcpy(optionstr, "[-no]");
      makeopt(optionstr+5, spcf_options[i].option);
      putopt(optionstr);
      break;

    case SA_INT:
      makeopt(optionstr, spcf_options[i].option);
      strcat(optionstr, " NUM");
      putopt(optionstr);
      break;

    case SA_ENUM:
      makeopt(optionstr, spcf_options[i].option);
      strcat(optionstr, " <");
      for(j = 0; spcf_options[i].enums[j] != NULL; j++) {
	strcat(optionstr, spcf_options[i].enums[j]);
	strcat(optionstr, " ");
      }
      optionstr[strlen(optionstr)-1] = '>';
      putopt(optionstr);
      break;
      
    case SA_STR:
      makeopt(optionstr, spcf_options[i].option);      
      strcat(optionstr, " NAME");
      putopt(optionstr);
      break;
    }
  
  fprintf(stderr, "\n");
}

static void process_option(int ix, int pre)
{
  pre = pre;

  switch(ix) {
  case OPT_HELP:
    usage();
    abort();
    break;
    
  case OPT_VERSION:
    fprintf(stderr, "%s\n", SPECTEMU_VERSION);
    abort();
    break;
  }
}



#define MAXATTRLEN 32

static int match_attr(const char *attr, int onlybool)
{
  int i;
  int nu;
  static char compa[MAXATTRLEN];
  
  nu = 0;
  for(i = 0; *attr && i < MAXATTRLEN - 1; attr++) {
    if(*attr == '-') {
      if(nu) return -1;
      nu = 1;
    }
    else if(nu) nu = 0, compa[i++] = toupper((int)*attr);
    else compa[i++] = tolower((int)*attr);
  }
  if(nu) return -1;
  compa[i] = '\0';

  for(i = 0; spcf_options[i].option != NULL; i++) 
    if((!onlybool || spcf_options[i].argtype == SA_BOOL) &&
       strcmp(spcf_options[i].option, compa) == 0) return i;
  
  return -1;
}

static int match_numbered(const char *attr, const char *beg, int maxnum)
{
  size_t blen;
  int num;

  blen = strlen(beg);
  if(strncmp(attr, beg, blen) == 0) {
    const char *ns;
    int nd;

    ns = attr+blen;
    nd = 0;
    for(; *ns; nd++, ns++) if(!isdigit((int)*ns)) return -1;

    if(nd > 0 && nd < 6 && 
       sscanf(attr+blen, "%d", &num) == 1 && num >= 0 && num <= maxnum) 
      return num;
  }
  return -1;
}

int spcf_match_keydef(const char *attr, const char *beg)
{
  size_t blen;
  
  blen = strlen(beg);
  
  if(strncmp(attr, beg, blen) == 0) {
    int i;
    const char *kn;
    
    kn = attr+blen;
    
    for(i = 32; i < 127; i++) 
      if(strcmp(kn, spcf_keynames_ascii[i-32]) == 0) return i;
    
    for(i = 0; i < 256; i++)
      if(spcf_keynames_misc[i] != NULL &&
	 strcmp(kn, spcf_keynames_misc[i]) == 0) return i+0x100;
  }
  return -1;
}


static int match_option(const char *attr)
{
  int i;

  for(i = 0; spco[i] != NULL; i++)
    if(strcmp(spco[i], attr) == 0) return i;
  
  return -1;
}

static int eqcstr(const char *s, const char *t)
{
  for(; *s || *t; s++, t++) if(tolower((int)*s) != *t) return 0;
  return 1;
}

static int istrue(const char *s)
{
  if(eqcstr(s, "true") ||
     eqcstr(s, "yes")  ||
     eqcstr(s, "on")   ||
     eqcstr(s, "1")) return 1;
  
  return 0;
}

static int isfalse(const char *s)
{
  if(eqcstr(s, "false") ||
     eqcstr(s, "no")  ||
     eqcstr(s, "off")   ||
     eqcstr(s, "0")) return 1;
  
  return 0;
}

void spcf_set_val(int ix, const char *val, const char *name, int ctr, 
		  int fatal)
{
  int res;
  long lv;
  int i;
  const char **es;
  int quiet;
  int *ip;
  char **cp;

  ip = (int *) spcf_options[ix].argvalp;
  cp = (char **) spcf_options[ix].argvalp;

  if(name == NULL) quiet = 1;
  else quiet = 0;

  switch(spcf_options[ix].argtype) {
  case SA_INT:
    res = sscanf(val, "%ld", &lv);
    if(res == 1) *ip = (int) lv;
    else if(!quiet) {
      fprintf(stderr, 
	      "%s:%i: Illegal value '%s', expected integer\n", 
	      name, ctr, val);
      if(fatal) abort();
    }
    break;
    
  case SA_BOOL:
    res = 0;
    if(istrue(val)) lv = 1, res = 1;
    else if(isfalse(val)) lv = 0, res = 1;
    else if(!quiet) {
      fprintf(stderr, 
	      "%s:%i: Illegal value '%s', expected "
	      "'true' or 'false'\n",
	      name, ctr, val);
      if(fatal) abort();
    }
    if(res) *ip = (int) lv;
    break;
    
  case SA_ENUM:
    res = 0;
    es = spcf_options[ix].enums;
    for(i = 0; es[i] != NULL; i++) if(strcmp(val, es[i]) == 0) {
      res = 1;
      break;
    }
    if(res) *ip = i;
    else if(!quiet) {
      fprintf(stderr, "%s:%i: Illegal value '%s', expected ", 
	      name, ctr, val);
      for(i = 0; es[i] != NULL;) {
	fprintf(stderr, "'%s'", es[i]);
	if(es[++i] != NULL) fprintf(stderr, " or ");
      }
      fprintf(stderr, "\n");
      if(fatal) abort();
    }
    break;
    
  case SA_STR:
    *cp = make_string(*cp, val);
    break;
  }
}

void spcf_set_color(int ix, const char *val, const char *name, int ctr, 
		    int fatal)
{
  int r, g, b;
  int res;
  int quiet;
  
  if(name == NULL) quiet = 1;
  else quiet = 0;

  res = sscanf(val, "%d %d %d", &r, &g, &b);
  if(res != 3 || r < 0 || r > 63 || g < 0 || g > 63 || b < 0 || b > 63) {
    if(!quiet) {
      fprintf(stderr, "%s:%i: Illegal rgb values: '%s'\n", name, ctr, val);
      if(fatal) abort();
    }
  }
  else {
    custom_colors[ix].r = r;
    custom_colors[ix].g = g;
    custom_colors[ix].b = b;
  }
}

#define MAXSPKEYNAME 32

void spcf_set_key(int ix, const char *val, const char *name, int ctr,
		  int fatal)
{
  int quiet;
  char spkeyname[MAXSPKEYNAME+1];
  int len;
  
  if(name == NULL) quiet = 1;
  else quiet = 0;

  if(!spkey_new_custom(ix)) {
    if(!quiet) {
      fprintf(stderr, "%s:%i: Custom key table full\n", name, ctr);
      if(fatal) abort();
    }
    return;
  }
  
  while(*val) {
    for(len = 0; val[len] && !isspace((int)val[len]); len++);
    if(len > MAXSPKEYNAME) len = MAXSPKEYNAME;
    strncpy(spkeyname, val, (size_t) len);
    spkeyname[len] = '\0';
    
    if(!spkey_add_custom(spkeyname)) {
      if(!quiet) {
	fprintf(stderr, "%s:%i: Illegal key name: '%s'\n", name, ctr, 
		spkeyname);
	if(fatal) abort();
      }
    }
    val += len;
    while(isspace((int)*val)) val++;
  }
  
  
}

void spcf_pre_check_options(int argc, char *argv[])
{
  progname = get_base_name(argv[0]);
  
  if(argc > 1 && argv[1][0] == '-') {
    int ix;

    ix = match_option(argv[1]+1);
    if(ix >= 0) process_option(ix, 1);
  }
}

void spcf_read_command_line(int argc, char *argv[])
{
  int a;
  int ix;
  int bval;

  for(a = 1; a < argc; a++) {
    if(argv[a][0] == '-') {
      if(file_type >= 0) {
	fprintf(stderr, "File name expected after option '%s'\n", argv[a-1]);
	abort();
      }
      bval = 1;
      if(strncmp(argv[a]+1, "no-", 3) == 0) {
	bval = 0;
	ix = match_attr(argv[a]+4, 1);
      }
      else ix = match_attr(argv[a]+1, 0);

      if(ix >= 0) {
	if(spcf_options[ix].argtype == SA_BOOL) 
	  *((int *) spcf_options[ix].argvalp) = (int) bval;
	else {
	  if(a+1 < argc) a++, spcf_set_val(ix, argv[a], "Command line", a, 1);
	  else {
	    fprintf(stderr, "Option '%s', argument expected\n",
		    argv[a]);
	    abort();
	  }
	}
      }
      else {
	ix = match_option(argv[a]+1);
	
	if(ix >= 0) process_option(ix, 0);
	else {
	  ix = find_extension(argv[a]+1);
	  
	  if(ix >= 0) {
	    if(a + 1 == argc) {
	      fprintf(stderr, 
		      "File name expected after option '%s'\n", argv[a]);
	      abort();
	    }

	    file_type = extensions[ix].type;
	    file_subtype = extensions[ix].subtype;
	  }
	  else {
	    fprintf(stderr, "Option '%s' not recognized\n", argv[a]);
	    fprintf(stderr, "Use '-help' to see valid options\n");
	    abort();
	  }
	}
      }
    }
    else { /* Does not start with '-' */
      strncpy(filenamebuf, argv[a], MAXFILENAME - 10);
      filenamebuf[MAXFILENAME-10] = '\0';
      
      if(file_type < 0) file_subtype = -1;
      
      if(!spcf_find_file_type(filenamebuf, &file_type, &file_subtype)) {
	fprintf(stderr, "Can't determine file type of '%s'\n", filenamebuf);
	abort();
      }
      
      if(file_type == FT_SNAPSHOT) {
	spcf_init_snapshot = make_string(spcf_init_snapshot, filenamebuf);
	spcf_init_snapshot_type = file_subtype;
      }
      else if(file_type == FT_TAPEFILE) {
	spcf_init_tapefile = make_string(spcf_init_tapefile, filenamebuf);
	spcf_init_tapefile_type = file_subtype;
      }
      
      file_type = -1;
    }
  }
}

int spcf_read_conf_file(const char *filename)
{
  int res;
  char *attr, *val;

  conffile = filename;
  
  conffp = fopen(conffile, "rt");
  if(conffp == NULL) {
    fprintf(stderr, "Could not open file %s: %s\n", 
	    conffile, strerror(errno));
    return -1;
  }
  linectr = 0;
  
  for(;;) {
    int ix;

    res = read_conf_line(&attr, &val);
    if(res <= 0) break;

    ix = match_attr(attr, 0);
    if(ix >= 0) spcf_set_val(ix, val, conffile, linectr, 0);
    else {
      ix = match_numbered(attr, "color", COLORNUM-1);
      if(ix >= 0) spcf_set_color(ix, val, conffile, linectr, 0);
      else {
	ix = spcf_match_keydef(attr, "Key_");
	if(ix >= 0) spcf_set_key(ix, val, conffile, linectr, 0);
	else fprintf(stderr, "%s:%i: Unknown attribute '%s'\n", 
		     conffile, linectr, attr);
      }
    }
  }

  fclose(conffp);

  return res;
}
