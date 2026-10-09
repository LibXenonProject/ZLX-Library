#include "mount.h"
//#include "../mplayer/mplayerlib.h"

extern DISC_INTERFACE xenon_atapi_ops;
extern DISC_INTERFACE xenon_ata_ops;
extern DISC_INTERFACE usb2mass_ops;

//#define DEBUG_MOUNTALL

#ifdef DEBUG_MOUNTALL
#define debug_printf(fmt, args...) \
        fprintf(stderr, "%s:%d:" fmt, __FUNCTION__, __LINE__, ##args)
#else
#define debug_printf(fmt, args...)
#endif

char * root_dev = NULL;
static int device_list_size = 0;
static char device_list[STD_MAX][10];

static char *prefix[] = {"uda", "sda", "dvd"};

DEVICE_STRUCT part[2][MAX_DEVICES];

static void AddPartition(sec_t sector, int device, int type, int *devnum) {
	int i;

	if (*devnum >= MAX_DEVICES)
		return;

	for (i = 0; i < *devnum; i++)
		if (part[device][i].sector == sector) return; // to avoid mount same partition again

	DISC_INTERFACE *disc = (DISC_INTERFACE *) & xenon_ata_ops;

	if (device == DEVICE_USB)
		disc = (DISC_INTERFACE *) & usb2mass_ops;
	
	else if(device == DEVICE_ATAPI)
		disc = (DISC_INTERFACE *) & xenon_atapi_ops;

	char mount[10];
	sprintf(mount, "%s%i", prefix[device], *devnum);
	char *name;

	switch (type) {
		case T_FAT:
			if (!fatMount(mount, disc, sector, 2, 64))
				return;
			fatGetVolumeLabel(mount, part[device][*devnum].name);
			break;
		case T_EXT2:
			if (!ext2Mount(mount, disc, sector, 2, 128, EXT2_FLAG_DEFAULT))
				return;

			name = (char *) ext2GetVolumeName(mount);

			if (name && name[0])
				strcpy(part[device][*devnum].name, name);
			else
				part[device][*devnum].name[0] = 0;
			break;
		case T_ISO9660:
			if (!ISO9660_Mount(mount, disc))
				return;

			name = (char *) ISO9660_GetVolumeLabel(mount);

			if (name && name[0])
				strcpy(part[device][*devnum].name, name);
			else
				strcpy(part[device][*devnum].name, "DVD");
			break;
	}

	int c = strlen(part[device][*devnum].name) - 1;

	while (c >= 0 && part[device][*devnum].name[c] == ' ')
		part[device][*devnum].name[c--] = 0;

	strcpy(part[device][*devnum].mount, mount);
	part[device][*devnum].interface = disc;
	part[device][*devnum].sector = sector;
	part[device][*devnum].type = type;
	++*devnum;
}

static int FindPartitions(int device) {
	int i;
	int devnum = 0;

	// clear list
	for (i = 0; i < MAX_DEVICES; i++) {
		part[device][i].name[0] = 0;
		part[device][i].mount[0] = 0;
		part[device][i].sector = 0;
		part[device][i].interface = NULL;
		part[device][i].type = 0;
	}

	DISC_INTERFACE *interface;

	switch(device){
		case DEVICE_ATAPI:
			interface = (DISC_INTERFACE *) & xenon_atapi_ops;
			break;
		case DEVICE_ATA:
			interface = (DISC_INTERFACE *) & xenon_ata_ops;
			break;
		case DEVICE_USB:
			interface = (DISC_INTERFACE *) & usb2mass_ops;
			break;
	}


	MASTER_BOOT_RECORD mbr;
	PARTITION_RECORD *partition = NULL;
	devnum = 0;
	sec_t part_lba = 0;

	union {
		u8 buffer[BYTES_PER_SECTOR];
		MASTER_BOOT_RECORD mbr;
		EXTENDED_BOOT_RECORD ebr;
		NTFS_BOOT_SECTOR boot;
	} sector;

	if(device == DEVICE_ATAPI){
		AddPartition(0, device, T_ISO9660, &devnum);
		return devnum;
	}

	// Read the first sector on the device
	if (!interface->readSectors(0, 1, &sector.buffer)) {
		//errno = EIO;
		return -1;
	}

	// If this is the devices master boot record
	debug_printf("0x%x\n", sector.mbr.signature);
	if (sector.mbr.signature == MBR_SIGNATURE) {
		memcpy(&mbr, &sector, sizeof (MASTER_BOOT_RECORD));
		debug_printf("Valid Master Boot Record found\n");

		// Search the partition table for all partitions (max. 4 primary partitions)
		for (i = 0; i < 4; i++) {
			partition = &mbr.partitions[i];
			part_lba = le32_to_cpu(mbr.partitions[i].lba_start);

			debug_printf(
					"Partition %i: %s, sector %u, type 0x%x\n",
					i + 1,
					partition->status == PARTITION_STATUS_BOOTABLE ? "bootable (active)"
					: "non-bootable", part_lba, partition->type);

			// Figure out what type of partition this is
			switch (partition->type) {
				case PARTITION_TYPE_DOS33_EXTENDED:
				case PARTITION_TYPE_WIN95_EXTENDED:
				{
					debug_printf("Partition %i: Claims to be Extended\n", i + 1);

					// Walk the extended partition chain
					sec_t ebr_lba = part_lba;
					sec_t next_erb_lba = 0;
					do {
						// Read and validate the extended boot record
						if (interface->readSectors(ebr_lba + next_erb_lba, 1, &sector)) {
							if (sector.ebr.signature == EBR_SIGNATURE) {
								debug_printf(
										"Logical Partition @ %d: %s type 0x%x\n",
										ebr_lba + next_erb_lba,
										sector.ebr.partition.status
										== PARTITION_STATUS_BOOTABLE ? "bootable (active)"
										: "non-bootable",
										sector.ebr.partition.type);

								// Get the start sector of the current partition
								// and the next extended boot record in the chain
								part_lba = ebr_lba + next_erb_lba
										+ le32_to_cpu(
										sector.ebr.partition.lba_start);
								next_erb_lba = le32_to_cpu(
										sector.ebr.next_ebr.lba_start);

								if (sector.ebr.partition.type == PARTITION_TYPE_LINUX) {
									debug_printf("Partition : type ext2/3/4 found\n");
									AddPartition(part_lba, device, T_EXT2, &devnum);
								}// Check if this partition has a valid FAT boot record
								else if (interface->readSectors(part_lba, 1, &sector)) {
									if (!memcmp(sector.buffer
											+ BPB_FAT16_fileSysType, FAT_SIG,
											sizeof (FAT_SIG)) || !memcmp(
											sector.buffer
											+ BPB_FAT32_fileSysType,
											FAT_SIG, sizeof (FAT_SIG))) {
										debug_printf("Partition : Valid FAT boot sector found\n");
										AddPartition(part_lba, device, T_FAT, &devnum);
									}
								}
							} else {
								next_erb_lba = 0;
							}
						}
					} while (next_erb_lba);
					break;
				}
				case PARTITION_TYPE_LINUX:
				{
					debug_printf("Partition %i: Claims to be LINUX\n", i + 1);

					// Read and validate the EXT2 partition
					AddPartition(part_lba, device, T_EXT2, &devnum);
					break;
				}
					// Ignore empty partitions
				case PARTITION_TYPE_EMPTY:
					debug_printf("Partition %i: Claims to be empty\n", i + 1);
					// Unknown or unsupported partition type
				default:
				{
					// Check if this partition has a FAT or EXT2FS filesystem anyway
					// it might be misrepresented due to a lazy partition editor
					if (interface->readSectors(part_lba, 1, &sector)) {
						if (!memcmp(sector.buffer + BPB_FAT16_fileSysType,
								FAT_SIG, sizeof (FAT_SIG)) || !memcmp(
								sector.buffer + BPB_FAT32_fileSysType, FAT_SIG,
								sizeof (FAT_SIG))) {
							debug_printf("Partition : Valid FAT boot sector found\n");
							AddPartition(part_lba, device, T_FAT, &devnum);
						} else {
							debug_printf("Trying : ext partition\n");
							AddPartition(part_lba, device, T_EXT2, &devnum);
						}
					}
					break;
				}
			}
		}
	}
	if (devnum == 0) // it is assumed this device has no master boot record or no partitions found
	{
		debug_printf("No Master Boot Record was found or no partitions found!\n");

		// As a last-ditched effort, search the first 64 sectors of the device for stray FAT or EXT2FS partitions
		for (i = 0; i < 64; i++) {
			if (interface->readSectors(i, 1, &sector)) {
			    if (!memcmp(sector.buffer + BPB_FAT16_fileSysType, FAT_SIG,
						sizeof (FAT_SIG)) || !memcmp(sector.buffer
						+ BPB_FAT32_fileSysType, FAT_SIG, sizeof (FAT_SIG))) {
					debug_printf("Partition : Valid FAT boot sector found\n");
					AddPartition(i, device, T_FAT, &devnum);
					break;
				} else {
					debug_printf("Trying : ext partition\n");
					AddPartition(part_lba, device, T_EXT2, &devnum);
				}
			}
		}
	}
	return devnum;
}

static void UnmountPartitions(int device) {
	char mount[11];
	int i;
	for (i = 0; i < MAX_DEVICES; i++) {
		switch (part[device][i].type) {
			case T_FAT:
				part[device][i].type = 0;
				sprintf(mount, "%s:", part[device][i].mount);
				fatUnmount(mount);
				break;
			case T_EXT2:
				part[device][i].type = 0;
				ext2Unmount(part[device][i].mount);
				break;

			case T_ISO9660:
				part[device][i].type = 0;
				sprintf(mount, "%s:", part[device][i].mount);
				ISO9660_Unmount(mount);
				break;
		}
		part[device][i].name[0] = 0;
		part[device][i].mount[0] = 0;
		part[device][i].sector = 0;
		part[device][i].interface = NULL;
	}
}

/**
 * Parse mbr for filesystem
 */
void mount_all_devices() {
	FindPartitions(DEVICE_USB);
	if (xenon_ata_ops.isInserted()) {
		XTAFMount();
		FindPartitions(DEVICE_ATA);
	}
	if (xenon_atapi_ops.isInserted()) {
		FindPartitions(DEVICE_ATAPI);
	}
}

void findDevices() {
	for (int i = 3; i < STD_MAX; i++) {
		if (devoptab_list[i]->structSize) {
			//strcpy(device_list[device_list_size],devoptab_list[i]->name);
			sprintf(device_list[device_list_size], "%s:/", devoptab_list[i]->name);
			printf("findDevices : %s\r\n", device_list[device_list_size]);
			device_list_size++;
		}
	}

	root_dev = device_list[0];
}

int get_devices(int j, char * m) {
	int i;
	for (i = 3 + j; i < STD_MAX; i++) {
		if (devoptab_list[i]->structSize) {
			sprintf(m, "%s:/", devoptab_list[i]->name);
			printf("found\n");
			break;
		}
	}
	i++;
	if (i >= STD_MAX) {
		sprintf(m, "%s:/", devoptab_list[3]->name);
		i = 4;
	}
	return i - 3;
}
