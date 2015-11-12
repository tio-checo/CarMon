/*********************************************************************
 *
 * Definitions for dambi log-structured filesystem.
 *
 *********************************************************************/

#ifndef __DLFS_H
#define __DLFS_H

#include <sys/types.h>
#include <stddef.h>

/* constants */
/*
 * Flag values accessible only to dlfs_open()
 */
#define	O_CREAT		0x100	/* open with file create */

/* size of device block */
#define	DLFS_SECTOR_SIZE	512

/* last two bytes are dedicated to CRC */
#define	DLFS_CRC_SIZE		2

/* Max length of one contiguous data record. */
#define	DLFS_MAX_DATA_LN	506

/* Number of sectors between two subsequent inodes */
#define	DLFS_SECS_PER_INODE	(1024 * 1024 / DLFS_SECTOR_SIZE)
/* Space reserved to account for concept of fdisk partitions */
#define	DLFS_FDISK_SECTORS	DLFS_SECS_PER_INODE
/* Sector holding super-block */
#define	DLFS_SB_SECTOR		DLFS_FDISK_SECTORS
/* Start of the first inode. */
#define	DLFS_1STINODE_SECTOR	(DLFS_FDISK_SECTORS + DLFS_SECS_PER_INODE)

/* size of magic word */
#define	DLFS_MAGIC_SIZE		4
/* magic string identifying superblock */
#define	DLFS_MAGIC_SB		"DFSS"
/* magic string identifying inode */
#define	DLFS_MAGIC_INODE	"DFSI"
/* magic string identifying data block */
#define	DLFS_MAGIC_DB		"DFSD"
/* Unused data payload */
#define	DLFS_DATA_UNUSED	((unsigned long)-1)

/* types */
#ifndef BOOL
typedef enum _BOOL { FALSE = 0, TRUE } BOOL;
#endif

/* superblock - 512 bytes in total */
typedef struct {
	char		magic[4];	/* 4b - superblock identifier */
	unsigned long	version;	/* 4b - filesystem version */
	char		dummy[502];
	unsigned short	crc;		/* 2b - checksum */
} dlfs_sb_t;

/* filesystem inode - 512 bytes in total */
typedef struct {
	char 			magic[4];	/* 4b - inode identifier */
	unsigned long long 	ctime;		/* 8b - creation time in UTC */
	char			dummy[498];
	unsigned short	crc;			/* 2b - checksum */
} dlfs_inode_t;

/* file data block - 512 bytes in total */
typedef struct {
	char	magic[4];		/*   4b - dblock identifier */
	char	data[DLFS_MAX_DATA_LN];	/* 506b - data */
	unsigned short	crc;		/*   2b - checksum */
} dlfs_db_t;

/* structure holding state of open file */
typedef struct {
	unsigned long long	sec;	/* current sector number */
	unsigned short		pos;	/* current position within sector */

	/* sector buffer - can be interpreted in several ways */
	union {
		char			buf[DLFS_SECTOR_SIZE];
		unsigned long		buf_ul[DLFS_SECTOR_SIZE /
					    sizeof (unsigned long)];
		dlfs_inode_t		inode;
		dlfs_db_t		db;
		dlfs_sb_t		sb;
	} u;
} dlfs_file_t;

/* structure holding data block to be flushed to the media */
typedef struct {
	/*
	 * Setting 'dirty'to TRUE triggers flushing process.
	 * It is reset to false once sector is written to the media.
	 */
	BOOL			dirty;
	/* Setting to TRUE writes empty sector after data one as EOF mark. */
	BOOL			write_eof_mark;
	unsigned long long	sec;	/* sector number */

	/* sector buffer */
	char	buf[DLFS_SECTOR_SIZE];
} dlfs_flush_t;

/* macros */
/* Calculates theoretical maximum number of inodes for mounted media. */
#define	DLFS_GET_MAX_INODES	\
    ((MDD_ReadCapacity() - DLFS_1STINODE_SECTOR) / DLFS_SECS_PER_INODE)

/* public functions */
/* Initialization routine. */
extern int dlfs_init(void);

/* Formats media. */
extern int dlfs_format(void);

/* Opens file. */
extern int dlfs_open(const char *, int);

/* Writes data to the file. */
extern ssize_t dlfs_write(int, const void *, size_t);

/*
 * Returns TRUE if specified inode is in use. FALSE otherwise.
 */
extern BOOL dlfs_inode_in_use(int);

/*
 * Returns TRUE if specified inode is occupied by data block. FALSE otherwise.
 */
extern BOOL dlfs_inode_used_by_db(int);

/*
 * Returns TRUE if specified inode is free. FALSE otherwise.
 */
extern BOOL dlfs_inode_is_free(int);

/* Returns number of blocks occupied by an inode. */
extern unsigned long long dlfs_blocks_in_use(int);

/* Clear statistics. */
extern int dlfs_kstat_clear(void);

/* Displays IO statistics. */
extern void dlfs_display_kstat(int (*)(const char *));

/* Displays log media status. */
extern void dlfs_display_media_status(int (*)(const char *));

/*
 * Upper layer service that takes care of flushing current data block
 * to the media.
 */
extern void dlfs_flush_ul_svc(void);
#endif /* __DLFS_H */
