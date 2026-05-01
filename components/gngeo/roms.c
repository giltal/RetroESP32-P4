#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
//#include <stdbool.h>
#include "roms.h"
#include "emu.h"
#include "memory.h"
//#include "unzip.h"
#if defined(HAVE_LIBZ)// && defined (HAVE_MMAP)
#include "zlib.h"
#endif
#include "unzip.h"

#include "video.h"
#include "ff.h"
#include "diskio.h"
#include "transpack.h"
#include "conf.h"
#include "resfile.h"
#include "menu.h"
#include "gnutil.h"
#include "frame_skip.h"
#include "screen.h"
#include "adpcm_cache.h"
#ifdef GP2X
#include "gp2x.h"
#include "ym2610-940/940shared.h"
#endif

/* Prototype */
void kof98_decrypt_68k(GAME_ROMS *r);
void kof99_decrypt_68k(GAME_ROMS *r);
void garou_decrypt_68k(GAME_ROMS *r);
void garouo_decrypt_68k(GAME_ROMS *r);
void mslug3_decrypt_68k(GAME_ROMS *r);
void kof2000_decrypt_68k(GAME_ROMS *r);
void kof2002_decrypt_68k(GAME_ROMS *r);
void matrim_decrypt_68k(GAME_ROMS *r);
void samsho5_decrypt_68k(GAME_ROMS *r);
void samsh5p_decrypt_68k(GAME_ROMS *r);
void mslug5_decrypt_68k(GAME_ROMS *r);
void kf2k3pcb_decrypt_s1data(GAME_ROMS *r);
void kf2k3pcb_decrypt_68k(GAME_ROMS *r);
void kof2003_decrypt_68k(GAME_ROMS *r);
void kof99_neogeo_gfx_decrypt(GAME_ROMS *r, int extra_xor);
void kof2000_neogeo_gfx_decrypt(GAME_ROMS *r, int extra_xor);
void cmc50_neogeo_gfx_decrypt(GAME_ROMS *r, int extra_xor);
void cmc42_neogeo_gfx_decrypt(GAME_ROMS *r, int extra_xor);
void neogeo_bootleg_cx_decrypt(GAME_ROMS *r);
void neogeo_bootleg_sx_decrypt(GAME_ROMS *r, int extra_xor);
void svcpcb_gfx_decrypt(GAME_ROMS *r);
void svcpcb_s1data_decrypt(GAME_ROMS *r);
void neo_pcm2_swap(GAME_ROMS *r, int value);
void neo_pcm2_snk_1999(GAME_ROMS *r, int value);
void neogeo_cmc50_m1_decrypt(GAME_ROMS *r);

static int need_decrypt = 1;

int neogeo_fix_bank_type = 0;

int bankoffset_kof99[64] = {
	0x000000, 0x100000, 0x200000, 0x300000, 0x3cc000,
	0x4cc000, 0x3f2000, 0x4f2000, 0x407800, 0x507800, 0x40d000, 0x50d000,
	0x417800, 0x517800, 0x420800, 0x520800, 0x424800, 0x524800, 0x429000,
	0x529000, 0x42e800, 0x52e800, 0x431800, 0x531800, 0x54d000, 0x551000,
	0x567000, 0x592800, 0x588800, 0x581800, 0x599800, 0x594800, 0x598000,
	/* rest not used? */
};
/* addr,uncramblecode,.... */
Uint8 scramblecode_kof99[7] = {0xF0, 14, 6, 8, 10, 12, 5,};

int bankoffset_garou[64] = {
	0x000000, 0x100000, 0x200000, 0x300000, // 00
	0x280000, 0x380000, 0x2d0000, 0x3d0000, // 04
	0x2f0000, 0x3f0000, 0x400000, 0x500000, // 08
	0x420000, 0x520000, 0x440000, 0x540000, // 12
	0x498000, 0x598000, 0x4a0000, 0x5a0000, // 16
	0x4a8000, 0x5a8000, 0x4b0000, 0x5b0000, // 20
	0x4b8000, 0x5b8000, 0x4c0000, 0x5c0000, // 24
	0x4c8000, 0x5c8000, 0x4d0000, 0x5d0000, // 28
	0x458000, 0x558000, 0x460000, 0x560000, // 32
	0x468000, 0x568000, 0x470000, 0x570000, // 36
	0x478000, 0x578000, 0x480000, 0x580000, // 40
	0x488000, 0x588000, 0x490000, 0x590000, // 44
	0x5d0000, 0x5d8000, 0x5e0000, 0x5e8000, // 48
	0x5f0000, 0x5f8000, 0x600000, /* rest not used? */
};
Uint8 scramblecode_garou[7] = {0xC0, 5, 9, 7, 6, 14, 12,};
int bankoffset_garouo[64] = {
	0x000000, 0x100000, 0x200000, 0x300000, // 00
	0x280000, 0x380000, 0x2d0000, 0x3d0000, // 04
	0x2c8000, 0x3c8000, 0x400000, 0x500000, // 08
	0x420000, 0x520000, 0x440000, 0x540000, // 12
	0x598000, 0x698000, 0x5a0000, 0x6a0000, // 16
	0x5a8000, 0x6a8000, 0x5b0000, 0x6b0000, // 20
	0x5b8000, 0x6b8000, 0x5c0000, 0x6c0000, // 24
	0x5c8000, 0x6c8000, 0x5d0000, 0x6d0000, // 28
	0x458000, 0x558000, 0x460000, 0x560000, // 32
	0x468000, 0x568000, 0x470000, 0x570000, // 36
	0x478000, 0x578000, 0x480000, 0x580000, // 40
	0x488000, 0x588000, 0x490000, 0x590000, // 44
	0x5d8000, 0x6d8000, 0x5e0000, 0x6e0000, // 48
	0x5e8000, 0x6e8000, 0x6e8000, 0x000000, // 52
	0x000000, 0x000000, 0x000000, 0x000000, // 56
	0x000000, 0x000000, 0x000000, 0x000000, // 60
};
Uint8 scramblecode_garouo[7] = {0xC0, 4, 8, 14, 2, 11, 13,};

int bankoffset_mslug3[64] = {
	0x000000, 0x020000, 0x040000, 0x060000, // 00
	0x070000, 0x090000, 0x0b0000, 0x0d0000, // 04
	0x0e0000, 0x0f0000, 0x120000, 0x130000, // 08
	0x140000, 0x150000, 0x180000, 0x190000, // 12
	0x1a0000, 0x1b0000, 0x1e0000, 0x1f0000, // 16
	0x200000, 0x210000, 0x240000, 0x250000, // 20
	0x260000, 0x270000, 0x2a0000, 0x2b0000, // 24
	0x2c0000, 0x2d0000, 0x300000, 0x310000, // 28
	0x320000, 0x330000, 0x360000, 0x370000, // 32
	0x380000, 0x390000, 0x3c0000, 0x3d0000, // 36
	0x400000, 0x410000, 0x440000, 0x450000, // 40
	0x460000, 0x470000, 0x4a0000, 0x4b0000, // 44
	0x4c0000, /* rest not used? */
};
Uint8 scramblecode_mslug3[7] = {0xE4, 14, 12, 15, 6, 3, 9,};
int bankoffset_kof2000[64] = {
	0x000000, 0x100000, 0x200000, 0x300000, // 00
	0x3f7800, 0x4f7800, 0x3ff800, 0x4ff800, // 04
	0x407800, 0x507800, 0x40f800, 0x50f800, // 08
	0x416800, 0x516800, 0x41d800, 0x51d800, // 12
	0x424000, 0x524000, 0x523800, 0x623800, // 16
	0x526000, 0x626000, 0x528000, 0x628000, // 20
	0x52a000, 0x62a000, 0x52b800, 0x62b800, // 24
	0x52d000, 0x62d000, 0x52e800, 0x62e800, // 28
	0x618000, 0x619000, 0x61a000, 0x61a800, // 32
};
Uint8 scramblecode_kof2000[7] = {0xEC, 15, 14, 7, 3, 10, 5,};

#define LOAD_BUF_SIZE (256*1024)
static Uint8* iloadbuf = NULL;

//char romerror[1024];

/* Actuall Code */

/*
 TODO
 static DRIVER_INIT( fatfury2 )
 {
 DRIVER_INIT_CALL(neogeo);
 fatfury2_install_protection(machine);
 }

 static DRIVER_INIT( mslugx )
 {
 DRIVER_INIT_CALL(neogeo);
 mslugx_install_protection(machine);
 }

 */

int init_mslugx(GAME_ROMS *r) {
	unsigned int i;
	Uint8 *RAM = r->cpu_m68k.p;
	if (need_decrypt) {
		for (i = 0; i < r->cpu_m68k.size; i += 2) {
			if ((READ_WORD_ROM(&RAM[i + 0]) == 0x0243)
					&& (READ_WORD_ROM(&RAM[i + 2]) == 0x0001) && /* andi.w  #$1, D3 */
					(READ_WORD_ROM(&RAM[i + 4]) == 0x6600)) { /* bne xxxx */

				WRITE_WORD_ROM(&RAM[i + 4], 0x4e71);
				WRITE_WORD_ROM(&RAM[i + 6], 0x4e71);
			}
		}

		WRITE_WORD_ROM(&RAM[0x3bdc], 0x4e71);
		WRITE_WORD_ROM(&RAM[0x3bde], 0x4e71);
		WRITE_WORD_ROM(&RAM[0x3be0], 0x4e71);
		WRITE_WORD_ROM(&RAM[0x3c0c], 0x4e71);
		WRITE_WORD_ROM(&RAM[0x3c0e], 0x4e71);
		WRITE_WORD_ROM(&RAM[0x3c10], 0x4e71);

		WRITE_WORD_ROM(&RAM[0x3c36], 0x4e71);
		WRITE_WORD_ROM(&RAM[0x3c38], 0x4e71);
	}
	return 0;
}

int init_kof99(GAME_ROMS *r) {
	if (need_decrypt) {
		kof99_decrypt_68k(r);
		kof99_neogeo_gfx_decrypt(r, 0x00);
	}
	neogeo_fix_bank_type = 0;
	memory.bksw_offset = bankoffset_kof99;
	memory.bksw_unscramble = scramblecode_kof99;
	memory.sma_rng_addr = 0xF8FA;
	//kof99_install_protection(machine);
	return 0;
}

int init_kof99n(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0x00);
	return 0;
}

int init_garou(GAME_ROMS *r) {
	if (need_decrypt) {
		garou_decrypt_68k(r);
		kof99_neogeo_gfx_decrypt(r, 0x06);
	}
	neogeo_fix_bank_type = 1;
	memory.bksw_offset = bankoffset_garou;
	memory.bksw_unscramble = scramblecode_garou;
	memory.sma_rng_addr = 0xCCF0;
	//garou_install_protection(machine);
	DEBUG_LOG("I HAS INITIALIZD GAROU\n");
	return 0;
}

int init_garouo(GAME_ROMS *r) {
	if (need_decrypt) {
		garouo_decrypt_68k(r);
		kof99_neogeo_gfx_decrypt(r, 0x06);
	}
	neogeo_fix_bank_type = 1;
	memory.bksw_offset = bankoffset_garouo;
	memory.bksw_unscramble = scramblecode_garouo;
	memory.sma_rng_addr = 0xCCF0;

	//garouo_install_protection(machine);
	return 0;
}

/*
 int init_garoup(GAME_ROMS *r) {
 garou_decrypt_68k(r);
 kof99_neogeo_gfx_decrypt(r, 0x06);

 return 0;
 }
 */
int init_garoubl(GAME_ROMS *r) {
	/* TODO: Bootleg support */
	if (need_decrypt) {
		neogeo_bootleg_sx_decrypt(r, 2);
		neogeo_bootleg_cx_decrypt(r);
	}
	return 0;
}

int init_mslug3(GAME_ROMS *r) {
	printf("INIT MSLUG3\n");
	if (need_decrypt) {
		mslug3_decrypt_68k(r);
		kof99_neogeo_gfx_decrypt(r, 0xad);
	}
	neogeo_fix_bank_type = 1;
	memory.bksw_offset = bankoffset_mslug3;
	memory.bksw_unscramble = scramblecode_mslug3;
	//memory.sma_rng_addr=0xF8FA;
	memory.sma_rng_addr = 0;

	//mslug3_install_protection(r);
	return 0;
}

int init_mslug3h(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0xad);
	return 0;
}

int init_mslug3b6(GAME_ROMS *r) {
	/* TODO: Bootleg support */
	if (need_decrypt) {
		neogeo_bootleg_sx_decrypt(r, 2);
		cmc42_neogeo_gfx_decrypt(r, 0xad);
	}
	return 0;
}

int init_kof2000(GAME_ROMS *r) {
	if (need_decrypt) {
		kof2000_decrypt_68k(r);
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x00);
	}
	neogeo_fix_bank_type = 2;
	memory.bksw_offset = bankoffset_kof2000;
	memory.bksw_unscramble = scramblecode_kof2000;
	memory.sma_rng_addr = 0xD8DA;
	//kof2000_install_protection(r);
	return 0;

}

int init_kof2000n(GAME_ROMS *r) {
	neogeo_fix_bank_type = 2;
	if (need_decrypt) {
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x00);
	}
	return 0;
}

int init_kof2001(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) {
		kof2000_neogeo_gfx_decrypt(r, 0x1e);
		neogeo_cmc50_m1_decrypt(r);
	}
	return 0;

}

/*

 TODO:
 static DRIVER_INIT( cthd2003 )
 {
 decrypt_cthd2003(machine);
 DRIVER_INIT_CALL(neogeo);
 patch_cthd2003(machine);
 }

 static DRIVER_INIT ( ct2k3sp )
 {
 decrypt_ct2k3sp(machine);
 DRIVER_INIT_CALL(neogeo);
 patch_cthd2003(machine);
 }

 static DRIVER_INIT ( ct2k3sa )
 {
 decrypt_ct2k3sa(machine);
 DRIVER_INIT_CALL(neogeo);
 patch_ct2k3sa(machine);
 }

 */

int init_mslug4(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1; /* USA violent content screen is wrong --
							 * not a bug, confirmed on real hardware! */
	if (need_decrypt) {
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x31);

		neo_pcm2_snk_1999(r, 8);
	}
	return 0;

}

int init_ms4plus(GAME_ROMS *r) {
	if (need_decrypt) {
		cmc50_neogeo_gfx_decrypt(r, 0x31);
		neo_pcm2_snk_1999(r, 8);
		neogeo_cmc50_m1_decrypt(r);
	}
	return 0;
}

int init_ganryu(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0x07);
	return 0;
}

int init_s1945p(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0x05);
	return 0;
}

int init_preisle2(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0x9f);
	return 0;
}

int init_bangbead(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0xf8);
	return 0;
}

int init_nitd(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0xff);
	return 0;
}

int init_zupapa(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0xbd);
	return 0;
}

int init_sengoku3(GAME_ROMS *r) {
	neogeo_fix_bank_type = 1;
	if (need_decrypt) kof99_neogeo_gfx_decrypt(r, 0xfe);
	return 0;
}

int init_kof98(GAME_ROMS *r) {
	if (need_decrypt) kof98_decrypt_68k(r);

	/* Enable kof98 protection, start at state 0 (return original ROM values) */
	memory.kof98_prot = 1;
	memory.kof98_prot_state = 0;

	return 0;
}

int init_rotd(GAME_ROMS *r) {
	if (need_decrypt) {
		neo_pcm2_snk_1999(r, 16);
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x3f);
	}
	neogeo_fix_bank_type = 1;
	return 0;
}

int init_kof2002(GAME_ROMS *r) {
	if (need_decrypt) {
		kof2002_decrypt_68k(r);
		neo_pcm2_swap(r, 0);
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0xec);
	}
	return 0;
}

int init_kof2002b(GAME_ROMS *r) {
	/* TODO: Bootleg */
	if (need_decrypt) {
		kof2002_decrypt_68k(r);
		neo_pcm2_swap(r, 0);
		neogeo_cmc50_m1_decrypt(r);
		//kof2002b_gfx_decrypt(r, r->tiles.p,0x4000000);
		//kof2002b_gfx_decrypt(r, r->game_sfix.p,0x20000);
	}
	return 0;
}

int init_kf2k2pls(GAME_ROMS *r) {
	if (need_decrypt) {
		kof2002_decrypt_68k(r);
		neo_pcm2_swap(r, 0);
		neogeo_cmc50_m1_decrypt(r);
		cmc50_neogeo_gfx_decrypt(r, 0xec);
	}
	return 0;
}

int init_kf2k2mp(GAME_ROMS *r) {
	/* TODO: Bootleg */
	if (need_decrypt) {
		//kf2k2mp_decrypt(r);
		neo_pcm2_swap(r, 0);
		//neogeo_bootleg_sx_decrypt(r, 2);
		cmc50_neogeo_gfx_decrypt(r, 0xec);
	}
	return 0;
}

int init_kof2km2(GAME_ROMS *r) {
	/* TODO: Bootleg */
	if (need_decrypt) {
		//kof2km2_px_decrypt(r);
		neo_pcm2_swap(r, 0);
		//neogeo_bootleg_sx_decrypt(r, 1);
		cmc50_neogeo_gfx_decrypt(r, 0xec);
	}
	return 0;
}

/*

 TODO
 static DRIVER_INIT( kof10th )
 {
 decrypt_kof10th(machine);
 DRIVER_INIT_CALL(neogeo);
 install_kof10th_protection(machine);
 }

 static DRIVER_INIT( kf10thep )
 {
 decrypt_kf10thep(machine);
 DRIVER_INIT_CALL(neogeo);
 }

 static DRIVER_INIT( kf2k5uni )
 {
 decrypt_kf2k5uni(machine);
 DRIVER_INIT_CALL(neogeo);
 }

 static DRIVER_INIT( kof2k4se )
 {
 decrypt_kof2k4se_68k(machine);
 DRIVER_INIT_CALL(neogeo);
 }

 static DRIVER_INIT( matrimbl )
 {
 matrim_decrypt_68k(machine);
 neogeo_fixed_layer_bank_type = 2;
 matrimbl_decrypt(machine);
 neogeo_sfix_decrypt(machine);
 DRIVER_INIT_CALL(neogeo);
 }

 */

int init_matrim(GAME_ROMS *r) {
	if (need_decrypt) {
		matrim_decrypt_68k(r);
		neo_pcm2_swap(r, 1);
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x6a);
	}
	neogeo_fix_bank_type = 2;
	return 0;
}

int init_pnyaa(GAME_ROMS *r) {
	if (need_decrypt) {
		neo_pcm2_snk_1999(r, 4);
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x2e);
	}
	neogeo_fix_bank_type = 1;
	return 0;
}

int init_mslug5(GAME_ROMS *r) {
	if (need_decrypt) {
		mslug5_decrypt_68k(r);
		neo_pcm2_swap(r, 2);
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x19);
	}
	neogeo_fix_bank_type = 1;
	//install_pvc_protection(r);
	return 0;
}

/*
 TODO:
 static TIMER_CALLBACK( ms5pcb_bios_timer_callback )
 {
 int harddip3 = input_port_read(machine, "HARDDIP") & 1;
 memory_set_bankptr(machine, NEOGEO_BANK_BIOS, memory_region(machine, "mainbios")
 +0x20000+harddip3*0x20000);
 }

 */
int init_ms5pcb(GAME_ROMS *r) {

	/* TODO: start a timer that will check the BIOS select DIP every second */
	//timer_set(machine, attotime_zero, NULL, 0, ms5pcb_bios_timer_callback);
	//timer_pulse(machine, ATTOTIME_IN_MSEC(1000), NULL, 0, ms5pcb_bios_timer_callback);
	if (need_decrypt) {
		mslug5_decrypt_68k(r);
		svcpcb_gfx_decrypt(r);
		neogeo_cmc50_m1_decrypt(r);
		kof2000_neogeo_gfx_decrypt(r, 0x19);
		svcpcb_s1data_decrypt(r);
		neo_pcm2_swap(r, 2);
	}
	neogeo_fix_bank_type = 2;
	//install_pvc_protection(r);
	return 0;
}

int init_ms5plus(GAME_ROMS *r) {
	/* TODO: Bootleg */
	if (need_decrypt) {
		cmc50_neogeo_gfx_decrypt(r, 0x19);
		neo_pcm2_swap(r, 2);
		//neogeo_bootleg_sx_decrypt(r, 1);
	}
	neogeo_fix_bank_type = 1;

	//install_ms5plus_protection(r);
	return 0;
}
#if 0
// TODO:

static TIMER_CALLBACK(svcpcb_bios_timer_callback) {
	int harddip3 = input_port_read(machine, "HARDDIP") & 1;
	memory_set_bankptr(machine, NEOGEO_BANK_BIOS, memory_region(machine, "mainbios")
			+ 0x20000 + harddip3 * 0x20000);
}

static DRIVER_INIT(svcpcb) {
	/* start a timer that will check the BIOS select DIP every second */
	timer_set(machine, attotime_zero, NULL, 0, svcpcb_bios_timer_callback);
	timer_pulse(machine, ATTOTIME_IN_MSEC(1000), NULL, 0, svcpcb_bios_timer_callback);

	svc_px_decrypt(machine);
	svcpcb_gfx_decrypt(machine);
	neogeo_cmc50_m1_decrypt(machine);
	kof2000_neogeo_gfx_decrypt(machine, 0x57);
	svcpcb_s1data_decrypt(machine);
	neo_pcm2_swap(machine, 3);
	neogeo_fixed_layer_bank_type = 2;
	DRIVER_INIT_CALL(neogeo);
	install_pvc_protection(machine);
}

static DRIVER_INIT(svc) {
	svc_px_decrypt(machine);
	neo_pcm2_swap(machine, 3);
	neogeo_fixed_layer_bank_type = 2;
	neogeo_cmc50_m1_decrypt(machine);
	kof2000_neogeo_gfx_decrypt(machine, 0x57);
	DRIVER_INIT_CALL(neogeo);
	install_pvc_protection(machine);
}

static DRIVER_INIT(svcboot) {
	svcboot_px_decrypt(machine);
	svcboot_cx_decrypt(machine);
	DRIVER_INIT_CALL(neogeo);
	install_pvc_protection(machine);
}

static DRIVER_INIT(svcplus) {
	svcplus_px_decrypt(machine);
	svcboot_cx_decrypt(machine);
	neogeo_bootleg_sx_decrypt(machine, 1);
	svcplus_px_hack(machine);
	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(svcplusa) {
	svcplusa_px_decrypt(machine);
	svcboot_cx_decrypt(machine);
	svcplus_px_hack(machine);
	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(svcsplus) {
	svcsplus_px_decrypt(machine);
	neogeo_bootleg_sx_decrypt(machine, 2);
	svcboot_cx_decrypt(machine);
	svcsplus_px_hack(machine);
	DRIVER_INIT_CALL(neogeo);
	install_pvc_protection(machine);
}

static DRIVER_INIT(samsho5) {
	samsho5_decrypt_68k(machine);
	neo_pcm2_swap(machine, 4);
	neogeo_fixed_layer_bank_type = 1;
	neogeo_cmc50_m1_decrypt(machine);
	kof2000_neogeo_gfx_decrypt(machine, 0x0f);
	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(samsho5b) {
	samsho5b_px_decrypt(machine);
	samsho5b_vx_decrypt(machine);
	neogeo_bootleg_sx_decrypt(machine, 1);
	neogeo_bootleg_cx_decrypt(machine);
	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(kf2k3pcb) {
	kf2k3pcb_decrypt_68k(machine);
	kf2k3pcb_gfx_decrypt(machine);
	kof2003biosdecode(machine);
	neogeo_cmc50_m1_decrypt(machine);

	/* extra little swap on the m1 - this must be performed AFTER the m1 decrypt
	 or the m1 checksum (used to generate the key) for decrypting the m1 is
	 incorrect */
	{
		int i;
		UINT8* rom = memory_region(machine, "audiocpu");
		for (i = 0; i < 0x90000; i++) {
			rom[i] = BITSWAP8(rom[i], 5, 6, 1, 4, 3, 0, 7, 2);
		}

	}

	kof2000_neogeo_gfx_decrypt(machine, 0x9d);
	kf2k3pcb_decrypt_s1data(machine);
	neo_pcm2_swap(machine, 5);
	neogeo_fixed_layer_bank_type = 2;
	DRIVER_INIT_CALL(neogeo);
	install_pvc_protection(machine);
	memory_install_read16_handler(cputag_get_address_space(machine, "maincpu",
			ADDRESS_SPACE_PROGRAM), 0xc00000, 0xc7ffff, 0, 0,
			(read16_space_func) SMH_BANK(6)); // 512k bios
}

static DRIVER_INIT(kof2003) {
	kof2003_decrypt_68k(machine);
	neo_pcm2_swap(machine, 5);
	neogeo_fixed_layer_bank_type = 2;
	neogeo_cmc50_m1_decrypt(machine);
	kof2000_neogeo_gfx_decrypt(machine, 0x9d);
	DRIVER_INIT_CALL(neogeo);
	install_pvc_protection(machine);
}

static DRIVER_INIT(kof2003h) {
	kof2003h_decrypt_68k(machine);
	neo_pcm2_swap(machine, 5);
	neogeo_fixed_layer_bank_type = 2;
	neogeo_cmc50_m1_decrypt(machine);
	kof2000_neogeo_gfx_decrypt(machine, 0x9d);
	DRIVER_INIT_CALL(neogeo);
	install_pvc_protection(machine);
}

static DRIVER_INIT(kf2k3bl) {
	cmc50_neogeo_gfx_decrypt(machine, 0x9d);
	neo_pcm2_swap(machine, 5);
	neogeo_bootleg_sx_decrypt(machine, 1);
	DRIVER_INIT_CALL(neogeo);
	kf2k3bl_install_protection(machine);
}

static DRIVER_INIT(kf2k3pl) {
	cmc50_neogeo_gfx_decrypt(machine, 0x9d);
	neo_pcm2_swap(machine, 5);
	kf2k3pl_px_decrypt(machine);
	neogeo_bootleg_sx_decrypt(machine, 1);
	DRIVER_INIT_CALL(neogeo);
	kf2k3pl_install_protection(machine);
}

static DRIVER_INIT(kf2k3upl) {
	cmc50_neogeo_gfx_decrypt(machine, 0x9d);
	neo_pcm2_swap(machine, 5);
	kf2k3upl_px_decrypt(machine);
	neogeo_bootleg_sx_decrypt(machine, 2);
	DRIVER_INIT_CALL(neogeo);
	kf2k3upl_install_protection(machine);
}

static DRIVER_INIT(samsh5sp) {
	samsh5sp_decrypt_68k(machine);
	neo_pcm2_swap(machine, 6);
	neogeo_fixed_layer_bank_type = 1;
	neogeo_cmc50_m1_decrypt(machine);
	kof2000_neogeo_gfx_decrypt(machine, 0x0d);
	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(jockeygp) {
	UINT16* extra_ram;

	neogeo_fixed_layer_bank_type = 1;
	neogeo_cmc50_m1_decrypt(machine);
	kof2000_neogeo_gfx_decrypt(machine, 0xac);

	/* install some extra RAM */
	extra_ram = auto_alloc_array(machine, UINT16, 0x2000 / 2);
	state_save_register_global_pointer(machine, extra_ram, 0x2000 / 2);

	memory_install_readwrite16_handler(cputag_get_address_space(machine, "maincpu",
			ADDRESS_SPACE_PROGRAM), 0x200000, 0x201fff, 0, 0,
			(read16_space_func) SMH_BANK(8), (write16_space_func) SMH_BANK(8));
	memory_set_bankptr(machine, NEOGEO_BANK_EXTRA_RAM, extra_ram);

	//  memory_install_read_port_handler(cputag_get_address_space(machine,
	//"maincpu", ADDRESS_SPACE_PROGRAM), 0x280000, 0x280001, 0, 0, "IN5");
	//  memory_install_read_port_handler(cputag_get_address_space(machine,
	//"maincpu", ADDRESS_SPACE_PROGRAM), 0x2c0000, 0x2c0001, 0, 0, "IN6");

	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(vliner) {
	UINT16* extra_ram;

	/* install some extra RAM */
	extra_ram = auto_alloc_array(machine, UINT16, 0x2000 / 2);
	state_save_register_global_pointer(machine, extra_ram, 0x2000 / 2);

	memory_install_readwrite16_handler(cputag_get_address_space(machine, "maincpu",
			ADDRESS_SPACE_PROGRAM), 0x200000, 0x201fff, 0, 0, (read16_space_func)
			SMH_BANK(8), (write16_space_func) SMH_BANK(8));
	memory_set_bankptr(machine, NEOGEO_BANK_EXTRA_RAM, extra_ram);

	memory_install_read_port_handler(cputag_get_address_space(machine, "maincpu",
			ADDRESS_SPACE_PROGRAM), 0x280000, 0x280001, 0, 0, "IN5");
	memory_install_read_port_handler(cputag_get_address_space(machine, "maincpu",
			ADDRESS_SPACE_PROGRAM), 0x2c0000, 0x2c0001, 0, 0, "IN6");

	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(kog) {
	/* overlay cartridge ROM */
	memory_install_read_port_handler(cputag_get_address_space(machine, "maincpu",
			ADDRESS_SPACE_PROGRAM), 0x0ffffe, 0x0fffff, 0, 0, "JUMPER");

	kog_px_decrypt(machine);
	neogeo_bootleg_sx_decrypt(machine, 1);
	neogeo_bootleg_cx_decrypt(machine);
	DRIVER_INIT_CALL(neogeo);
}

static DRIVER_INIT(lans2004) {
	lans2004_decrypt_68k(machine);
	lans2004_vx_decrypt(machine);
	neogeo_bootleg_sx_decrypt(machine, 1);
	neogeo_bootleg_cx_decrypt(machine);
	DRIVER_INIT_CALL(neogeo);
}

#endif

struct roms_init_func {
	char *name;
	int (*init)(GAME_ROMS * r);
} init_func_table[] = {
	//	{"mslugx",init_mslugx},
	{ "kof99", init_kof99},
	{ "kof99n", init_kof99n},
	{ "garou", init_garou},
	{ "garouo", init_garouo},
	//	{"garoup",init_garoup},
	{ "garoubl", init_garoubl},
	{ "mslug3", init_mslug3},
	{ "mslug3h", init_mslug3h},
	{ "mslug3n", init_mslug3h},
	{ "mslug3b6", init_mslug3b6},
	{ "kof2000", init_kof2000},
	{ "kof2000n", init_kof2000n},
	{ "kof2001", init_kof2001},
	{ "mslug4", init_mslug4},
	{ "ms4plus", init_ms4plus},
	{ "ganryu", init_ganryu},
	{ "s1945p", init_s1945p},
	{ "preisle2", init_preisle2},
	{ "bangbead", init_bangbead},
	{ "nitd", init_nitd},
	{ "zupapa", init_zupapa},
	{ "sengoku3", init_sengoku3},
	{ "kof98", init_kof98},
	{ "rotd", init_rotd},
	{ "kof2002", init_kof2002},
	{ "kof2002b", init_kof2002b},
	{ "kf2k2pls", init_kf2k2pls},
	{ "kf2k2mp", init_kf2k2mp},
	{ "kof2km2", init_kof2km2},
	{ "matrim", init_matrim},
	{ "pnyaa", init_pnyaa},
	{ "mslug5", init_mslug5},
	{ "ms5pcb", init_ms5pcb},
	{ "ms5plus", init_ms5plus},
	{ NULL, NULL}
};

/* Forward declaration for streaming cache support */
static int convert_roms_tile(Uint8 *g, int tileno);

static int allocate_region(ROM_REGION *r, Uint32 size, int region) {
	DEBUG_LOG("Allocating 0x%08x byte for Region %d\n", size, region);
	if (size != 0) {
#ifdef GP2X
		switch (region) {
			case REGION_AUDIO_CPU_CARTRIDGE:
				r->p = gp2x_ram_malloc(size, 1);
#ifdef ENABLE_940T
				shared_data->sm1 = (Uint8*) ((r->p - gp2x_ram2) + 0x1000000);
				printf("Z80 code: %08x\n", (Uint32) shared_data->sm1);
#endif
				break;
			case REGION_AUDIO_DATA_1:
				r->p = gp2x_ram_malloc(size, 0);
#ifdef ENABLE_940T
				shared_data->pcmbufa = (Uint8*) (r->p - gp2x_ram);
				printf("SOUND1 code: %08x\n", (Uint32) shared_data->pcmbufa);
				shared_data->pcmbufa_size = size;
#endif
				break;
			case REGION_AUDIO_DATA_2:
				r->p = gp2x_ram_malloc(size, 0);
#ifdef ENABLE_940T
				shared_data->pcmbufb = (Uint8*) (r->p - gp2x_ram);
				printf("SOUND2 code: %08x\n", (Uint32) shared_data->pcmbufa);
				shared_data->pcmbufb_size = size;
#endif
				break;
			default:
				r->p = malloc(size);
				break;

		}
#else
		r->p = malloc(size);
#endif
		if (r->p == 0) {
			r->size = 0;
			printf("Error allocating 0x%x bytes for region %d\n", size, region);
			printf("Not enough memory! Free PSRAM: %u KB\n",
				   (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
			return 1;
		}
		memset(r->p, 0, size);
	} else
		r->p = NULL;
	r->size = size;
	return 0;
}

static void free_region(ROM_REGION *r) {
	DEBUG_LOG("Free Region %p %p %d\n", r, r->p, r->size);
	if (r->p)
		free(r->p);
	r->size = 0;
	r->p = NULL;
}

/*
 * Streaming ROM cache support for large Neo Geo games.
 *
 * When sprite tiles (C ROM) or ADPCM samples (V ROM) are too large for
 * PSRAM, we extract them to flat files on the SD card and use paged
 * caches in PSRAM for runtime access.
 *
 * Cache file paths:  /sd/roms/neogeo/<game>.ctile  (converted sprite tiles)
 *                    /sd/roms/neogeo/<game>.vroma   (ADPCM-A samples)
 *                    /sd/roms/neogeo/<game>.vromb   (ADPCM-B samples, if different)
 *
 * Sprite cache budget: 12 MB in PSRAM, bank size 4096 bytes (32 tiles).
 * ADPCM cache budget:  4 MB in PSRAM, page size 4096 bytes.
 */

#define SPRITE_CACHE_BUDGET   (12 * 1024 * 1024)
#define SPRITE_BANK_SIZE      4096
#define ADPCM_CACHE_BUDGET    (4 * 1024 * 1024)
#define CUSAGE_MAGIC          0x43553039  /* "CU09" — unbuffered I/O for read-modify-write */

/* Minimum free PSRAM to keep after all allocations (headroom for stacks, etc.) */
#define PSRAM_HEADROOM        (2 * 1024 * 1024)

static int streaming_tiles = 0;   /* 1 if sprite tiles are streamed from SD */
static int streaming_adpcma = 0;  /* 1 if ADPCM-A is streamed from SD */
static int streaming_adpcmb = 0;  /* 1 if ADPCM-B is streamed from SD */
static FILE *stream_tile_file = NULL;   /* temp file handle during cache creation */
static FILE *stream_adpcma_file = NULL;
static FILE *stream_adpcmb_file = NULL;

static void get_cache_path(const char *name, const char *ext, char *out, int maxlen) {
	snprintf(out, maxlen, "%s%s/%s.%s", ROOTPATH, name, name, ext);
}

static int cache_file_exists(const char *path) {
	struct stat st;
	return (stat(path, &st) == 0 && st.st_size > 0);
}

/* Check that a .cusage file has the correct magic header */
static int cusage_file_valid(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	Uint32 magic = 0;
	fread(&magic, sizeof(magic), 1, f);
	fclose(f);
	return (magic == CUSAGE_MAGIC);
}

static int read_counter;  /* Moved here so write_data_*_to_file can use it */

/* Write interleaved sprite data from ZIP to a file (same as read_data_i but to file).
 * Reads from ZIP, scatters bytes at stride-2 into a file buffer to avoid byte-by-byte seeks.
 * Uses read-modify-write with a 8KB file buffer for each 4KB of input. */
static int write_data_i_to_file(ZFILE *gz, FILE *out, Uint32 dest, Uint32 size, Uint32 total_size) {
	Uint32 s = 4096;   /* input chunk size */
	Uint8 *inbuf = malloc(s);
	Uint8 *filebuf = malloc(s * 2);  /* output covers 2x the input (stride 2) */
	if (!inbuf || !filebuf) {
		free(inbuf); free(filebuf);
		return -1;
	}

	Uint32 odd = dest & 1;  /* 0 for even C ROM, 1 for odd C ROM — constant for entire call */
	Uint32 total_read = 0, total_nonzero = 0;

	while (size) {
		Uint32 c = size;
		if (c > s) c = s;
		c = gn_unzip_fread(gz, inbuf, c);
		if (c == 0) break;

		/* Count non-zero bytes from ZIP */
		for (Uint32 i = 0; i < c; i++) { if (inbuf[i]) total_nonzero++; }
		total_read += c;

		/* Align file region to even boundary for read-modify-write */
		Uint32 out_start = dest & ~1U;
		Uint32 out_len = c * 2;

		/* Clamp to file size */
		if (out_start + out_len > total_size)
			out_len = total_size - out_start;

		/* Read existing file contents (contains the other interleave half or zeros) */
		fseek(out, out_start, SEEK_SET);
		fread(filebuf, 1, out_len, out);

		/* Scatter input bytes at correct interleave offsets */
		for (Uint32 i = 0; i < c && (i * 2 + odd) < out_len; i++) {
			filebuf[i * 2 + odd] = inbuf[i];
		}

		/* Write back the merged buffer */
		fseek(out, out_start, SEEK_SET);
		fwrite(filebuf, 1, out_len, out);

		dest += c * 2;
		size -= c;
		read_counter += c;
		gn_update_pbar(read_counter);
	}
	fflush(out);
	fsync(fileno(out));
	printf("  write_data_i: read=%u bytes, nonzero=%u (%.1f%%)\n",
		   total_read, total_nonzero, total_read ? (total_nonzero * 100.0f / total_read) : 0.0f);
	free(inbuf);
	free(filebuf);
	return 0;
}

/* Write sequential data from ZIP to a file (same as read_data_p but to file) */
static int write_data_p_to_file(ZFILE *gz, FILE *out, Uint32 dest, Uint32 size) {
	Uint8 *buf;
	Uint32 s = 4096, c;
	buf = malloc(s);
	if (!buf) return -1;

	fseek(out, dest, SEEK_SET);
	while (size) {
		c = size;
		if (c > s) c = s;
		c = gn_unzip_fread(gz, buf, c);
		if (c == 0) break;
		fwrite(buf, 1, c, out);
		size -= c;
		read_counter += c;
		gn_update_pbar(read_counter);
	}
	free(buf);
	return 0;
}

/* Convert raw tile data in a file to decoded 4bpp format, in chunks.
 * Also builds spr_usage table in memory. */
static void convert_tiles_in_file(FILE *f, Uint32 total_size, GAME_ROMS *r) {
	Uint32 nb_tiles = total_size >> 7;
	Uint32 bank_tiles = SPRITE_BANK_SIZE >> 7;  /* tiles per bank */
	Uint8 *bank_buf = malloc(SPRITE_BANK_SIZE);
	if (!bank_buf) return;

	/* Allocate spr_usage in PSRAM (small: nb_tiles/16 * 4 bytes) */
	allocate_region(&r->spr_usage, (total_size >> 11) * sizeof(Uint32), REGION_SPR_USAGE);
	memset(r->spr_usage.p, 0, r->spr_usage.size);

	printf("Converting %u tiles in file (%u banks)...\n", nb_tiles, (nb_tiles + bank_tiles - 1) / bank_tiles);

	for (Uint32 tile = 0; tile < nb_tiles; tile += bank_tiles) {
		Uint32 chunk_tiles = bank_tiles;
		if (tile + chunk_tiles > nb_tiles)
			chunk_tiles = nb_tiles - tile;
		Uint32 chunk_bytes = chunk_tiles << 7;

		/* Read raw bank from file */
		fseek(f, (long)tile << 7, SEEK_SET);
		fread(bank_buf, 1, chunk_bytes, f);

		/* Convert each tile in the bank (in-place in bank_buf) */
		for (Uint32 t = 0; t < chunk_tiles; t++) {
			Uint32 global_tileno = tile + t;
			int usage = convert_roms_tile(bank_buf, t);
			((Uint32 *)r->spr_usage.p)[global_tileno >> 4] |= usage;
		}

		/* Write converted bank back to file */
		fseek(f, (long)tile << 7, SEEK_SET);
		fwrite(bank_buf, 1, chunk_bytes, f);
	}
	fflush(f);
	free(bank_buf);
	printf("Tile conversion complete.\n");
}

/* Set up the sprite streaming cache from a .ctile file */
static int setup_sprite_streaming(const char *ctile_path, Uint32 tile_size) {
	GFX_CACHE *gcache = &memory.vid.spr_cache;

	/* Allocate FATFS FIL structure in internal RAM so that its embedded
	 * sector buffer is DMA-reachable for the init-time sector map build. */
	FIL *fil = heap_caps_calloc(1, sizeof(FIL), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if (!fil) {
		printf("ERROR: Cannot allocate FIL in internal RAM (%u bytes, %u free)\n",
			   (unsigned)sizeof(FIL),
			   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
		return GN_FALSE;
	}

	/* Build FATFS path: strip VFS mount-point "/sd" and prepend drive "0:" */
	char fatfs_path[256];
	const char *rel = ctile_path;
	if (strncmp(rel, "/sd/", 4) == 0) rel += 3;   /* keep leading '/' */
	snprintf(fatfs_path, sizeof(fatfs_path), "0:%s", rel);

	FRESULT fres = f_open(fil, fatfs_path, FA_READ);
	if (fres != FR_OK) {
		printf("ERROR: f_open(%s) failed, FRESULT=%d\n", fatfs_path, (int)fres);
		heap_caps_free(fil);
		return GN_FALSE;
	}
	printf("Sprite cache opened via FATFS: %s (FIL in internal RAM, %u bytes)\n",
		   fatfs_path, (unsigned)sizeof(FIL));

	/* --- Build LBA sector map by walking the FAT chain directly ---
	 * FATFS f_lseek has a subtle bug: at cluster boundaries, fp->clust
	 * points to the PREVIOUS cluster (because `while (ofs > bcs)` uses
	 * strict greater-than).  Also, fp->clust is invalid when fptr==0.
	 * This causes 25% of banks (every 4th) to map to wrong sectors.
	 *
	 * Instead, we walk the FAT chain ourselves using disk_read() on
	 * the FAT table sectors.  This is correct by construction and
	 * avoids all FATFS internal state issues. */
	FATFS *fs = fil->obj.fs;
	WORD ssize = fs->ssize;          /* SD card sector size (512) */
	WORD csize = fs->csize;          /* cluster size in sectors */
	LBA_t database = fs->database;   /* first data sector LBA */
	LBA_t fatbase = fs->fatbase;     /* FAT table start sector */
	BYTE pdrv = fs->pdrv;
	BYTE fs_type = fs->fs_type;
	int sectors_per_bank = SPRITE_BANK_SIZE / ssize;
	int banks_per_cluster = (csize * ssize) / SPRITE_BANK_SIZE;
	DWORD total_banks = fil->obj.objsize / SPRITE_BANK_SIZE;
	DWORD sclust = fil->obj.sclust;

	printf("Sector map: ssize=%u csize=%u database=%lu fatbase=%lu sclust=%lu spb=%d bpc=%d banks=%lu fs_type=%d\n",
		   ssize, csize, (unsigned long)database, (unsigned long)fatbase,
		   (unsigned long)sclust, sectors_per_bank, banks_per_cluster,
		   (unsigned long)total_banks, fs_type);

	/* Close FATFS file — we only needed it to get the filesystem params
	 * and start cluster.  All further reads go through disk_read(). */
	f_close(fil);
	heap_caps_free(fil);
	fil = NULL;

	if (banks_per_cluster < 1) {
		printf("ERROR: cluster size %u bytes < bank size %d\n",
			   (unsigned)(csize * ssize), SPRITE_BANK_SIZE);
		return GN_FALSE;
	}

	DWORD *sector_map = calloc(total_banks, sizeof(DWORD));
	if (!sector_map) {
		printf("ERROR: sector_map alloc failed (%lu bytes)\n",
			   (unsigned long)(total_banks * sizeof(DWORD)));
		return GN_FALSE;
	}

	/* Allocate a DMA-aligned bounce buffer for reading FAT sectors.
	 * We reuse the sprite bounce buffer (allocated later), so use
	 * a temporary one here — one sector is enough. */
	Uint8 *fat_buf = heap_caps_aligned_alloc(64, ssize, MALLOC_CAP_DMA);
	if (!fat_buf) {
		printf("ERROR: FAT read buffer alloc failed\n");
		free(sector_map);
		return GN_FALSE;
	}

	/* Walk the cluster chain from sclust, filling sector_map */
	DWORD clust = sclust;
	DWORD bank = 0;
	LBA_t cached_fat_sect = 0;  /* which FAT sector is in fat_buf */

	while (bank < total_banks && clust >= 2 && clust < fs->n_fatent) {
		/* Compute LBA of first sector in this cluster */
		LBA_t base_sect = database + (LBA_t)(clust - 2) * csize;

		/* Fill sector_map entries for all banks within this cluster */
		for (int b = 0; b < banks_per_cluster && bank < total_banks; b++, bank++) {
			sector_map[bank] = (DWORD)(base_sect + b * sectors_per_bank);
		}

		/* Follow FAT chain to next cluster */
		LBA_t fat_sect;
		UINT fat_off;
		if (fs_type == FS_FAT32) {
			fat_sect = fatbase + (clust / (ssize / 4));
			fat_off = (clust % (ssize / 4)) * 4;
		} else {
			/* FAT16 */
			fat_sect = fatbase + (clust / (ssize / 2));
			fat_off = (clust % (ssize / 2)) * 2;
		}

		/* Read FAT sector if not already cached */
		if (fat_sect != cached_fat_sect) {
			DRESULT dres = disk_read(pdrv, fat_buf, fat_sect, 1);
			if (dres != RES_OK) {
				printf("ERROR: FAT sector read failed at LBA %lu\n",
					   (unsigned long)fat_sect);
				heap_caps_free(fat_buf);
				free(sector_map);
				return GN_FALSE;
			}
			cached_fat_sect = fat_sect;
		}

		if (fs_type == FS_FAT32) {
			clust = (*(DWORD *)(fat_buf + fat_off)) & 0x0FFFFFFF;
		} else {
			clust = *(WORD *)(fat_buf + fat_off);
		}
	}

	heap_caps_free(fat_buf);

	if (bank < total_banks) {
		printf("ERROR: FAT chain ended early at bank %lu/%lu (clust=%lu)\n",
			   (unsigned long)bank, (unsigned long)total_banks,
			   (unsigned long)clust);
		free(sector_map);
		return GN_FALSE;
	}

	printf("Sector map built: bank[0]=LBA %lu, bank[%lu]=LBA %lu\n",
		   (unsigned long)sector_map[0],
		   (unsigned long)(total_banks - 1),
		   (unsigned long)sector_map[total_banks - 1]);

	/* Set up raw mode */
	gcache->sector_map = sector_map;
	gcache->sectors_per_bank = sectors_per_bank;
	gcache->pdrv = pdrv;
	gcache->gno = NULL;      /* not used in raw mode */
	gcache->offset = NULL;
	gcache->raw_mode = 1;

	/* tiles region: keep the size but p is NULL (data is on SD) */
	memory.rom.tiles.size = tile_size;
	memory.rom.tiles.p = NULL;
	memory.nb_of_tiles = tile_size >> 7;

	/* Calculate max sprite cache: leave room for ADPCM cache + IPC pool + headroom.
	 * ADPCM streaming needs up to 4MB, IPC pool needs at least 1MB,
	 * plus 2MB general headroom for task stacks, framebuffers, etc. */
	Uint32 free_now = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	Uint32 reserve = (streaming_adpcma ? ADPCM_CACHE_BUDGET : 0) + (3 * 1024 * 1024);
	Uint32 max_sprite_cache = (free_now > reserve) ? (free_now - reserve) : (2 * 1024 * 1024);

	/* Try cache sizes from large to small, limited by max_sprite_cache */
	Uint32 cache_sizes[] = { 12, 8, 6, 4, 2, 0 };
	for (int i = 0; cache_sizes[i] != 0; i++) {
		Uint32 cache_bytes = cache_sizes[i] * 1024 * 1024;
		if (cache_bytes > max_sprite_cache)
			continue;  /* skip sizes that would starve other allocations */
		if (init_sprite_cache(cache_bytes, SPRITE_BANK_SIZE) == GN_TRUE) {
			printf("Sprite streaming: %u MB cache, %u tiles, bank=%d\n",
				   cache_sizes[i], memory.nb_of_tiles, SPRITE_BANK_SIZE);
			/* DMA-aligned bounce buffer in internal RAM for SDMMC DMA. */
			gcache->bounce_buf = heap_caps_aligned_alloc(64, SPRITE_BANK_SIZE,
														 MALLOC_CAP_DMA);
			if (gcache->bounce_buf)
				printf("Sprite bounce buffer: %d bytes (DMA @ %p, free=%u)\n",
					   SPRITE_BANK_SIZE, gcache->bounce_buf,
					   (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
			else
				printf("WARNING: bounce buffer alloc failed (%u free DMA)\n",
					   (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
			return GN_TRUE;
		}
	}

	printf("ERROR: Could not allocate sprite cache\n");
	free(sector_map);
	gcache->sector_map = NULL;
	return GN_FALSE;
}

/* Set up ADPCM streaming cache from a .vrom file */
static int setup_adpcm_streaming(adpcm_cache_t *cache, const char *vrom_path,
                                  Uint32 vrom_size, ROM_REGION *region) {
	FILE *f = fopen(vrom_path, "rb");
	if (!f) {
		printf("ERROR: Cannot open ADPCM cache %s\n", vrom_path);
		return GN_FALSE;
	}

	/* Try cache sizes from large to small */
	Uint32 cache_sizes[] = { 4, 2, 1, 0 };
	for (int i = 0; cache_sizes[i] != 0; i++) {
		Uint32 cache_bytes = cache_sizes[i] * 1024 * 1024;
		if (adpcm_cache_init(cache, f, vrom_size, cache_bytes) == 0) {
			printf("ADPCM streaming: %u MB cache (ROM: %u MB)\n",
				   cache_sizes[i], vrom_size / (1024 * 1024));
			/* Set region size but p = NULL (data on SD) */
			region->size = vrom_size;
			region->p = NULL;
			return GN_TRUE;
		}
	}

	printf("ERROR: Could not allocate ADPCM cache\n");
	fclose(f);
	return GN_FALSE;
}

static int zip_seek_current_file(ZFILE *gz, Uint32 offset) {
	Uint8 *buf;
	Uint32 s = 4096, c;
	buf = malloc(s);
	if (!buf)
		return -1;
	while (offset) {
		c = offset;
		if (c > s)
			c = s;

		c = gn_unzip_fread(gz, buf, c);
		if (c == 0) {
			break;
		}
		offset -= c;
	}
	free(buf);
	return 0;

}

/* read_counter declared earlier (before write_data_*_to_file functions) */

static int read_data_i(ZFILE *gz, ROM_REGION *r, Uint32 dest, Uint32 size) {
	//Uint8 *buf;
	Uint8 *p = r->p + dest;
	Uint32 s = LOAD_BUF_SIZE, c, i;
	if (r->p == NULL || r->size < (dest & ~0x1) + (size * 2)) {
		printf("Region not allocated or not big enough %08x %08x\n", r->size,
				dest + (size * 2));
		return -1;
	}
	//buf=malloc(s);
	if (!iloadbuf)
		return -1;

	while (size) {
		c = size;
		if (c > s)
			c = s;

		c = gn_unzip_fread(gz, iloadbuf, c);
		if (c == 0) {
			//free(buf);
			return 0;
		}
		for (i = 0; i < c; i++) {
			//printf("%d %d\n",i,c);
			*p = iloadbuf[i];
			p += 2;
		}
		size -= c;
		read_counter += c;
		gn_update_pbar(read_counter);
	}
	//free(buf);
	return 0;
}

static int read_data_p(ZFILE *gz, ROM_REGION *r, Uint32 dest, Uint32 size) {
	Uint32 s = LOAD_BUF_SIZE, c, i = 0;
	if (r->p == NULL || r->size < dest + size) {
		printf("Region not allocated or not big enough\n");
		return -1;
	}
	while (size) {
		c = size;
		if (c > s)
			c = s;
		c = gn_unzip_fread(gz, r->p + dest + i, c);
		if (c == 0) {
			//free(buf);
			return 0;
		}
		i += c;
		size -= c;
		read_counter += c;
		gn_update_pbar(read_counter);
	}

	return 0;
}

static int load_region(PKZIP *pz, GAME_ROMS *r, int region, Uint32 src,
		Uint32 dest, Uint32 size, Uint32 crc, char *filename) {
	int rc;
	int badcrc = 0;
	ZFILE *gz;

	gz = gn_unzip_fopen(pz, filename, crc);
	if (gz == NULL) {
		DEBUG_LOG("KO\n");
		DEBUG_LOG("Load file %-17s in region %d: KO\n", filename, region);
		return 1;
	}

	if (src != 0) { /* TODO: Reuse an allready opened zfile */

		if (region == REGION_SPRITES)
			rc = zip_seek_current_file(gz, src / 2);
		else
			rc = zip_seek_current_file(gz, src);
		DEBUG_LOG("setoffset: %d %08x %08x %08x\n", rc, src, dest, size);
	}

	DEBUG_LOG("Trying to load file %-17s in region %d\n", filename, region);

	switch (region) {
		case REGION_SPRITES: /* Special interleaved loading  */
			if (streaming_tiles && stream_tile_file) {
				/* Write interleaved data to cache file instead of PSRAM */
				printf("TILE_LOAD: file=%s dest=0x%08X size=0x%08X (odd=%u)\n", filename, dest, size, dest & 1);
				write_data_i_to_file(gz, stream_tile_file, dest, size, r->tiles.size);
			} else if (r->tiles.p) {
				read_data_i(gz, &r->tiles, dest, size);
			}
			break;
		case REGION_AUDIO_CPU_CARTRIDGE:
			read_data_p(gz, &r->cpu_z80, dest, size);
			break;
		case REGION_AUDIO_CPU_ENCRYPTED:
			read_data_p(gz, &r->cpu_z80c, dest, size);
			break;
		case REGION_MAIN_CPU_CARTRIDGE:
			read_data_p(gz, &r->cpu_m68k, dest, size);
			break;
		case REGION_FIXED_LAYER_CARTRIDGE:
			read_data_p(gz, &r->game_sfix, dest, size);
			break;
		case REGION_AUDIO_DATA_1:
			if (streaming_adpcma && stream_adpcma_file) {
				write_data_p_to_file(gz, stream_adpcma_file, dest, size);
			} else if (r->adpcma.p) {
				read_data_p(gz, &r->adpcma, dest, size);
			}
			break;
		case REGION_AUDIO_DATA_2:
			if (streaming_adpcmb && stream_adpcmb_file) {
				write_data_p_to_file(gz, stream_adpcmb_file, dest, size);
			} else if (r->adpcmb.p) {
				read_data_p(gz, &r->adpcmb, dest, size);
			}
			break;
		case REGION_MAIN_CPU_BIOS:
			read_data_p(gz, &r->bios_m68k, dest, size);
			break;
		case REGION_AUDIO_CPU_BIOS:
			read_data_p(gz, &r->bios_m68k, dest, size);
			break;
		case REGION_FIXED_LAYER_BIOS:
			read_data_p(gz, &r->bios_sfix, dest, size);
			break;

		default:
			DEBUG_LOG("Unhandled region %d\n", region);
			break;

	}
	DEBUG_LOG("Load file %-17s in region %d: OK %s\n", filename, region,
			(badcrc ? "(Bad CRC)" : ""));
	//unzCloseCurrentFile(gz);
	gn_unzip_fclose(gz);
	return 0;
}

static PKZIP *open_rom_zip(char *rom_path, char *name) {
	char *buf;
	int size = strlen(rom_path) + strlen(name) + 6;
	PKZIP *gz;
	buf = malloc(size);
	snprintf(buf, size, "%s/%s.zip", rom_path, name);
	gz = gn_open_zip(buf);
	free(buf);
	return gz;
}

static int convert_roms_tile(Uint8 *g, int tileno) {
	unsigned char swap[128];
	unsigned int *gfxdata;
	int x, y;
	unsigned int pen, usage = 0;
	gfxdata = (Uint32*) & g[tileno << 7];

	memcpy(swap, gfxdata, 128);

	//filed=1;
	for (y = 0; y < 16; y++) {
		unsigned int dw;

		dw = 0;
		for (x = 0; x < 8; x++) {
			pen = ((swap[64 + (y << 2) + 3] >> x) & 1) << 3;
			pen |= ((swap[64 + (y << 2) + 1] >> x) & 1) << 2;
			pen |= ((swap[64 + (y << 2) + 2] >> x) & 1) << 1;
			pen |= (swap[64 + (y << 2)] >> x) & 1;
			//if (!pen) filed=0;
			dw |= pen << ((7 - x) << 2);
			//memory.pen_usage[tileno]  |= (1 << pen);
			usage |= (1 << pen);
		}
		*(gfxdata++) = dw;

		dw = 0;
		for (x = 0; x < 8; x++) {
			pen = ((swap[(y << 2) + 3] >> x) & 1) << 3;
			pen |= ((swap[(y << 2) + 1] >> x) & 1) << 2;
			pen |= ((swap[(y << 2) + 2] >> x) & 1) << 1;
			pen |= (swap[(y << 2)] >> x) & 1;
			//if (!pen) filed=0;
			dw |= pen << ((7 - x) << 2);
			//memory.pen_usage[tileno]  |= (1 << pen);
			usage |= (1 << pen);
		}
		*(gfxdata++) = dw;
	}

	//if ((usage & ~1) == 0) pen_usage|=(TILE_INVISIBLE<<((tileno&0xF)*2));
	/* TODO transpack support */
	if ((usage & ~1) == 0)
		return (TILE_INVISIBLE << ((tileno & 0xF) * 2));
	else
		return 0;

}

void convert_all_tile(GAME_ROMS *r) {
	Uint32 i;
	allocate_region(&r->spr_usage, (r->tiles.size >> 11) * sizeof (Uint32), REGION_SPR_USAGE);
	memset(r->spr_usage.p, 0, r->spr_usage.size);
	for (i = 0; i < r->tiles.size >> 7; i++) {
		((Uint32*) r->spr_usage.p)[i >> 4] |= convert_roms_tile(r->tiles.p, i);
	}
}

void convert_all_char(Uint8 *Ptr, int Taille,
		Uint8 *usage_ptr) {
	int i, j;
	unsigned char usage;

	Uint8 *Src;
	Uint8 *sav_src;

	Src = (Uint8*) malloc(Taille);
	if (!Src) {
		printf("Not enought memory!!\n");
		return;
	}
	sav_src = Src;
	memcpy(Src, Ptr, Taille);
#ifdef WORDS_BIGENDIAN
#define CONVERT_TILE *Ptr++ = *(Src+8);\
	             usage |= *(Src+8);\
                     *Ptr++ = *(Src);\
		     usage |= *(Src);\
		     *Ptr++ = *(Src+24);\
		     usage |= *(Src+24);\
		     *Ptr++ = *(Src+16);\
		     usage |= *(Src+16);\
		     Src++;
#else
#define CONVERT_TILE *Ptr++ = *(Src+16);\
	             usage |= *(Src+16);\
                     *Ptr++ = *(Src+24);\
		     usage |= *(Src+24);\
		     *Ptr++ = *(Src);\
		     usage |= *(Src);\
		     *Ptr++ = *(Src+8);\
		     usage |= *(Src+8);\
		     Src++;
#endif
	for (i = Taille; i > 0; i -= 32) {
		usage = 0;
		for (j = 0; j < 8; j++) {
			CONVERT_TILE
		}
		Src += 24;
		*usage_ptr++ = usage;
	}
	free(sav_src);
#undef CONVERT_TILE
}

static int init_roms(GAME_ROMS *r) {
	int i = 0;
	//printf("INIT ROM %s\n",r->info.name);
	neogeo_fix_bank_type = 0;
	memory.bksw_handler = 0;
	memory.bksw_unscramble = NULL;
	memory.bksw_offset = NULL;
	memory.sma_rng_addr = 0;
	memory.kof98_prot = 0;

	while (init_func_table[i].name) {
		//printf("INIT ROM ? %s %s\n",init_func_table[i].name,r->info.name);
		if (strcmp(init_func_table[i].name, r->info.name) == 0
				&& init_func_table[i].init != NULL) {
			DEBUG_LOG("Special init func\n");
			return init_func_table[i].init(r);
		}
		i++;
	}
	DEBUG_LOG("Default roms init\n");
	return 0;
}

int dr_load_bios(GAME_ROMS *r) {
	FILE *f;
	int i;
	PKZIP *pz;
	ZFILE *z;
	size_t totread = 0;
	unsigned int size;
	char *rpath = CF_STR(cf_get_item_by_name("rompath"));
	char *fpath;
	const char *romfile;
	fpath = malloc(strlen(rpath) + strlen("neogeo.zip") + 2);
	sprintf(fpath, "%s/neogeo.zip", rpath);

	printf("BIOS: Opening %s\n", fpath);
	pz = gn_open_zip(fpath);
	if (pz == NULL) {
		printf("BIOS: Can't open %s\n", fpath);
		gn_set_error_msg( "Can't open BIOS\n%s\n", fpath);
		free(fpath);
		return GN_FALSE;
	}

	memory.ng_lo = gn_unzip_file_malloc(pz, "000-lo.lo", 0x0, &size);
	if (memory.ng_lo == NULL) {
		printf("BIOS: 000-lo.lo not found, trying ng-lo.rom\n");
		memory.ng_lo = gn_unzip_file_malloc(pz, "ng-lo.rom", 0x0, &size);
	}
	if (memory.ng_lo == NULL) {
		printf("BIOS: Couldn't find 000-lo.lo or ng-lo.rom\n");
		gn_set_error_msg("Couldn't find 000-lo.lo\nPlease check your bios\n");
		return GN_FALSE;
	}
	printf("BIOS: lo loaded (%u bytes)\n", size);

	if (!(r->info.flags & HAS_CUSTOM_SFIX_BIOS)) {
		printf("Load Sfix\n");
		r->bios_sfix.p = gn_unzip_file_malloc(pz, "sfix.sfx", 0x0,
				&r->bios_sfix.size);
		if (r->bios_sfix.p == NULL) {
			printf("Couldn't find sfix.sfx, try sfix.sfix\n");
			r->bios_sfix.p = gn_unzip_file_malloc(pz, "sfix.sfix", 0x0,
					&r->bios_sfix.size);
		}
		if (r->bios_sfix.p == NULL) {
			printf("Trying ng-sfix.rom\n");
			r->bios_sfix.p = gn_unzip_file_malloc(pz, "ng-sfix.rom", 0x0,
					&r->bios_sfix.size);
		}
		if (r->bios_sfix.p == NULL) {
			gn_set_error_msg("Couldn't find sfix.sfx/sfix.sfix/ng-sfix.rom\n");
			return GN_FALSE;
		}
	}
	printf("BIOS: sfix loaded (%u bytes)\n", r->bios_sfix.size);
	/* convert bios fix char */
	convert_all_char(memory.rom.bios_sfix.p, 0x20000, memory.fix_board_usage);

	printf("BIOS: Loading CPU BIOS (system=%d, country=%d)\n", conf.system, conf.country);
	if (!(r->info.flags & HAS_CUSTOM_CPU_BIOS)) {
		if (conf.system == SYS_UNIBIOS) {
			char *unipath;

			/* First check in neogeo.zip */
			r->bios_m68k.p = gn_unzip_file_malloc(pz, "uni-bios.rom", 0x0, &r->bios_m68k.size);
			if (r->bios_m68k.p == NULL) {
				unipath = malloc(strlen(rpath) + strlen("uni-bios.rom") + 2);

				sprintf(unipath, "%s/uni-bios.rom", rpath);
				f = fopen(unipath, "rb");
				if (!f) { /* TODO: Fallback to arcade mode */
					gn_set_error_msg( "Can't open Universal BIOS\n%s\n", unipath);
					free(fpath);
					free(unipath);
					return GN_FALSE;
				}
				r->bios_m68k.p = malloc(0x20000);
				totread = fread(r->bios_m68k.p, 0x20000, 1, f);
				r->bios_m68k.size = 0x20000;
				fclose(f);
				free(unipath);
			}
		} else {
			if (conf.system == SYS_HOME) {
				romfile = "aes-bios.bin";
			} else {
				switch (conf.country) {
					case CTY_JAPAN:
						romfile = "vs-bios.rom";
						break;
					case CTY_USA:
						romfile = "usa_2slt.bin";
						break;
					case CTY_ASIA:
						romfile = "asia-s3.rom";
						break;
					default:
						romfile = "sp-s2.sp1";
						break;
				}
			}
			printf("BIOS: Loading %s\n", romfile);
			r->bios_m68k.p = gn_unzip_file_malloc(pz, romfile, 0x0,
					&r->bios_m68k.size);
			if (r->bios_m68k.p == NULL) {
				printf("BIOS: %s not found, trying neo-geo.rom\n", romfile);
				r->bios_m68k.p = gn_unzip_file_malloc(pz, "neo-geo.rom", 0x0,
						&r->bios_m68k.size);
			}
			if (r->bios_m68k.p == NULL) {
				printf("BIOS: Couldn't load CPU BIOS\n");
				gn_set_error_msg("Couldn't load bios\n%s\n", romfile);
				goto error;
			}
		}
	}

	gn_close_zip(pz);
	free(fpath);
	return GN_TRUE;

error:
	gn_close_zip(pz);
	free(fpath);
	return GN_FALSE;
}

ROM_DEF *dr_check_zip(const char *filename) {

	char *z;
	ROM_DEF *drv;
#ifdef HAVE_BASENAME
	char *game = strdup(basename(filename));
#else
	char *game = strdup(strrchr(filename, '/'));
#endif
	//	printf("Game=%s\n", game);
	if (game == NULL)
		return NULL;
	z = strstr(game, ".zip");
	//	printf("z=%s\n", game);
	if (z == NULL)
	{
		free(game);
		return NULL;
	}
	z[0] = 0;
	drv = res_load_drv(game);
	free(game);
	return drv;
}

int dr_load_roms(GAME_ROMS *r, char *rom_path, char *name) {
	//unzFile *gz,*gzp=NULL,*rdefz;
	PKZIP *gz, *gzp = NULL;
	ROM_DEF *drv;
	int i;
	int romsize;

	memset(r, 0, sizeof (GAME_ROMS));

	gn_loading_info("Loading driver...");
	drv = res_load_drv(name);
	if (!drv) {
		gn_set_error_msg("Can't find rom driver for %s\n", name);

		return GN_FALSE;
	}

	gz = open_rom_zip(rom_path, name);
	if (gz == NULL) {
		gn_set_error_msg("Rom %s/%s.zip not found\n", rom_path, name);
		return GN_FALSE;
	}

	/* Open Parent.
	 For now, only one parent is supported, no recursion
	 */
	gzp = open_rom_zip(rom_path, drv->parent);
	if (gzp == NULL) {
		gn_set_error_msg("Parent %s/%s.zip not found\n", rom_path, name);
		return GN_FALSE;
	}

	//printf("year %d\n",drv->year);
	//return;

	r->info.name = strdup(drv->name);
	r->info.longname = strdup(drv->longname);
	r->info.year = drv->year;
	r->info.flags = 0;
	allocate_region(&r->cpu_m68k, drv->romsize[REGION_MAIN_CPU_CARTRIDGE],
			REGION_MAIN_CPU_CARTRIDGE);
	if (drv->romsize[REGION_AUDIO_CPU_CARTRIDGE] == 0
			&& drv->romsize[REGION_AUDIO_CPU_ENCRYPTED] != 0) {
		allocate_region(&r->cpu_z80c, 0x80000, REGION_AUDIO_CPU_ENCRYPTED);
		allocate_region(&r->cpu_z80, 0x90000, REGION_AUDIO_CPU_CARTRIDGE);
	} else {
		allocate_region(&r->cpu_z80, drv->romsize[REGION_AUDIO_CPU_CARTRIDGE],
				REGION_AUDIO_CPU_CARTRIDGE);
	}
	allocate_region(&r->game_sfix, drv->romsize[REGION_FIXED_LAYER_CARTRIDGE],
			REGION_FIXED_LAYER_CARTRIDGE);
	allocate_region(&r->gfix_usage, r->game_sfix.size >> 5,
			REGION_GAME_FIX_USAGE);

	/* Determine if tiles and ADPCM need streaming (too large for PSRAM) */
	Uint32 tiles_size = drv->romsize[REGION_SPRITES];
	Uint32 adpcma_size = conf.sound ? drv->romsize[REGION_AUDIO_DATA_1] : 0;
	Uint32 adpcmb_size = conf.sound ? drv->romsize[REGION_AUDIO_DATA_2] : 0;
	Uint32 free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

	/* Need enough for: tiles + adpcma + adpcmb + spr_usage + headroom */
	Uint32 total_rom_need = tiles_size + adpcma_size + adpcmb_size + PSRAM_HEADROOM;
	streaming_tiles = 0;
	streaming_adpcma = 0;
	streaming_adpcmb = 0;

	char ctile_path[128], vroma_path[128], vromb_path[128];
	get_cache_path(name, "ctile", ctile_path, sizeof(ctile_path));
	get_cache_path(name, "vroma", vroma_path, sizeof(vroma_path));
	get_cache_path(name, "vromb", vromb_path, sizeof(vromb_path));

	/* Fall back to parent's cache files for clones that share sprites/audio */
	if (drv->parent[0] != '\0' && strcmp(drv->parent, "neogeo") != 0) {
		char parent_path[128];
		if (!cache_file_exists(ctile_path)) {
			get_cache_path(drv->parent, "ctile", parent_path, sizeof(parent_path));
			if (cache_file_exists(parent_path)) {
				printf("Using parent ctile: %s\n", parent_path);
				strncpy(ctile_path, parent_path, sizeof(ctile_path));
			}
		}
		if (!cache_file_exists(vroma_path)) {
			get_cache_path(drv->parent, "vroma", parent_path, sizeof(parent_path));
			if (cache_file_exists(parent_path)) {
				printf("Using parent vroma: %s\n", parent_path);
				strncpy(vroma_path, parent_path, sizeof(vroma_path));
			}
		}
		if (!cache_file_exists(vromb_path)) {
			get_cache_path(drv->parent, "vromb", parent_path, sizeof(parent_path));
			if (cache_file_exists(parent_path)) {
				printf("Using parent vromb: %s\n", parent_path);
				strncpy(vromb_path, parent_path, sizeof(vromb_path));
			}
		}
	}

	gn_loading_info("Allocating memory...");
	printf("ROM sizes: tiles=%uMB adpcma=%uMB adpcmb=%uMB free_psram=%uMB\n",
		   tiles_size / (1024*1024), adpcma_size / (1024*1024),
		   adpcmb_size / (1024*1024), free_psram / (1024*1024));

	if (total_rom_need > free_psram) {
		printf("ROMs too large for PSRAM (%u MB needed, %u MB free) — using streaming\n",
			   total_rom_need / (1024*1024), free_psram / (1024*1024));

		/* Decide what to stream based on size.
		 * Strategy: stream the largest regions first until total fits */
		Uint32 budget = free_psram - PSRAM_HEADROOM;

		/* Always try to stream tiles (usually the largest) */
		if (tiles_size > budget / 2 || tiles_size + adpcma_size + adpcmb_size > budget) {
			streaming_tiles = 1;
			/* Don't allocate tiles in PSRAM — will use cache */
			r->tiles.p = NULL;
			r->tiles.size = tiles_size;
		} else {
			allocate_region(&r->tiles, tiles_size, REGION_SPRITES);
		}

		/* Recalculate remaining budget */
		Uint32 remaining = budget - (streaming_tiles ? 0 : tiles_size);
		remaining -= SPRITE_CACHE_BUDGET; /* reserve for sprite cache if streaming */

		if (conf.sound && adpcma_size > 0) {
			if (adpcma_size > remaining) {
				streaming_adpcma = 1;
				r->adpcma.p = NULL;
				r->adpcma.size = adpcma_size;
				remaining -= ADPCM_CACHE_BUDGET;
			} else {
				allocate_region(&r->adpcma, adpcma_size, REGION_AUDIO_DATA_1);
				remaining -= adpcma_size;
			}

			if (adpcmb_size > 0 && adpcmb_size > remaining) {
				streaming_adpcmb = 1;
				r->adpcmb.p = NULL;
				r->adpcmb.size = adpcmb_size;
			} else if (adpcmb_size > 0) {
				allocate_region(&r->adpcmb, adpcmb_size, REGION_AUDIO_DATA_2);
			}
		}
	} else {
		/* Everything fits in PSRAM — normal path */
		allocate_region(&r->tiles, tiles_size, REGION_SPRITES);
		if (conf.sound) {
			allocate_region(&r->adpcma, adpcma_size, REGION_AUDIO_DATA_1);
			allocate_region(&r->adpcmb, adpcmb_size, REGION_AUDIO_DATA_2);
		} else {
			r->adpcma.p = NULL; r->adpcma.size = 0;
			r->adpcmb.p = NULL; r->adpcmb.size = 0;
		}
	}

	if (!conf.sound) {
		r->adpcma.p = NULL; r->adpcma.size = 0;
		r->adpcmb.p = NULL; r->adpcmb.size = 0;
		streaming_adpcma = 0;
		streaming_adpcmb = 0;
	}

	printf("Streaming: tiles=%d adpcma=%d adpcmb=%d\n",
		   streaming_tiles, streaming_adpcma, streaming_adpcmb);

	/* Allocate bios if necessary */
	DEBUG_LOG("BIOS SIZE %08x %08x %08x\n", drv->romsize[REGION_MAIN_CPU_BIOS],
			drv->romsize[REGION_AUDIO_CPU_BIOS],
			drv->romsize[REGION_FIXED_LAYER_BIOS]);
	if (drv->romsize[REGION_MAIN_CPU_BIOS] != 0) {
		r->info.flags |= HAS_CUSTOM_CPU_BIOS;
		allocate_region(&r->bios_m68k, drv->romsize[REGION_MAIN_CPU_BIOS],
				REGION_MAIN_CPU_BIOS);
	}
	if (drv->romsize[REGION_AUDIO_CPU_BIOS] != 0) {
		r->info.flags |= HAS_CUSTOM_AUDIO_BIOS;
		allocate_region(&r->bios_audio, drv->romsize[REGION_AUDIO_CPU_BIOS],
				REGION_AUDIO_CPU_BIOS);
	}
	if (drv->romsize[REGION_FIXED_LAYER_BIOS] != 0) {
		r->info.flags |= HAS_CUSTOM_SFIX_BIOS;
		allocate_region(&r->bios_sfix, drv->romsize[REGION_FIXED_LAYER_BIOS],
				REGION_FIXED_LAYER_BIOS);
	}

	iloadbuf = malloc(LOAD_BUF_SIZE);

	/*
	 * Open cache files for streaming regions.
	 * If cache files already exist, we skip extracting those regions from ZIP.
	 * If not, we create them during the ROM loading loop.
	 */
	int skip_tile_extract = 0;
	int skip_adpcma_extract = 0;
	int skip_adpcmb_extract = 0;

	if (streaming_tiles) {
		char cusage_chk[128];
		get_cache_path(name, "cusage", cusage_chk, sizeof(cusage_chk));
		/* Fall back to parent cusage if game-specific one missing */
		if (!cusage_file_valid(cusage_chk) && drv->parent[0] != '\0' && strcmp(drv->parent, "neogeo") != 0) {
			char parent_cusage[128];
			get_cache_path(drv->parent, "cusage", parent_cusage, sizeof(parent_cusage));
			if (cusage_file_valid(parent_cusage)) {
				printf("Using parent cusage: %s\n", parent_cusage);
				strncpy(cusage_chk, parent_cusage, sizeof(cusage_chk));
			}
		}
		if (cache_file_exists(ctile_path) && cusage_file_valid(cusage_chk)) {
			printf("Sprite cache complete: %s\n", ctile_path);
			skip_tile_extract = 1;
		} else {
			/* Remove incomplete cache files from previous interrupted run */
			remove(ctile_path);
			remove(cusage_chk);
			printf("Creating sprite cache: %s (%u MB)\n", ctile_path, tiles_size / (1024*1024));
			stream_tile_file = fopen(ctile_path, "w+b");
			if (stream_tile_file) {
				/* Pre-fill file to full size (zeroed) */
				Uint8 zero[4096];
				memset(zero, 0, sizeof(zero));
				for (Uint32 off = 0; off < tiles_size; off += sizeof(zero)) {
					Uint32 chunk = sizeof(zero);
					if (off + chunk > tiles_size) chunk = tiles_size - off;
					fwrite(zero, 1, chunk, stream_tile_file);
				}
				fflush(stream_tile_file);
				fsync(fileno(stream_tile_file));
				fclose(stream_tile_file);
				/* Reopen in r+b so FATFS can properly read back written data */
				stream_tile_file = fopen(ctile_path, "r+b");
				if (stream_tile_file)
					setvbuf(stream_tile_file, NULL, _IONBF, 0);  /* Unbuffered — critical for read-modify-write on FATFS */
				printf("Pre-fill done (%u MB)\n", tiles_size / (1024*1024));
			}
		}
	}
	if (streaming_adpcma) {
		if (cache_file_exists(vroma_path)) {
			printf("ADPCM-A cache found: %s\n", vroma_path);
			skip_adpcma_extract = 1;
		} else {
			remove(vroma_path);
			printf("Creating ADPCM-A cache: %s (%u MB)\n", vroma_path, adpcma_size / (1024*1024));
			stream_adpcma_file = fopen(vroma_path, "w+b");
		}
	}
	if (streaming_adpcmb && adpcmb_size > 0) {
		if (cache_file_exists(vromb_path)) {
			printf("ADPCM-B cache found: %s\n", vromb_path);
			skip_adpcmb_extract = 1;
		} else {
			remove(vromb_path);
			printf("Creating ADPCM-B cache: %s (%u MB)\n", vromb_path, adpcmb_size / (1024*1024));
			stream_adpcmb_file = fopen(vromb_path, "w+b");
		}
	}

	/*
	 * Fast-path: if .vroma/.vromb exists and ADPCM is in PSRAM (not streaming),
	 * load directly from the flat file instead of decompressing from ZIP.
	 * This replaces ~30s of stb_zlib decompression with a ~2s flat fread.
	 */
	if (!streaming_adpcma && r->adpcma.p && adpcma_size > 0 && cache_file_exists(vroma_path)) {
		gn_loading_info("Loading ADPCM audio...");
		int64_t t0 = esp_timer_get_time();
		FILE *vf = fopen(vroma_path, "rb");
		if (vf) {
			size_t rd = fread(r->adpcma.p, 1, adpcma_size, vf);
			fclose(vf);
			int64_t elapsed = esp_timer_get_time() - t0;
			printf("ADPCM-A fast-load from %s: %u KB in %lld ms (%.1f MB/s)\n",
				   vroma_path, (unsigned)(rd / 1024), elapsed / 1000,
				   elapsed > 0 ? (double)rd / elapsed : 0.0);
			skip_adpcma_extract = 1;
		}
	}
	if (!streaming_adpcmb && r->adpcmb.p && adpcmb_size > 0 && cache_file_exists(vromb_path)) {
		int64_t t0 = esp_timer_get_time();
		FILE *vf = fopen(vromb_path, "rb");
		if (vf) {
			size_t rd = fread(r->adpcmb.p, 1, adpcmb_size, vf);
			fclose(vf);
			int64_t elapsed = esp_timer_get_time() - t0;
			printf("ADPCM-B fast-load from %s: %u KB in %lld ms (%.1f MB/s)\n",
				   vromb_path, (unsigned)(rd / 1024), elapsed / 1000,
				   elapsed > 0 ? (double)rd / elapsed : 0.0);
			skip_adpcmb_extract = 1;
		}
	}

	/* Now, load the roms */
	gn_loading_info("Loading ROMs...");
	read_counter = 0;
	romsize = 0;
	for (i = 0; i < drv->nb_romfile; i++)
		romsize += drv->rom[i].size;
	gn_init_pbar("Loading...", romsize);
	int64_t load_start_us = esp_timer_get_time();
	int64_t total_skip_us = 0, total_load_us = 0;
	for (i = 0; i < drv->nb_romfile; i++) {
		int region = drv->rom[i].region;
		int64_t file_start_us = esp_timer_get_time();

		/* Skip extraction for regions that have existing cache files */
		if (region == REGION_SPRITES && skip_tile_extract) {
			read_counter += drv->rom[i].size;
			gn_update_pbar(read_counter);
			total_skip_us += esp_timer_get_time() - file_start_us;
			printf("SKIP  %-20s region=%d size=%uKB\n", drv->rom[i].filename, region, drv->rom[i].size/1024);
			continue;
		}
		if (region == REGION_AUDIO_DATA_1 && skip_adpcma_extract) {
			read_counter += drv->rom[i].size;
			gn_update_pbar(read_counter);
			total_skip_us += esp_timer_get_time() - file_start_us;
			printf("SKIP  %-20s region=%d size=%uKB\n", drv->rom[i].filename, region, drv->rom[i].size/1024);
			continue;
		}
		if (region == REGION_AUDIO_DATA_2 && skip_adpcmb_extract) {
			read_counter += drv->rom[i].size;
			gn_update_pbar(read_counter);
			total_skip_us += esp_timer_get_time() - file_start_us;
			printf("SKIP  %-20s region=%d size=%uKB\n", drv->rom[i].filename, region, drv->rom[i].size/1024);
			continue;
		}

		if (load_region(gz, r, drv->rom[i].region, drv->rom[i].src,
				drv->rom[i].dest, drv->rom[i].size, drv->rom[i].crc,
				drv->rom[i].filename) != 0) {
			/* File not found in the roms, try the parent */
			if (gzp) {
				int region = drv->rom[i].region;
				int pi;
				pi = load_region(gzp, r, drv->rom[i].region, drv->rom[i].src,
						drv->rom[i].dest, drv->rom[i].size, drv->rom[i].crc,
						drv->rom[i].filename);
				DEBUG_LOG("From parent %d\n", pi);
				if (pi && (region != 5 && region != 0 && region != 7)) {
					gn_set_error_msg("ERROR: File %s not found\n",
							drv->rom[i].filename);
					goto error1;
				}
			} else {
				int region = drv->rom[i].region;
				if (region != 5 && region != 0 && region != 7) {
					gn_set_error_msg("ERROR: File %s not found\n",
							drv->rom[i].filename);
					goto error1;
				}

			}
		}
		int64_t file_elapsed_us = esp_timer_get_time() - file_start_us;
		total_load_us += file_elapsed_us;
		printf("LOAD  %-20s region=%d size=%6uKB  %4lldms  (%.1f MB/s)\n",
			   drv->rom[i].filename, region, drv->rom[i].size/1024,
			   file_elapsed_us / 1000,
			   file_elapsed_us > 0 ? (double)drv->rom[i].size / file_elapsed_us : 0.0);

	}
	int64_t load_elapsed_us = esp_timer_get_time() - load_start_us;
	printf("=== LOAD SUMMARY: total=%lldms  load=%lldms  skip=%lldms  files=%d ===\n",
		   load_elapsed_us / 1000, total_load_us / 1000, total_skip_us / 1000, drv->nb_romfile);
	gn_terminate_pbar();
	/* Close/clean up */
	gn_close_zip(gz);
	if (gzp) gn_close_zip(gzp);
	free(drv);

	/* Handle streaming tile cache creation */
	if (streaming_tiles) {
		gn_loading_info("Setting up sprite cache...");
		char cusage_path[128];
		get_cache_path(name, "cusage", cusage_path, sizeof(cusage_path));

		if (stream_tile_file) {
			/* Scan for first non-zero data in the raw tile file */
			{
				Uint8 dbg[4096];
				int found_nonzero = 0;
				Uint32 total_nonzero = 0;
				fseek(stream_tile_file, 0, SEEK_SET);
				/* Check first 1MB for non-zero tiles */
				for (Uint32 off = 0; off < 1024*1024 && !found_nonzero; off += 4096) {
					fread(dbg, 1, 4096, stream_tile_file);
					for (int b = 0; b < 4096; b++) {
						if (dbg[b] != 0) total_nonzero++;
					}
					if (total_nonzero > 0 && !found_nonzero) {
						/* Find and dump the first non-zero 128-byte tile in this block */
						for (int t = 0; t < 4096; t += 128) {
							int has_data = 0;
							for (int b = 0; b < 128; b++) { if (dbg[t+b]) has_data = 1; }
							if (has_data) {
								Uint32 tileno = (off + t) / 128;
								printf("RAW first non-zero tile %u (offset 0x%X):\n", tileno, off + t);
								for (int d = 0; d < 128; d += 16) {
									printf("  %04X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
										d, dbg[t+d],dbg[t+d+1],dbg[t+d+2],dbg[t+d+3],dbg[t+d+4],dbg[t+d+5],dbg[t+d+6],dbg[t+d+7],
										dbg[t+d+8],dbg[t+d+9],dbg[t+d+10],dbg[t+d+11],dbg[t+d+12],dbg[t+d+13],dbg[t+d+14],dbg[t+d+15]);
								}
								found_nonzero = 1;
								break;
							}
						}
					}
				}
				printf("RAW file scan: %u non-zero bytes in first 1MB\n", total_nonzero);
			}
			/* We just extracted tiles to the cache file — convert them in-place */
			printf("Converting tiles in cache file...\n");
			convert_tiles_in_file(stream_tile_file, r->tiles.size, r);
			fflush(stream_tile_file);
			fsync(fileno(stream_tile_file));
			fclose(stream_tile_file);
			stream_tile_file = NULL;

			/* Save spr_usage to companion file (serves as completion marker) */
			FILE *uf = fopen(cusage_path, "wb");
			if (uf) {
				Uint32 magic = CUSAGE_MAGIC;
				fwrite(&magic, sizeof(magic), 1, uf);
				fwrite(r->spr_usage.p, 1, r->spr_usage.size, uf);
				fflush(uf);
				fsync(fileno(uf));
				fclose(uf);
				printf("Saved spr_usage (%u bytes)\n", r->spr_usage.size);
			}
		} else {
			/* Load spr_usage from companion file */
			Uint32 usage_size = (tiles_size >> 11) * sizeof(Uint32);
			allocate_region(&r->spr_usage, usage_size, REGION_SPR_USAGE);
			FILE *uf = fopen(cusage_path, "rb");
			if (uf) {
				Uint32 magic = 0;
				fread(&magic, sizeof(magic), 1, uf);  /* skip magic header */
				fread(r->spr_usage.p, 1, usage_size, uf);
				fclose(uf);
				printf("Loaded spr_usage from %s\n", cusage_path);
			} else {
				/* No valid cusage — should not happen (checked earlier) */
				printf("ERROR: cusage file missing, deleting cache\n");
				remove(ctile_path);
				remove(cusage_path);
				memset(r->spr_usage.p, 0, usage_size);
			}
		}
		/* Set up streaming from the .ctile file */
		if (setup_sprite_streaming(ctile_path, tiles_size) != GN_TRUE) {
			printf("ERROR: Failed to set up sprite streaming\n");
			return GN_FALSE;
		}
	}

	/* Handle streaming ADPCM cache setup */
	if (streaming_adpcma) {
		if (stream_adpcma_file) {
			fflush(stream_adpcma_file);
			fsync(fileno(stream_adpcma_file));
			fclose(stream_adpcma_file);
			stream_adpcma_file = NULL;
		}
		if (setup_adpcm_streaming(&adpcm_cacheA, vroma_path, adpcma_size, &r->adpcma) != GN_TRUE) {
			printf("ERROR: Failed to set up ADPCM-A streaming\n");
			return GN_FALSE;
		}
	}
	if (streaming_adpcmb) {
		if (stream_adpcmb_file) {
			fflush(stream_adpcmb_file);
			fsync(fileno(stream_adpcmb_file));
			fclose(stream_adpcmb_file);
			stream_adpcmb_file = NULL;
		}
		if (setup_adpcm_streaming(&adpcm_cacheB, vromb_path, adpcmb_size, &r->adpcmb) != GN_TRUE) {
			printf("ERROR: Failed to set up ADPCM-B streaming\n");
			return GN_FALSE;
		}
	}

	if (r->adpcmb.size == 0) {
		r->adpcmb.p = r->adpcma.p;
		r->adpcmb.size = r->adpcma.size;
		/* If ADPCM-A is streaming, B should alias the same cache */
		if (streaming_adpcma && !streaming_adpcmb) {
			adpcm_cacheB = adpcm_cacheA;
			adpcm_cacheB.file = NULL; /* don't double-close */
			streaming_adpcmb = 1;
		}
	}

	memory.fix_game_usage = r->gfix_usage.p;
	memory.nb_of_tiles = r->tiles.size >> 7;

	free(iloadbuf);
	iloadbuf = NULL;

	/* Init rom and bios */
	gn_loading_info("Decrypting ROMs...");
	init_roms(r);

	/* Convert tiles (only if loaded in PSRAM, not streaming) */
	if (!streaming_tiles) {
		gn_loading_info("Converting tiles...");
		convert_all_tile(r);
	}

	gn_loading_info("Loading BIOS...");
	return dr_load_bios(r);

error1:
	gn_terminate_pbar();
	//unzClose(gz);
	//if (gzp) unzClose(gzp);
	gn_close_zip(gz);
	if (gzp)
		gn_close_zip(gzp);

	free(drv);
	return GN_FALSE;
}

int dr_load_game(char *name) {
	//GAME_ROMS rom;
	char *rpath = CF_STR(cf_get_item_by_name("rompath"));
	int rc;
	printf("Loading %s/%s.zip\n", rpath, name);
	memory.bksw_handler = 0;
	memory.bksw_unscramble = NULL;
	memory.bksw_offset = NULL;

	rc = dr_load_roms(&memory.rom, rpath, name);
	if (rc == GN_FALSE) {
		return GN_FALSE;
	}
	conf.game = memory.rom.info.name;
	/* TODO *///neogeo_fix_bank_type =0;
	/* TODO */
	//	set_bankswitchers(0);

	memcpy(memory.game_vector, memory.rom.cpu_m68k.p, 0x80);
	memcpy(memory.rom.cpu_m68k.p, memory.rom.bios_m68k.p, 0x80);

	/* For CMC42/CMC50 games in streaming mode, neogeo_sfix_decrypt() is skipped
	 * because tiles.p is NULL. Load pre-extracted .sfix file if available. */
	if (memory.rom.tiles.p == NULL) {
		char sfix_path[128];
		get_cache_path(name, "sfix", sfix_path, sizeof(sfix_path));
		if (cache_file_exists(sfix_path)) {
			FILE *sf = fopen(sfix_path, "rb");
			if (sf) {
				fseek(sf, 0, SEEK_END);
				long sfix_size = ftell(sf);
				fseek(sf, 0, SEEK_SET);
				if (sfix_size > 0 && (Uint32)sfix_size <= memory.rom.game_sfix.size) {
					fread(memory.rom.game_sfix.p, 1, sfix_size, sf);
					printf("Loaded SFIX cache: %s (%ld bytes)\n", sfix_path, sfix_size);
				} else if (sfix_size > (long)memory.rom.game_sfix.size) {
					/* .sfix larger than allocated region — reallocate */
					Uint8 *new_sfix = heap_caps_malloc(sfix_size, MALLOC_CAP_SPIRAM);
					if (new_sfix) {
						fread(new_sfix, 1, sfix_size, sf);
						heap_caps_free(memory.rom.game_sfix.p);
						memory.rom.game_sfix.p = new_sfix;
						memory.rom.game_sfix.size = sfix_size;
						/* Reallocate usage array for new size */
						Uint32 new_usage_size = (sfix_size >> 5) * sizeof(Uint32);
						Uint8 *new_usage = heap_caps_malloc(new_usage_size, MALLOC_CAP_SPIRAM);
						if (new_usage) {
							heap_caps_free(memory.rom.gfix_usage.p);
							memory.rom.gfix_usage.p = new_usage;
							memory.rom.gfix_usage.size = new_usage_size;
							memory.fix_game_usage = new_usage;
						}
						printf("Loaded SFIX cache (realloc): %s (%ld bytes)\n", sfix_path, sfix_size);
					}
				}
				fclose(sf);
			}
		}
	}

	convert_all_char(memory.rom.game_sfix.p, memory.rom.game_sfix.size,
			memory.fix_game_usage);

	/* TODO: Move this somewhere else. */
	gn_loading_info("Initializing video...");
	init_video();

	return GN_TRUE;

}

#if defined(HAVE_LIBZ)//&& defined (HAVE_MMAP)

static int dump_region(FILE *gno, const ROM_REGION *rom, Uint8 id, Uint8 type,
		Uint32 block_size) {
	if (rom->p == NULL)
		return GN_FALSE;
	fwrite(&rom->size, sizeof (Uint32), 1, gno);
	fwrite(&id, sizeof (Uint8), 1, gno);
	fwrite(&type, sizeof (Uint8), 1, gno);
	if (type == 0) {
		printf("Dump %d %08x\n", id, rom->size);
		fwrite(rom->p, rom->size, 1, gno);
	} else {
		Uint32 nb_block = rom->size / block_size;
		Uint32 *block_offset;
		Uint32 cur_offset = 0;
		long offset_pos;
		Uint32 i;
		const Uint8 *inbuf = rom->p;
		Uint8 *outbuf;
		uLongf outbuf_len;
		uLongf outlen;
		Uint32 outlen32;
		Uint32 cmpsize = 0;
		int rc;
		printf("nb_block=%d\n", nb_block);
		fwrite(&block_size, sizeof (Uint32), 1, gno);
		if ((rom->size & (block_size - 1)) != 0) {
			printf("Waring: Block_size and totsize not compatible %x %x\n",
					rom->size, block_size);
		}
		block_offset = malloc(nb_block * sizeof (Uint32));
		/* Zlib compress output buffer need to be at least the size
		 of inbuf + 0.1% + 12 byte */
		outbuf_len = compressBound(block_size);
		outbuf = malloc(outbuf_len);
		offset_pos = ftell(gno);
		fseek(gno, nb_block * 4 + 4, SEEK_CUR); /* Skip all the offset table + the total compressed size */

		for (i = 0; i < nb_block; i++) {
			cur_offset = ftell(gno);
			block_offset[i] = cur_offset;
			outlen = outbuf_len;
			rc = compress(outbuf, &outlen, inbuf, block_size);
			printf("%d %ld\n", rc, outlen);
			//cur_offset += outlen;
			cmpsize += outlen;
			printf("cmpsize=%d %ld\n", cmpsize, sizeof (uLongf));
			inbuf += block_size;
			outlen32 = (Uint32) outlen;
			fwrite(&outlen32, sizeof (Uint32), 1, gno);
			printf("bank %d outlen=%d offset=%d\n", i, outlen32, cur_offset);
			fwrite(outbuf, outlen, 1, gno);
		}
		free(outbuf);
		/* Now, write the offset table */
		fseek(gno, offset_pos, SEEK_SET);
		fwrite(block_offset, sizeof (Uint32), nb_block, gno);
		free(block_offset);
		fwrite(&cmpsize, sizeof (Uint32), 1, gno);
		printf("cmpsize=%d\n", cmpsize);
		fseek(gno, 0, SEEK_END);
		offset_pos = ftell(gno);
		printf("currpos=%li\n", offset_pos);
	}
	return GN_TRUE;
}

int dr_save_gno(GAME_ROMS *r, char *filename) {
	FILE *gno;
	char *fid = "gnodmpv1";
	char fname[9];
	Uint8 nb_sec = 0;
	int i;

	gno = fopen(filename, "wb");
	if (!gno)
		return GN_FALSE;

	/* restore game vector */
	memcpy(memory.rom.cpu_m68k.p, memory.game_vector, 0x80);
	for (i = 0; i < 0x80; i++)
		printf("%02x ", memory.rom.cpu_m68k.p[i]);
	printf("\n");

	if (r->cpu_m68k.p)
		nb_sec++;
	if (r->cpu_z80.p)
		nb_sec++;
	if (r->adpcma.p)
		nb_sec++;
	if (r->adpcmb.p && (r->adpcmb.p != r->adpcma.p))
		nb_sec++;
	if (r->game_sfix.p)
		nb_sec++;
	if (r->tiles.p)
		nb_sec += 2; /* Sprite + Sprite usage */
	if (r->gfix_usage.p)
		nb_sec++;
	/* Do we need Custom Bios? */
	if ((r->info.flags & HAS_CUSTOM_CPU_BIOS)) {
		nb_sec++;
	}
	if ((r->info.flags & HAS_CUSTOM_SFIX_BIOS)) {
		nb_sec++;
	}


	/* Header information */
	fwrite(fid, 8, 1, gno);
	snprintf(fname, 9, "%-8s", r->info.name);
	fwrite(fname, 8, 1, gno);
	fwrite(&r->info.flags, sizeof (Uint32), 1, gno);
	fwrite(&nb_sec, sizeof (Uint8), 1, gno);

	/* Now each section */
	dump_region(gno, &r->cpu_m68k, REGION_MAIN_CPU_CARTRIDGE, 0, 0);
	dump_region(gno, &r->cpu_z80, REGION_AUDIO_CPU_CARTRIDGE, 0, 0);
	dump_region(gno, &r->adpcma, REGION_AUDIO_DATA_1, 0, 0);
	if (r->adpcma.p != r->adpcmb.p)
		dump_region(gno, &r->adpcmb, REGION_AUDIO_DATA_2, 0, 0);
	dump_region(gno, &r->game_sfix, REGION_FIXED_LAYER_CARTRIDGE, 0, 0);
	dump_region(gno, &r->spr_usage, REGION_SPR_USAGE, 0, 0);
	dump_region(gno, &r->gfix_usage, REGION_GAME_FIX_USAGE, 0, 0);
	if ((r->info.flags & HAS_CUSTOM_CPU_BIOS)) {
		dump_region(gno, &r->bios_m68k, REGION_MAIN_CPU_BIOS, 0, 0);
	}
	if ((r->info.flags & HAS_CUSTOM_SFIX_BIOS)) {
		dump_region(gno, &r->bios_sfix, REGION_FIXED_LAYER_BIOS, 0, 0);
	}
	/* TODO, there is a bug in the loading routine, only one compressed (type 1)
	 * region can be present at the end of the file */
	dump_region(gno, &r->tiles, REGION_SPRITES, 1, 4096);


	fclose(gno);
	return GN_TRUE;
}

int read_region(FILE *gno, GAME_ROMS *roms) {
	Uint32 size;
	Uint8 lid, type;
	ROM_REGION *r = NULL;
	size_t totread = 0;
	Uint32 cache_size[] = {64, 32, 24, 16, 8, 6, 4, 2, 1, 0};
	int i = 0;

	/* Read region header */
	totread = fread(&size, sizeof (Uint32), 1, gno);
	totread += fread(&lid, sizeof (Uint8), 1, gno);
	totread += fread(&type, sizeof (Uint8), 1, gno);

	switch (lid) {
		case REGION_MAIN_CPU_CARTRIDGE:
			r = &roms->cpu_m68k;
			break;
		case REGION_AUDIO_CPU_CARTRIDGE:
			r = &roms->cpu_z80;
			break;
		case REGION_AUDIO_DATA_1:
			r = &roms->adpcma;
			break;
		case REGION_AUDIO_DATA_2:
			r = &roms->adpcmb;
			break;
		case REGION_FIXED_LAYER_CARTRIDGE:
			r = &roms->game_sfix;
			break;
		case REGION_SPRITES:
			r = &roms->tiles;
			break;
		case REGION_SPR_USAGE:
			r = &roms->spr_usage;
			break;
		case REGION_GAME_FIX_USAGE:
			r = &roms->gfix_usage;
			break;
		case REGION_FIXED_LAYER_BIOS:
			r = &roms->bios_sfix;
			break;
		case REGION_MAIN_CPU_BIOS:break;
			r = &roms->bios_m68k;
			break;
		default:
			return GN_FALSE;
	}

	printf("Read region %d %08X type %d\n", lid, size, type);
	if (type == 0) {
		/* TODO: Support ADPCM streaming for platform with less that 64MB of Mem */
		allocate_region(r, size, lid);
		printf("Load %d %08x\n", lid, r->size);
		totread += fread(r->p, r->size, 1, gno);
	} else {
		Uint32 nb_block, block_size;
		Uint32 cmp_size;
		totread += fread(&block_size, sizeof (Uint32), 1, gno);
		nb_block = size / block_size;

		printf("Region size=%08X\n", size);
		r->size = size;


		memory.vid.spr_cache.offset = malloc(sizeof (Uint32) * nb_block);
		totread += fread(memory.vid.spr_cache.offset, sizeof (Uint32), nb_block, gno);
		memory.vid.spr_cache.gno = gno;

		totread += fread(&cmp_size, sizeof (Uint32), 1, gno);

		fseek(gno, cmp_size, SEEK_CUR);

		/* TODO: Find the best cache size dynamically! */
		for (i = 0; cache_size[i] != 0; i++) {
			if (init_sprite_cache(cache_size[i]*1024 * 1024, block_size) == GN_TRUE) {
				printf("Cache size=%dMB\n", cache_size[i]);
				break;
			}
		}
	}
	return GN_TRUE;
}

int dr_open_gno(char *filename) {
	FILE *gno;
	char fid[9]; // = "gnodmpv1";
	char name[9] = {0,};
	GAME_ROMS *r = &memory.rom;
	Uint8 nb_sec;
	int i;
	char *a;
	size_t totread = 0;

	memory.bksw_handler = 0;
	memory.bksw_unscramble = NULL;
	memory.bksw_offset = NULL;

	need_decrypt = 0;

	gno = fopen(filename, "rb");
	if (!gno)
		return GN_FALSE;

	totread += fread(fid, 8, 1, gno);
	if (strncmp(fid, "gnodmpv1", 8) != 0) {
		fclose(gno);
		printf("Invalid GNO file\n");
		return GN_FALSE;
	}
	totread += fread(name, 8, 1, gno);
	a = strchr(name, ' ');
	if (a) a[0] = 0;
	r->info.name = strdup(name);

	totread += fread(&r->info.flags, sizeof (Uint32), 1, gno);
	totread += fread(&nb_sec, sizeof (Uint8), 1, gno);

	gn_init_pbar("Loading", nb_sec);
	for (i = 0; i < nb_sec; i++) {
		gn_update_pbar(i);
		read_region(gno, r);
	}
	gn_terminate_pbar();

	if (r->adpcmb.p == NULL) {
		r->adpcmb.p = r->adpcma.p;
		r->adpcmb.size = r->adpcma.size;
	}
	//fclose(gno);

	memory.fix_game_usage = r->gfix_usage.p;
	/*	memory.pen_usage = malloc((r->tiles.size >> 11) * sizeof(Uint32));
	CHECK_ALLOC(memory.pen_usage);
	memset(memory.pen_usage, 0, (r->tiles.size >> 11) * sizeof(Uint32));*/
	memory.nb_of_tiles = r->tiles.size >> 7;

	/* Init rom and bios */
	init_roms(r);
	//convert_all_tile(r);
	if (dr_load_bios(r)==GN_FALSE)
		return GN_FALSE;

	conf.game = memory.rom.info.name;

	memcpy(memory.game_vector, memory.rom.cpu_m68k.p, 0x80);
	memcpy(memory.rom.cpu_m68k.p, memory.rom.bios_m68k.p, 0x80);
	init_video();

	return GN_TRUE;
}

char *dr_gno_romname(char *filename) {
	FILE *gno;
	char fid[9]; // = "gnodmpv1";
	char name[9] = {0,};
	char *space;
	size_t totread = 0;

	gno = fopen(filename, "rb");
	if (!gno)
		return NULL;

	totread += fread(fid, 8, 1, gno);
	if (strncmp(fid, "gnodmpv1", 8) != 0) {
		printf("Invalid GNO file\n");
		return NULL;
	}

	totread += fread(name, 8, 1, gno);

	space=strchr(name,' ');
	if (space!=NULL) space[0]=0;

	fclose(gno);
	return strdup(name);
}


#else

static int dump_region(FILE *gno, ROM_REGION *rom, Uint8 id, Uint8 type, Uint32 block_size) {
	return GN_TRUE;
}

int dr_save_gno(GAME_ROMS *r, char *filename) {
	return GN_TRUE;
}
#endif

void dr_free_roms(GAME_ROMS *r) {
	free_region(&r->cpu_m68k);
	free_region(&r->cpu_z80c);

	if (!memory.vid.spr_cache.data) {
		printf("Free tiles\n");
		free_region(&r->tiles);
	} else {
		if (memory.vid.spr_cache.gno) {
			fclose(memory.vid.spr_cache.gno);
			memory.vid.spr_cache.gno = NULL;
		}
		free_sprite_cache();
		if (memory.vid.spr_cache.offset) {
			free(memory.vid.spr_cache.offset);
			memory.vid.spr_cache.offset = NULL;
		}
	}
	free_region(&r->game_sfix);

#ifndef ENABLE_940T
	free_region(&r->cpu_z80);
	free_region(&r->bios_audio);

	/* Free ADPCM caches if streaming */
	if (adpcm_cacheA.active) {
		adpcm_cache_free(&adpcm_cacheA);
	}
	if (adpcm_cacheB.active) {
		adpcm_cache_free(&adpcm_cacheB);
	}

	if (r->adpcmb.p && r->adpcmb.p != r->adpcma.p)
		free_region(&r->adpcmb);
	else {
		r->adpcmb.p = NULL;
		r->adpcmb.size = 0;
	}

	if (r->adpcma.p)
		free_region(&r->adpcma);
#endif

	free_region(&r->bios_m68k);
	free_region(&r->bios_sfix);

	free(memory.ng_lo);
	free(memory.fix_game_usage);
	free_region(&r->spr_usage);

	free(r->info.name);
	free(r->info.longname);

	streaming_tiles = 0;
	streaming_adpcma = 0;
	streaming_adpcmb = 0;

	conf.game = NULL;
}


void open_nvram(char *name) {
    char *filename;
    size_t totread = 0;
#ifdef EMBEDDED_FS
    const char *gngeo_dir = ROOTPATH"save/";
#elif defined(__AMIGA__)
    const char *gngeo_dir = "/PROGDIR/save/";
#else
    const char *gngeo_dir = get_gngeo_dir();
#endif
    FILE *f;
    int len = strlen(name) + strlen(gngeo_dir) + 4; /* ".nv\0" => 4 */

    filename = (char *) alloca(len);
    sprintf(filename, "%s%s.nv", gngeo_dir, name);

    if ((f = fopen(filename, "rb")) == 0)
        return;
    totread = fread(memory.sram, 1, 0x10000, f);
    fclose(f);

}

/* TODO: multiple memcard */
void open_memcard(char *name) {
    char *filename;
    size_t totread = 0;
#ifdef EMBEDDED_FS
    const char *gngeo_dir = ROOTPATH"save/";
#elif defined(__AMIGA__)
    const char *gngeo_dir = "/PROGDIR/save/";
#else
    const char *gngeo_dir = get_gngeo_dir();
#endif
    FILE *f;
    int len = strlen("memcard") + strlen(gngeo_dir) + 1; /* ".nv\0" => 4 */

    filename = (char *) alloca(len);
    sprintf(filename, "%s%s", gngeo_dir, "memcard");

    if ((f = fopen(filename, "rb")) == 0)
        return;
    totread = fread(memory.memcard, 1, 0x800, f);
    fclose(f);
}

void save_nvram(char *name) {
    char *filename;
#ifdef EMBEDDED_FS
    const char *gngeo_dir = ROOTPATH"save/";
#elif defined(__AMIGA__)
    const char *gngeo_dir = strdup("/PROGDIR/save/");
#else
    const char *gngeo_dir = get_gngeo_dir();
#endif
    FILE *f;
    int len = strlen(name) + strlen(gngeo_dir) + 4; /* ".nv\0" => 4 */

    //strlen(name) + strlen(getenv("HOME")) + strlen("/.gngeo/") + 4;
    int i;
    //    printf("Save nvram %s\n",name);
    for (i = 0xffff; i >= 0; i--) {
        if (memory.sram[i] != 0)
            break;
    }

    filename = (char *) alloca(len);

    sprintf(filename, "%s%s.nv", gngeo_dir, name);

    if ((f = fopen(filename, "wb")) != NULL) {
        fwrite(memory.sram, 1, 0x10000, f);
        fclose(f);
    }
}

void save_memcard(char *name) {
    char *filename;
#ifdef EMBEDDED_FS
    const char *gngeo_dir = ROOTPATH"save/";
#elif defined(__AMIGA__)
    const char *gngeo_dir = strdup("/PROGDIR/save/");
#else
    const char *gngeo_dir = get_gngeo_dir();
#endif
    FILE *f;
    int len = strlen("memcard") + strlen(gngeo_dir) + 1; /* ".nv\0" => 4 */

    filename = (char *) alloca(len);
    sprintf(filename, "%s%s", gngeo_dir, "memcard");

    if ((f = fopen(filename, "wb")) != NULL) {
        fwrite(memory.memcard, 1, 0x800, f);
        fclose(f);
    }
}

int close_game(void) {
    if (conf.game == NULL) return GN_FALSE;
    save_nvram(conf.game);
    save_memcard(conf.game);

    dr_free_roms(&memory.rom);
    trans_pack_free();

    return GN_TRUE;
}

int load_game_config(char *rom_name) {
	char *gpath;
	char *drconf;
#ifdef EMBEDDED_FS
    gpath=ROOTPATH"conf/";
#else
    gpath=get_gngeo_dir();
#endif
	cf_reset_to_default();
	cf_open_file(NULL); /* Reset possible previous setting */
	if (rom_name) {
		if (strstr(rom_name,".gno")!=NULL) {
			char *name=dr_gno_romname(rom_name);
			if (name) {
				printf("Tring to load a gno file %s %s\n",rom_name,name);
				drconf=alloca(strlen(gpath)+strlen(name)+strlen(".cf")+1);
				sprintf(drconf,"%s%s.cf",gpath,name);
			} else {
				printf("Error while loading %s\n",rom_name);
				return GN_FALSE;
			}
		} else {
			drconf=alloca(strlen(gpath)+strlen(rom_name)+strlen(".cf")+1);
			sprintf(drconf,"%s%s.cf",gpath,rom_name);
		}
		cf_open_file(drconf);
	}
	return GN_TRUE;
}

int init_game(char *rom_name) {
//printf("AAA Blitter %s effect %s\n",CF_STR(cf_get_item_by_name("blitter")),CF_STR(cf_get_item_by_name("effect")));

	load_game_config(rom_name);
	/* reinit screen if necessary */
	//screen_change_blitter_and_effect(NULL,NULL);
	reset_frame_skip();
	screen_reinit();
	printf("BBB Blitter %s effect %s\n",CF_STR(cf_get_item_by_name("blitter")),CF_STR(cf_get_item_by_name("effect")));
    /* open transpack if need */
    trans_pack_open(CF_STR(cf_get_item_by_name("transpack")));

    if (strstr(rom_name, ".gno") != NULL) {
        if (dr_open_gno(rom_name)==GN_FALSE)
        	return GN_FALSE;
    } else {

        //open_rom(rom_name);
	if (dr_load_game(rom_name) == GN_FALSE) {
#if defined(GP2X)
            gn_popup_error(" Error! :", "Couldn't load %s",
                    file_basename(rom_name));
#else
            printf("Can't load %s\n", rom_name);
#endif
            return GN_FALSE;
        }

    }

    open_nvram(conf.game);

    open_memcard(conf.game);
#ifndef GP2X /* crash on the gp2x */
    sdl_set_title(conf.game);
#endif
    gn_loading_info("Initializing CPU...");
    init_neo();
    setup_misc_patch(conf.game);

    fix_usage = memory.fix_board_usage;
    current_pal = memory.vid.pal_neo[0];
    current_fix = memory.rom.bios_sfix.p;
    current_pc_pal = (Uint32 *) memory.vid.pal_host[0];

	memory.vid.currentpal=0;
	memory.vid.currentfix=0;


    return GN_TRUE;
}
