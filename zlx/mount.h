#ifndef MOUNT_H
#define	MOUNT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <libfat/fat.h>
#include <libext2/ext2.h>
#include <libxtaf/xtaf.h>
#include <iso9660/iso9660.h>
#include <sys/iosupport.h>
#include <diskio/disc_io.h>
#include <byteswap.h>

#define le32_to_cpu(x) bswap_32(x)

#define BYTES_PER_SECTOR 4096
#define NTFS_OEM_ID                         (0x4e54465320202020ULL)

#define PARTITION_TYPE_EMPTY                0x00 /* Empty */
#define PARTITION_TYPE_DOS33_EXTENDED       0x05 /* DOS 3.3+ extended partition */
#define PARTITION_TYPE_NTFS                 0x07 /* Windows NT NTFS */
#define PARTITION_TYPE_WIN95_EXTENDED       0x0F /* Windows 95 extended partition */
#define PARTITION_TYPE_LINUX                0x83 /* EXT2/3/4 */

#define PARTITION_STATUS_NONBOOTABLE        0x00 /* Non-bootable */
#define PARTITION_STATUS_BOOTABLE           0x80 /* Bootable (active) */

#define MBR_SIGNATURE                       (0x55AA)
#define EBR_SIGNATURE                       (0x55AA)

#define BPB_FAT16_fileSysType  0x36
#define BPB_FAT32_fileSysType  0x52

#define T_FAT           1
#define T_NTFS          2
#define T_EXT2          3
#define T_ISO9660       4

static const char FAT_SIG[3] = {'F', 'A', 'T'};

enum {
	DEVICE_USB, // usb
	DEVICE_ATA, // hdd
	DEVICE_ATAPI, // cdrom
};

/**
 * PRIMARY_PARTITION - Block device partition record
 */
typedef struct _PARTITION_RECORD {
	u8 status; /* Partition status; see above */
	u8 chs_start[3]; /* Cylinder-head-sector address to first block of partition */
	u8 type; /* Partition type; see above */
	u8 chs_end[3]; /* Cylinder-head-sector address to last block of partition */
	u32 lba_start; /* Local block address to first sector of partition */
	u32 block_count; /* Number of blocks in partition */
} __attribute__((__packed__)) PARTITION_RECORD;

/**
 * MASTER_BOOT_RECORD - Block device master boot record
 */
typedef struct _MASTER_BOOT_RECORD {
	u8 code_area[446]; /* Code area; normally empty */
	PARTITION_RECORD partitions[4]; /* 4 primary partitions */
	u16 signature; /* MBR signature; 0xAA55 */
} __attribute__((__packed__)) MASTER_BOOT_RECORD;

/**
 * struct BIOS_PARAMETER_BLOCK - BIOS parameter block (bpb) structure.
 */
typedef struct {
	u16 bytes_per_sector; /* Size of a sector in bytes. */
	u8 sectors_per_cluster; /* Size of a cluster in sectors. */
	u16 reserved_sectors; /* zero */
	u8 fats; /* zero */
	u16 root_entries; /* zero */
	u16 sectors; /* zero */
	u8 media_type; /* 0xf8 = hard disk */
	u16 sectors_per_fat; /* zero */
	u16 sectors_per_track; /* Required to boot Windows. */
	u16 heads; /* Required to boot Windows. */
	u32 hidden_sectors; /* Offset to the start of the partition */
	u32 large_sectors; /* zero */
} __attribute__((__packed__)) BIOS_PARAMETER_BLOCK;

/**
 * struct NTFS_BOOT_SECTOR - NTFS boot sector structure.
 */
typedef struct {
	u8 jump[3]; /* Irrelevant (jump to boot up code).*/
	u64 oem_id; /* Magic "NTFS    ". */
	BIOS_PARAMETER_BLOCK bpb; /* See BIOS_PARAMETER_BLOCK. */
	u8 physical_drive; /* 0x00 floppy, 0x80 hard disk */
	u8 current_head; /* zero */
	u8 extended_boot_signature; /* 0x80 */
	u8 reserved2; /* zero */
	s64 number_of_sectors; /* Number of sectors in volume. */
	s64 mft_lcn; /* Cluster location of mft data. */
	s64 mftmirr_lcn; /* Cluster location of copy of mft. */
	s8 clusters_per_mft_record; /* Mft record size in clusters. */
	u8 reserved0[3]; /* zero */
	s8 clusters_per_index_record; /* Index block size in clusters. */
	u8 reserved1[3]; /* zero */
	u64 volume_serial_number; /* Irrelevant (serial number). */
	u32 checksum; /* Boot sector checksum. */
	u8 bootstrap[426]; /* Irrelevant (boot up code). */
	u16 end_of_sector_marker; /* End of bootsector magic. */
} __attribute__((__packed__)) NTFS_BOOT_SECTOR;

/**
 * EXTENDED_PARTITION - Block device extended boot record
 */
typedef struct _EXTENDED_BOOT_RECORD {
	u8 code_area[446]; /* Code area; normally empty */
	PARTITION_RECORD partition; /* Primary partition */
	PARTITION_RECORD next_ebr; /* Next extended boot record in the chain */
	u8 reserved[32]; /* Normally empty */
	u16 signature; /* EBR signature; 0xAA55 */
} __attribute__((__packed__)) EXTENDED_BOOT_RECORD;

#define MAX_DEVICES 10

typedef struct {
	char name[50];
	char mount[10];
	int type;
	DISC_INTERFACE* interface;
	sec_t sector;
} DEVICE_STRUCT;

static void AddPartition(sec_t sector, int device, int type, int *devnum);
static int FindPartitions(int device);
static void UnmountPartitions(int device);
extern int XTAFMount();
void mount_all_devices();
void findDevices();
int get_devices(int j, char * m);


#ifdef	__cplusplus
}
#endif

#endif	/* MOUNT_H */
