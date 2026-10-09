/* 
 * File:   action.cpp
 * Author: cc
 *
 * Created on 28 juillet 2011, 21:39
 */
#include <stdio.h>
#include <stdlib.h>

#include <Console.h>
#include <Draw.h>
#include <Browser.h>
#ifdef LIBXENON

#include <debug.h>
#include <xenos/xenos.h>

#include <diskio/ata.h>
extern "C" {
#include <input/input.h>
#include <xenon_nand/xenon_sfcx.h>
#include "tftp.h"
}
#include <console/console.h>
#include <diskio/disc_io.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>

#include <dirent.h>
#include <elf/elf.h>
#else
#include "posix\dirent.h"
#include "posix\input.h"
#endif
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <string>
#include <math.h>

#include "tbrowser.h"

#define printf
//#define XELL_2S

void ActionReturnToXell(void * unused) {
    exit(0);
}

void ActionBootTFTP(void * unused) {
    int d = boot_tftp_url("192.168.1.98:/tftpboot/xenon");
}

void ActionShutdown(void * unused) {
    xenon_smc_power_shutdown();
}

void ActionRestart(void * unused) {
    // restart
    xenon_smc_power_reboot();
}


// xbrflash.c
//NAND SIZES
#define NAND_SIZE_16MB          0x1000000
#define NAND_SIZE_64MB          0x4000000
#define NAND_SIZE_128MB         0x8000000
#define NAND_SIZE_256MB         0x10000000
#define NAND_SIZE_512MB         0x20000000

// xbrflash.c

int detect_read_write_modus(int nand_size) {
    //64MB XBR-File
    /*if( ftell(f) == NAND_SIZE_64MB / sfc.page_sz * sfc.page_sz_phys )
        sfc.size_bytes = NAND_SIZE_64MB;*/

    printf("detect_read_write_modus %i\r\n", sfc.size_bytes / sfc.page_sz * sfc.page_sz_phys);
    printf("detect_read_write_modus %i\r\n", NAND_SIZE_64MB / sfc.page_sz * sfc.page_sz_phys);

    if (nand_size == sfc.size_bytes / sfc.page_sz * sfc.page_sz_phys) {
        printf("Detected RAW nand file, verifying in raw mode.\n");
        return 1;
    } else if (nand_size == NAND_SIZE_64MB / sfc.page_sz * sfc.page_sz_phys) {
        printf("Detected RAW nand file, verifying in raw mode(no mu).\n");
        return 1;
    } else if (nand_size == NAND_SIZE_16MB || nand_size == NAND_SIZE_64MB || nand_size == NAND_SIZE_128MB || nand_size == NAND_SIZE_256MB || nand_size == NAND_SIZE_512MB) {
        printf("Detected short nand file, verifying in cooked mode.\n");
        return 0;
    } else {
        printf("Wrong filesize (%i). excepted: (%i bytes or %i bytes) \n", nand_size, sfc.size_bytes, sfc.size_bytes_phys);
        return -1;
    }
}

// xbrflash.c

int flash_from_file(const char *filename, int raw) {
    int i, logic_pos, status = 0x200;
    unsigned char *block, *block_flash, *bad_block;
    float pct = 0.f;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("\nError: Failed to open file: %s\n", filename);
        return -1;
    }

    printf("\nFlashing from %s...\n", filename);
    printf("0x%x block's to write...\n", sfc.size_blocks);

    if (!f)
        return -1;

    if (raw == -1) /* auto */ {
        fseek(f, 0, SEEK_END);
        raw = detect_read_write_modus(ftell(f));
        if (raw == -1)
            return -1;
        fseek(f, 0, SEEK_SET);
    }

    block = (unsigned char*) malloc(sfc.block_sz_phys);
    block_flash = (unsigned char*) malloc(sfc.block_sz_phys);
    bad_block = (unsigned char*) malloc(sfc.block_sz_phys);

    unsigned nand_size = sfc.size_bytes;

    // Dump only 64mb on big nand
    if ((nand_size == NAND_SIZE_512MB) | (nand_size == NAND_SIZE_256MB)) {
        nand_size = NAND_SIZE_64MB;
    }

    for (i = 0; i < nand_size; i += sfc.block_sz) {
        memset(block, 0xFF, sizeof (block));
        if (!fread(block, 1, sfc.page_sz_phys * sfc.pages_in_block, f)) {
            printf("Error reading ...\r\n");
            return i;
        }


        pct = ((float) i / (float) (nand_size));
        App.SetProgressValue(pct);

        // printf("Writing block: 0x%x of 0x%x (%iMB/%iMB)\r\n", sfcx_address_to_block(i) +1 , sfc.size_blocks , (i + sfc.block_sz) >> 20, nand_size >> 20);

        //Check for bad block
        sfcx_writereg(SFCX_STATUS, sfcx_readreg(SFCX_STATUS));
        sfcx_writereg(SFCX_ADDRESS, i);
        sfcx_writereg(SFCX_COMMAND, raw ? PHY_PAGE_TO_BUF : LOG_PAGE_TO_BUF);
        // Wait Busy
        while ((status = sfcx_readreg(SFCX_STATUS)) & 1);

        if ((!raw)) {
            logic_pos = sfcx_readreg(SFCX_PHYSICAL);

            if (!(logic_pos & 0x04000000)) /* shouldn't happen, unless the existing image is broken. just assume the block is okay. */ {
                printf("Error: Uh, oh, don't know. Reading @ block %08x failed. logic_pos: %i\r\n", i, sfcx_address_to_block(logic_pos));
                logic_pos = i;
            }
            logic_pos &= 0x3fffe00;

            if (logic_pos != i) {
                printf("Relocating/moving bad block from position 0x%x to 0x%x\r\n", sfcx_address_to_block(i), sfcx_address_to_block(logic_pos));
            }
        } else if (status & 0x40) //Bad Block Management
        {
            printf("Bad block ...\r\n");
#if 0
            logic_pos = sfc.last_bad_block_pos * sfc.block_sz;
            sfc.last_bad_block_pos--;

            sfcx_set_blocknumber(block, sfcx_address_to_block(i));
            sfcx_calcecc((int*) block);

            if (logic_pos != i) {
                printf("Relocating/moving bad block from position 0x%x to 0x%x\n", sfcx_address_to_block(i), sfcx_address_to_block(logic_pos));
            }
#endif
        } else {
            logic_pos = i;
        }
#if 0
        if (sfc.last_bad_block_pos == sfcx_address_to_block(i)) {
            //We reach the the last entry bad block in reserved area
            printf("\nWriting only to last entry bad block in the reserved area.");
            break;
        }
#endif

        //Erase block in Nand
        sfcx_erase_block(logic_pos);

        //Write block to Nand
        sfcx_write_block(block, logic_pos);

    }

    printf("\nWrite done\n");

    fclose(f);
    free(block);
    free(block_flash);
    free(bad_block);

    if (App.Warning("Flashing is complete, reboot the console?") == TRUE) {
        xenon_smc_power_reboot();
    }

    return 0;
}

void DumpNand(char * dest) {
    float pct = 0.0f;

    FILE *f = fopen(dest, "wb");

    if (App.Warning("Warning dump nand ???") == FALSE) {
        return;
    }

    if (f) {
        unsigned char* buff = (unsigned char*) malloc(sfc.block_sz_phys);

        unsigned int buffer_len = sfc.block_sz_phys;
        unsigned nand_size = sfc.size_bytes;

        // Dump only 64mb on big nand
        if ((nand_size == NAND_SIZE_512MB) | (nand_size == NAND_SIZE_256MB)) {
            nand_size = NAND_SIZE_64MB;
        }

        for (unsigned int i = 0; i < nand_size; i += sfc.block_sz) {
            sfcx_read_block(buff, i, 1);

            if (buffer_len != fwrite(buff, 1, buffer_len, f)) {
                App.Alert("Error dumping nand !!! please check disk space !!");
            }
            pct = ((float) i / (float) (nand_size));
            App.SetProgressValue(pct);
        }

        fclose(f);
        free(buff);
    }
}

void ActionDumpNand(void * unused) {
    char dest[512];
    lpBrowserActionEntry action = (lpBrowserActionEntry) unused;
    sprintf(dest, "%s/%s", (char*) action->param, "dump.bin");
    printf(dest);
    udelay(500);
    DumpNand(dest);
}

BOOL FileIsNand(const char * filename) {
    BOOL ret = FALSE;
    FILE * f = fopen(filename, "rb");
    if (f) {
        // get the file size
        fseek(f, 0, SEEK_END);
        int size = ftell(f);
        if (size < 0x50) {

        } else {
            void * header = malloc(0x50);
            fseek(f, 0, SEEK_SET);
            fread(header, 1, 0x50, f);

            char * copyright = (char*) header + 0x1C;
            if (strncmp("Microsoft Corporation", copyright, 0x10) == 0) {
                ret = TRUE;
            }
            printf("FileIsNand copyright : %s\r\n", copyright);
            free(header);
        }
    }
    fclose(f);
    printf("FileIsNand:%s\r\n", (ret == TRUE) ? "true" : "false");
    return ret;
}

BOOL FileIsElf(const char * filename) {
    BOOL ret = FALSE;
    FILE * f = fopen(filename, "rb");
    if (f) {
        // get the file size
        fseek(f, 0, SEEK_END);
        int size = ftell(f);
        if (size < 0x10) {

        } else {
            void * header = malloc(0x10);
            fseek(f, 0, SEEK_SET);
            fread(header, 1, 0x10, f);

            unsigned int elf_header = 0x7F454C46;
            if (memcmp(&elf_header, header, 4) == 0) {
                ret = TRUE;
            }
            free(header);
        }
    }
    fclose(f);
    printf("FileIsElf:%s\r\n", (ret == TRUE) ? "true" : "false");
    return ret;
}

void ActionFlashNand(const char * filename) {
    if (App.Warning("Continue ???") == FALSE) {
        return;
    }
    flash_from_file(filename, -1);
}

void ActionLaunchElf(char * filename) {
    char * elf = NULL;
    // read file by chunk
    FILE * f = fopen(filename, "rb");
    int size = 0;
    int chunk = 65536;
    float pct = 0.0f;

    //FileIsElf(filename);

    if (f) {
        // get the file size
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        // 0
        fseek(f, 0, SEEK_SET);

        if (size == 0)
            return;


        elf = (char*) malloc(size);

        for (int i = 0; i < size; i += fread(elf + i, 1, chunk, f)) {
            pct = ((float) i / (float) (size));
            App.SetProgressValue(pct);
        }
        fclose(f);

        // read elf header ...
        unsigned int elf_header = 0x7F454C46;
        if (memcmp(&elf_header, elf, 4) == 0) {
            char * argv[] = {
                filename,
            };
            int argc = sizeof (argv) / sizeof (char *);

            elf_setArgcArgv(argc, argv);
            elf_runFromMemory(elf, size);
        } else {

            App.Alert("Not a valid elf !!");
        }

    }
    fclose(f);
}

void ActionLaunchFile(char * filename) {

    if (FileIsElf(filename)) {
        if (App.Warning("Continue with launch elf ???") == FALSE) {
            return;
        }
        ActionLaunchElf(filename);
    }
    if (FileIsNand(filename)) {
        if (App.Warning("Flash this nand file ???") == FALSE) {
            return;
        }
        ActionFlashNand(filename);
    }
}
