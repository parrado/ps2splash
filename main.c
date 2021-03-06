
#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <unistd.h>
#include <input.h>
#include <time.h>
#include <string.h>
#include <libcdvd.h>

#include <fcntl.h>
#include <sbv_patches.h>

#include <stdio.h>
#include <debug.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <stdint.h>
#include "about.h"
#include "OSDInit.h"
#include "spng.h"

#define NTSC 2
#define PAL 3

#define DELAY 5000

char romver_region_char[1];
u8 romver[16];

// gs.c
typedef enum
{
	PAL_640_512_32,
	NTSC_640_448_32
} gs_video_mode;

int width, height;
spng_ctx *ctx = NULL;
struct spng_ihdr ihdr;
FILE *fdn;

void delay(u64 msec)
{
	u64 start;

	TimerInit();

	start = Timer();

	while (Timer() <= (start + msec))
		;

	TimerEnd();
}

void ResetIOP()
{
	SifInitRpc(0);
	while (!SifIopReset("", 0))
	{
	};
	while (!SifIopSync())
	{
	};
	SifInitRpc(0);
}

void InitPS2()
{
	init_scr();
	ResetIOP();
	SifInitIopHeap();
	SifLoadFileInit();
	fioInit();
	sbv_patch_disable_prefix_check();
	SifLoadModule("rom0:SIO2MAN", 0, NULL);
	SifLoadModule("rom0:MCMAN", 0, NULL);
	SifLoadModule("rom0:MCSERV", 0, NULL);
}

void LoadElf(const char *elf, char *path)
{
	char *args[1];
	t_ExecData exec;
	SifLoadElf(elf, &exec);

	if (exec.epc > 0)
	{
		ResetIOP();

		if (path != 0)
		{
			args[0] = path;
			ExecPS2((void *)exec.epc, (void *)exec.gp, 1, args);
		}
		else
		{
			ExecPS2((void *)exec.epc, (void *)exec.gp, 0, NULL);
		}
	}
}

void read_png_file(char *filename)
{
	FILE *fdn = fopen(filename, "rb");
	ctx = spng_ctx_new(0);
	/* Ignore and don't calculate chunk CRC's */
	spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);

	/* Set memory usage limits for storing standard and unknown chunks,
       this is important when reading arbitrary files! */
	size_t limit = 1024 * 1024 * 64;
	spng_set_chunk_limits(ctx, limit, limit);

	/* Set source PNG */
	spng_set_png_file(ctx, fdn); /* or _buffer(), _stream() */

	spng_get_ihdr(ctx, &ihdr);

	width = ihdr.width;
	height = ihdr.height;
}

int file_exists(char filepath[])
{
	int fdn;

	fdn = open(filepath, O_RDONLY);
	if (fdn < 0)
		return 0;
	close(fdn);

	return 1;
}

int main(int argc, char *argv[])
{

	uint32_t *splash;
	uint8_t *out;
	uint8_t *aux;
	int x, r;

	InitPS2();
	scr_clear();

	int fdn;
	if ((fdn = open("rom0:ROMVER", O_RDONLY)) > 0)
	{ // Reading ROMVER
		read(fdn, romver, sizeof romver);
		close(fdn);
	}

	// Getting region char
	romver_region_char[0] = (romver[4] == 'E' ? 'E' : (romver[4] == 'J' ? 'I' : (romver[4] == 'H' ? 'A' : (romver[4] == 'U' ? 'A' : romver[4]))));

	int VMode = NTSC;

	gs_reset(); // Reset GS

	if (romver[4] == 'E')
		VMode = PAL; // Set Video mode

	// Init GS with the good vmode
	if (VMode == PAL)
	{
		gs_init(PAL_640_512_32);
	}
	else
	{
		gs_init(NTSC_640_448_32);
	}
	scr_printf("\n\n\n\n HWC's OSDSYS Launcher + ps2splash by alexparrado\n");

	char fileName0[] = "mc0:ps2splash.png";
	char fileName1[] = "mc1:ps2splash.png";

	int f0 = file_exists(fileName0);
	int f1 = file_exists(fileName1);

	if (f0)
		read_png_file(fileName0);
	else
	{
		if (f1)
			read_png_file(fileName1);
	}

	if (f0 || f1)
	{

		if (width > gs_get_max_x() || height > gs_get_max_y())
		{
			scr_printf(" Image is too large and does not fit in your screen\n");
		}
		else
		{
			size_t out_size, out_width;

			int fmt = SPNG_FMT_RGBA8;

			r = spng_decoded_image_size(ctx, fmt, &out_size);

			splash = (uint32_t *)memalign(16, out_size);
			if (splash == NULL)
			{
				scr_printf(" Error allocating temporary data buffer, is image too big?\n");

				abort();
			}

			spng_decode_image(ctx, NULL, 0, fmt, SPNG_DECODE_PROGRESSIVE);

			/* ihdr.height will always be non-zero if spng_get_ihdr() succeeds */
			out_width = out_size / height;

			struct spng_row_info row_info = {0};

			out = splash;

			do
			{
				r = spng_get_row_info(ctx, &row_info);
				if (r)
					break;
				aux = out + row_info.row_num * out_width;
				spng_decode_row(ctx, aux, out_width);

				for (x = 0; x < width; x++)
				{
					aux[4 * x + 2] = (uint16_t)aux[4 * x + 2] * (uint16_t)aux[4 * x + 3] / 255;
					aux[4 * x + 1] = (uint16_t)aux[4 * x + 1] * (uint16_t)aux[4 * x + 3] / 255;
					aux[4 * x + 0] = (uint16_t)aux[4 * x + 0] * (uint16_t)aux[4 * x + 3] / 255;
				}
			} while (!r);

			// clear the screen
			gs_set_fill_color(0x00, 0x00, 0x0);
			gs_fill_rect(0, 0, gs_get_max_x(), gs_get_max_y());

			// print bitmap
			gs_print_bitmap((gs_get_max_x() - width) / 2, (gs_get_max_y() - height) / 2, width, height, splash); // print centered splash bitmap

			gs_print_bitmap((gs_get_max_x() - about_w), (gs_get_max_y() - about_h), about_w, about_h, about);

			delay(DELAY);
			free(splash);
		}
	}

	else
	{
		scr_printf(" ps2splash.png could not be found in mc0 neither mc1\n");
	}

	int osdsys_exists(char filepath[])
	{
		filepath[6] = (romver[4] == 'E' ? 'E' : (romver[4] == 'J' ? 'I' : (romver[4] == 'H' ? 'A' : (romver[4] == 'U' ? 'A' : romver[4]))));

		int fdn;

		fdn = open(filepath, O_RDONLY);
		if (fdn < 0)
			return 0;
		close(fdn);

		return 1;
	}
	void CargarelOSDSYS(char default_OSDSYS_path[])
	{
		char arg0[20], arg1[20], arg2[20], arg3[40];
		char *args[4] = {arg0, arg1, arg2, arg3};
		char kelf_loader[40];
		int argc;
		char path[1025];

		default_OSDSYS_path[6] = (romver[4] == 'E' ? 'E' : (romver[4] == 'J' ? 'I' : (romver[4] == 'H' ? 'A' : (romver[4] == 'U' ? 'A' : romver[4]))));
		/*
		if(romver[4] == 'J') default_OSDSYS_path[] = "mc:/BIEXEC-SYSTEM/osdmain.elf";
		else if(romver[4] == 'E') default_OSDSYS_path[] = "mc:/BEEXEC-SYSTEM/osdmain.elf";
		else default_OSDSYS_path[] = "mc:/BAEXEC-SYSTEM/osdmain.elf";
		*/

		strcpy(path, default_OSDSYS_path);

		strcpy(arg0, "-m rom0:SIO2MAN");
		strcpy(arg1, "-m rom0:MCMAN");
		strcpy(arg2, "-m rom0:MCSERV");
		sprintf(arg3, "-x %s", path);
		argc = 4;
		strcpy(kelf_loader, "moduleload");
		LoadExecPS2(kelf_loader, argc, args);
	}

	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osdmain.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osdmain.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd110.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd110.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd120.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd120.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd130.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd130.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd140.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd140.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd150.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd150.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd160.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd160.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd170.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd170.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd180.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd180.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd190.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd190.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd200.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd200.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd210.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd210.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd220.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd220.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd230.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd230.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd240.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd240.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd250.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd250.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd260.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd260.elf");
	if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd270.elf"))
		CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd270.elf");

	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osdmain.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osdmain.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd110.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd110.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd120.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd120.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd130.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd130.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd140.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd140.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd150.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd150.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd160.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd160.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd170.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd170.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd180.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd180.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd190.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd190.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd200.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd200.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd210.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd210.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd220.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd220.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd230.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd230.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd240.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd240.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd250.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd250.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd260.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd260.elf");
	if (osdsys_exists("mc1:/B?EXEC-SYSTEM/osd270.elf"))
		CargarelOSDSYS("mc1:/B?EXEC-SYSTEM/osd270.elf");

	if (file_exists("mc0:/BOOT/BOOT2.ELF"))
		LoadElf("mc0:/BOOT/BOOT2.ELF", "mc0:/BOOT/");
	if (file_exists("mc0:/FORTUNA/BOOT2.ELF"))
		LoadElf("mc0:/FORTUNA/BOOT2.ELF", "mc0:/FORTUNA/");
	if (file_exists("mc0:/APPS/BOOT.ELF"))
		LoadElf("mc0:/APPS/BOOT.ELF", "mc0:/APPS/");
	if (file_exists("mc0:/APPS/ULE.ELF"))
		LoadElf("mc0:/APPS/ULE.ELF", "mc0:/APPS/");
	if (file_exists("mc0:/BOOT/ULE.ELF"))
		LoadElf("mc0:/BOOT/ULE.ELF", "mc0:/BOOT/");
	if (file_exists("mc0:/APPS/WLE.ELF"))
		LoadElf("mc0:/APPS/WLE.ELF", "mc0:/APPS/");
	if (file_exists("mc0:/BOOT/WLE.ELF"))
		LoadElf("mc0:/BOOT/WLE.ELF", "mc0:/BOOT/");
	if (file_exists("mc1:/BOOT/BOOT2.ELF"))
		LoadElf("mc1:/BOOT/BOOT2.ELF", "mc1:/BOOT/");
	if (file_exists("mc1:/FORTUNA/BOOT2.ELF"))
		LoadElf("mc1:/FORTUNA/BOOT2.ELF", "mc1:/FORTUNA/");
	if (file_exists("mc1:/APPS/BOOT.ELF"))
		LoadElf("mc1:/APPS/BOOT.ELF", "mc1:/APPS/");
	if (file_exists("mc1:/APPS/ULE.ELF"))
		LoadElf("mc1:/APPS/ULE.ELF", "mc1:/APPS/");
	if (file_exists("mc1:/BOOT/ULE.ELF"))
		LoadElf("mc1:/BOOT/ULE.ELF", "mc1:/BOOT/");
	if (file_exists("mc1:/APPS/WLE.ELF"))
		LoadElf("mc1:/APPS/WLE.ELF", "mc1:/APPS/");
	if (file_exists("mc1:/BOOT/WLE.ELF"))
		LoadElf("mc1:/BOOT/WLE.ELF", "mc1:/BOOT/");
	if (file_exists("mc1:/FORTUNA/BOOT.ELF"))
		LoadElf("mc1:/FORTUNA/BOOT.ELF", "mc1:/FORTUNA/");

	/*
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osdmain.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osdmain.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd110.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd110.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd120.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd120.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd130.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd130.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd140.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd140.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd150.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd150.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd160.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd160.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd170.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd170.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd180.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd180.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd190.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd190.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd200.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd200.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd210.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd210.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd220.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd220.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd230.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd230.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd240.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd240.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd250.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd250.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd260.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd260.elf");
	if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd270.elf")) CargarelOSDSYS("mc:/BAEXEC-SYSTEM/osd270.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osdmain.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osdmain.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd110.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd110.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd120.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd120.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd130.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd130.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd140.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd140.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd150.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd150.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd160.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd160.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd170.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd170.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd180.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd180.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd190.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd190.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd200.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd200.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd210.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd210.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd220.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd220.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd230.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd230.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd240.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd240.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd250.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd250.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd260.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd260.elf");
	if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd270.elf")) CargarelOSDSYS("mc:/BEEXEC-SYSTEM/osd270.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osdmain.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osdmain.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd110.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd110.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd120.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd120.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd130.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd130.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd140.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd140.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd150.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd150.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd160.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd160.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd170.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd170.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd180.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd180.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd190.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd190.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd200.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd200.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd210.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd210.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd220.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd220.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd230.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd230.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd240.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd240.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd250.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd250.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd260.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd260.elf");
	if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd270.elf")) CargarelOSDSYS("mc:/BIEXEC-SYSTEM/osd270.elf");
	*/
	/////////////////
	// Agregar dentro del while la llamada al SYSLOOP.ELF
	scr_printf("\n Prioritary region: EUR \n");
	scr_printf("\n\n If you stuck on this screen for a while, \n verify you have connected a Memory Card \n with XtremeEliteBoot+ or FreeMCBoot \n properly installed. \n");
	while (1)
	{
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osdmain.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osdmain.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd110.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd110.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd120.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd120.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd130.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd130.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd140.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd140.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd150.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd150.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd160.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd160.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd170.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd170.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd180.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd180.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd190.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd190.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd200.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd200.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd210.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd210.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd220.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd220.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd230.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd230.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd240.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd240.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd250.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd250.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd260.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd260.elf");
		if (osdsys_exists("mc0:/B?EXEC-SYSTEM/osd270.elf"))
			CargarelOSDSYS("mc0:/B?EXEC-SYSTEM/osd270.elf");
		//
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osdmain.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osdmain.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd110.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd110.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd120.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd120.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd130.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd130.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd140.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd140.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd150.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd150.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd160.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd160.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd170.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd170.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd180.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd180.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd190.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd190.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd200.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd200.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd210.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd210.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd220.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd220.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd230.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd230.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd240.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd240.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd250.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd250.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd260.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd260.elf");
		if (osdsys_exists("mc0:/BEEXEC-SYSTEM/osd270.elf"))
			CargarelOSDSYS("mc0:/BEEXEC-SYSTEM/osd270.elf");
		//
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osdmain.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osdmain.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd110.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd110.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osdsys.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osdsys.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osdmain.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osdmain.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd110.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd110.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd120.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd120.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd130.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd130.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd140.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd140.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd150.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd150.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd160.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd160.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd170.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd170.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd180.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd180.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd190.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd190.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd200.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd200.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd210.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd210.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd220.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd220.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd230.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd230.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd240.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd240.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd250.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd250.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd260.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd260.elf");
		if (osdsys_exists("mc0:/BAEXEC-SYSTEM/osd270.elf"))
			CargarelOSDSYS("mc0:/BAEXEC-SYSTEM/osd270.elf");
		//
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osdmain.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osdmain.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd110.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd110.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd120.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd120.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd130.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd130.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd140.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd140.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd150.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd150.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd160.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd160.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd170.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd170.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd180.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd180.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd190.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd190.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd200.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd200.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd210.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd210.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd220.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd220.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd230.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd230.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd240.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd240.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd250.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd250.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd260.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd260.elf");
		if (osdsys_exists("mc0:/BIEXEC-SYSTEM/osd270.elf"))
			CargarelOSDSYS("mc0:/BIEXEC-SYSTEM/osd270.elf");
		//CargarelOSDSYS("mc:/B?EXEC-SYSTEM/osdmain.elf");
		if (file_exists("mc0:/BOOT/BOOT2.ELF"))
			LoadElf("mc0:/BOOT/BOOT2.ELF", "mc0:/BOOT/");
		if (file_exists("mc0:/FORTUNA/BOOT2.ELF"))
			LoadElf("mc0:/FORTUNA/BOOT2.ELF", "mc0:/FORTUNA/");
		if (file_exists("mc0:/APPS/BOOT.ELF"))
			LoadElf("mc0:/APPS/BOOT.ELF", "mc0:/APPS/");
		if (file_exists("mc0:/APPS/ULE.ELF"))
			LoadElf("mc0:/APPS/ULE.ELF", "mc0:/APPS/");
		if (file_exists("mc0:/BOOT/ULE.ELF"))
			LoadElf("mc0:/BOOT/ULE.ELF", "mc0:/BOOT/");
		if (file_exists("mc0:/APPS/WLE.ELF"))
			LoadElf("mc0:/APPS/WLE.ELF", "mc0:/APPS/");
		if (file_exists("mc0:/BOOT/WLE.ELF"))
			LoadElf("mc0:/BOOT/WLE.ELF", "mc0:/BOOT/");
		if (file_exists("mc1:/BOOT/BOOT2.ELF"))
			LoadElf("mc1:/BOOT/BOOT2.ELF", "mc1:/BOOT/");
		if (file_exists("mc1:/FORTUNA/BOOT2.ELF"))
			LoadElf("mc1:/FORTUNA/BOOT2.ELF", "mc1:/FORTUNA/");
		if (file_exists("mc1:/APPS/BOOT.ELF"))
			LoadElf("mc1:/APPS/BOOT.ELF", "mc1:/APPS/");
		if (file_exists("mc1:/APPS/ULE.ELF"))
			LoadElf("mc1:/APPS/ULE.ELF", "mc1:/APPS/");
		if (file_exists("mc1:/BOOT/ULE.ELF"))
			LoadElf("mc1:/BOOT/ULE.ELF", "mc1:/BOOT/");
		if (file_exists("mc1:/APPS/WLE.ELF"))
			LoadElf("mc1:/APPS/WLE.ELF", "mc1:/APPS/");
		if (file_exists("mc1:/BOOT/WLE.ELF"))
			LoadElf("mc1:/BOOT/WLE.ELF", "mc1:/BOOT/");
		if (file_exists("mc1:/FORTUNA/BOOT.ELF"))
			LoadElf("mc1:/FORTUNA/BOOT.ELF", "mc1:/FORTUNA/");
	}

	return 0;
}
