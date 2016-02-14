/*********************************************************************
 *
 * Implements dambi log-structured filesystem (DLFS).
 *
 * o Design
 *
 *   DLFS has been designed as a filesystem serving one particular
 *   purpose - to log various kind of data collected in car to
 *   block backend storage - currently represented by SD card.
 *
 *   The logging process is unattended and may run for a long time
 *   (months) before collected data are transferred from logging
 *   device to the main server.
 *   That means the logging mechanism (and thus also underlying
 *   filesystem) must be reliable and robust, so that risk of data
 *   loss is minimized. That among other things includes capability
 *   to deal with regular unplanned power outages - occurring
 *   when car ignition is turned off after engine is stopped.
 *
 *   To address that requirement (and as its name indicates)
 *   DLFS is designed as log-structured filesystem. That means
 *   data as well as metadata are written sequentially to backend
 *   storage, eliminating read-modify-write operations which
 *   (if interrupted due to power outage) may cause data or metadata
 *   corruption.
 *
 * o DLFS entities
 *
 *   superblock - Contains information about filesystem (e.g. version).
 *                Occupies one block device.
 *   inode      - Contains information about file (one continuos log
 *                record). Created each time new log record is started.
 *                Occupies one block on device.
 *   data block - Contains logged data.
 *                Occupies one block on device.
 *
 * o Implementation details
 *
 *   - dlfs_* functions which are exchanging data with backend media
 *     work in blocking mode. Thus, they should not be called from
 *     interrupt context.
 *   - In order to coexist with concept of fdisk partitions,
 *     1st MB of the device is unused and not being touched.
 *   - Each block on device which is in-use by DLFS entity
 *     starts with 'magic word'. 'Magic word' identifies particular
 *     DLFS entity.
 *   - DLFS entities are appended with CRC checksum which is used
 *     to validate the entity.
 *   - Log record always starts at 1MB boundary - this is where
 *     inode is put, subsequently followed by data blocks.
 *     That speeds-up process of locating free slot when new log
 *     record is to be created as well as later simplifies process
 *     of assembling list of collected log records (which is equivalent
 *     of reading directory content).
 *
 *********************************************************************/

#include "HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "xc.h"

#include "dlfs.h"
#include "helpers.h"
#include "pic_core.h"
#include "utils.h"
#include "SD-SPI.h"

#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <stddef.h>
#include <string.h>

/* constants */

/* macros */

/* Associates physical layer functions with the SD card physical layer. */
#ifdef USE_SD_INTERFACE_WITH_SPI
#define MDD_MediaInitialize     MDD_SDSPI_MediaInitialize
#define MDD_MediaDetect         MDD_SDSPI_MediaDetect
#define MDD_SectorRead          MDD_SDSPI_SectorRead
#define MDD_SectorWrite         MDD_SDSPI_SectorWrite
#define MDD_InitIO              MDD_SDSPI_InitIO
#define MDD_ShutdownMedia       MDD_SDSPI_ShutdownMedia
#define MDD_WriteProtectState   MDD_SDSPI_WriteProtectState
#define	MDD_ReadCapacity	MDD_SDSPI_ReadCapacity
#else
#error "MDD_() functions not associated with any physical device."
#endif

/* Calculates inode position in sectors. inodes are numbered from 0. */
#define	DLFS_INODE2SEC(inode)	\
    ((inode) * DLFS_SECS_PER_INODE + DLFS_1STINODE_SECTOR)

/* types */

/* local variables */
/* Statistics */
static struct dlfs_kstat_t {
	long		srn;		/* total number of sectors read */
	struct timeval	srt_min;	/* min sector read time */
	struct timeval	srt_max;	/* max sector read time */
	long		rsen;		/* read sector error counter */

	long		swn;		/* total number of sectors written */
	struct timeval	swt_min;	/* min sector write time */
	struct timeval	swt_max;	/* max sector write time */
	long		wsen;		/* write sector error counter */

	long		fln;		/* total number of dlfs_flush calls */
	long		fen;		/* number of failed dlfs_flush calls */

	long		ocn;		/* total numner of dlfs_open calls */
	long		oen;		/* total number of open errors */
	long		wcn;		/* total number of dlfs_write calls */
} dlfs_kstat;

/*
 * 'empty' media sector - all bits set to 'ones'
 * 'const' qualifier guarantees this structure resides in ROM
 */
static const char dlfs_empty_sec[DLFS_SECTOR_SIZE] =
    {[0 ... DLFS_SECTOR_SIZE - 1] = 0xff};

/* TRUE if log device present */
static BOOL dlfs_media_ready;

/* TRUE if log device mounted */
static BOOL dlfs_media_mounted;

/* array of file descriptors */
static dlfs_file_t	dlfs_files[1];

/*
 * Index of buffer cache lower currently being filled
 * by dlfs_write() calls.
 */
static int	dlfs_ll_write_buf_num;
/*
 * Index of buffer cache currently monitored by upper layer
 * service (taking care of flushing buffers to the media).
 */
static int	dlfs_ul_flush_buf_num;

/* buffer cache - array of dlfs_cache_buf_entry_t structures */
static dlfs_buf_cache_entry_t dlfs_buf_cache[DLFS_BUF_CACHE_SIZE];

/* local functions */

/*
 * Populates DLFS magic word.
 */
static void
dlfs_populate_magic(char target[DLFS_MAGIC_SIZE], const char *magic)
{
	int	i;

	for (i = 0; i < DLFS_MAGIC_SIZE; i++) {
		target[i] = magic[i];
	}
}

/*
 * Validates CRC checksum.
 *
 * Returns: TRUE if CRC is valid, FALSE otherwise.
 */
static BOOL
dlfs_crc_valid(void *d)
{
	return TRUE;
}

/*
 * Calculates 16bit CRC-CCITT for given sector and writes result
 * to the last two bytes.
 */
static void
dlfs_calc_crc(unsigned char *buff)
{
	unsigned short crc;

	crc = crc_calc_array(buff, DLFS_SECTOR_SIZE - DLFS_CRC_SIZE);

	/* LSB first */
	buff[DLFS_SECTOR_SIZE - 2] = crc & 0xff;
	buff[DLFS_SECTOR_SIZE - 1] = crc >> 8;
}

/*
 * Writes sector to media.
 *
 * Returns: 0      - sector was successfully written
 *          ENODEV - media not available
 *          EIO    - write operation failed
 */
static int
dlfs_write_sector(unsigned long long secaddr, void *buff, BOOL calc_crc)
{
	struct timeval ct, dt;

	/* Media not available, do not proceed. */
	if (!dlfs_media_ready) {
		return ENODEV;
	}

	/* Calculate CRC */
	if (calc_crc) {
		dlfs_calc_crc(buff);
	}

	/* Capture current time for purposes of measuring statistics. */
	get_curr_hrtime(&ct);

	/* Do not allow writing to MBR. */
	if (!MDD_SectorWrite(secaddr, buff, FALSE)) {
		/* Operation failed, mark backend storage as not available. */
		dlfs_media_mounted = FALSE;
		dlfs_media_ready = FALSE;

		/* Update statistics */
		dlfs_kstat.wsen++;

		return EIO;
	}

	/* Update statistics */
	dlfs_kstat.swn++;

	/* Calculate time difference. */
	get_diff_hrtime(&ct, &dt);

	/* Update min time if appropriate */
	if (cmp_hrtime(&dt, &dlfs_kstat.swt_min) == -1) {
		dlfs_kstat.swt_min = dt;
	}

	/* Update max time if appropriate */
	if (cmp_hrtime(&dt, &dlfs_kstat.swt_max) == 1) {
		dlfs_kstat.swt_max = dt;
	}

	return 0;
}

/*
 * Reads sector from media.
 *
 * Returns: 0      - success
 *          ENODEV - media not available
 *          EIO    - read operation failed
 */
static int
dlfs_read_sector(unsigned long long secaddr, dlfs_file_t *df)
{
	struct timeval ct, dt;

	/* Media not available, do not proceed. */
	if (!dlfs_media_ready) {
		return ENODEV;
	}

	/* Sector already cached in buffer, no need to read it again. */
	if (df->sec == secaddr) {
		return 0;
	}

	/* Capture current time for purposes of measuring statistics. */
	get_curr_hrtime(&ct);

	if (!MDD_SectorRead(secaddr, (BYTE *)(df->sbufp->buf))) {
		/* Operation failed, mark backend storage as not available. */
		dlfs_media_mounted = FALSE;
		dlfs_media_ready = FALSE;

		/* Update statistics */
		dlfs_kstat.rsen++;

		return EIO;
	}

	/* Sector successfully read to buffer, update its descriptor. */
	df->sec = secaddr;
	df->pos = 0;

	/* Update statistics */
	dlfs_kstat.srn++;

	/* Calculate time difference. */
	get_diff_hrtime(&ct, &dt);

	/* Update min time if appropriate */
	if (cmp_hrtime(&dt, &dlfs_kstat.srt_min) == -1) {
		dlfs_kstat.srt_min = dt;
	}

	/* Update max time if appropriate */
	if (cmp_hrtime(&dt, &dlfs_kstat.srt_max) == 1) {
		dlfs_kstat.srt_max = dt;
	}

	return 0;
}

/*
 * Cleans up data block data buffer.
 */
static void
dlfs_clean_db_data(dlfs_db_t *db)
{
	memset(db->data, 0xff, sizeof (db->data));
}

/*
 * Mounts media.
 *
 * Returns: 0      - success
 *          ENODEV - media not available
 *          EINVAL - the super block has an invalid magic number
 */
static int
dlfs_mount(void)
{
	int fd = 0;

	/* Media not available, do not proceed. */
	if (!dlfs_media_ready) {
		return ENODEV;
	}

	/* Load super-block. */
	if (dlfs_read_sector(DLFS_SB_SECTOR, &dlfs_files[fd]) != 0) {
		return ENODEV;
	}

	/* Validate super-block - magic number and CRC. */
/* TODO
	if (strncmp(dlfs_sb.magic, DLFS_MAGIC_SB, DLFS_MAGIC_SIZE) != 0) {
		return EINVAL;
	}
*/
	if (!dlfs_crc_valid(&dlfs_files[fd].sbufp->sb)) {
		return EINVAL;
	} 

	/* Media mounted - there is a valid dlfs filesystem. */
	dlfs_media_mounted = TRUE;

	return 0;
}

/*
 * Returns TRUE if specified block is occupied by data block;
 * FALSE otherwise.
 */
static BOOL
dlfs_block_is_db(unsigned long long sec)
{
	int fd = 0;

	if (dlfs_read_sector(sec, &dlfs_files[fd]) != 0) {
		return FALSE;
	}

	return strncmp((char *)dlfs_files[fd].sbufp->db.magic,
	    DLFS_MAGIC_DB, DLFS_MAGIC_SIZE) == 0;
}

/*
 * Returns number of the first free inode. -1 if all inodes are occupied.
 */
static int
dlfs_find_first_free_inode(void)
{
	int l, m, h;
	int fd;

	/*
	 * Handle corner cases when all or none inodes are available.
	 */
	if (dlfs_inode_is_free(0)) {
		return 0;
	}

	if (!dlfs_inode_is_free(DLFS_GET_MAX_INODES - 1)) {
		return -1;
	}

	/*
	 * Now we know that the first inode is occupied
	 * AND the last inode is available.
         * 
	 * Low marker points to the first inode
	 * High marker points to the last inode
	 * Middle marker points to the middle inode
	 */
	l = 0;
	h = DLFS_GET_MAX_INODES - 1;
	m = (l + h) / 2;

	/*
	 * Binary search for the free inode slot.
	 * Search until high marker points to the free inode
	 * which immediately follows the last used one.
	 */
	while (h - l > 1) {
		if (dlfs_inode_is_free(m)) {
			h = m;
		} else {
			l = m;
		}

		m = (l + h) / 2;
	}

	/*
	 * Low marker is now supposed to point to the last occupied inode
	 * and high marker to the first free inode. If that assumption
	 * is not met, then something went wrong.
	 */
	if (h - l != 1) {
		return -1;
	}

	/* Reuse the last used inode if it describes empty log. */
	if (dlfs_read_sector(DLFS_INODE2SEC(l) + 1, &dlfs_files[fd]) != 0) {
		return -1;
	}

	if (dlfs_files[fd].sbufp->buf_ul[0] == DLFS_DATA_UNUSED) {
		return l;
	}

	/*
	 * Last used inode describes non-empty log,
	 * so return the first free inode.
	 */
	return h;
}

/*
 * Commence process of flushing current data block to the media.
 * Returns: 0     - success
 *          EBUSY - run out of free buffers
 */
static int
dlfs_flush(unsigned long long sec, BOOL write_eof_mark, BOOL update_inode_time)
{
	int fd = 0;
	int cbi = dlfs_ll_write_buf_num;
	int nbi = (cbi + 1) % DLFS_BUF_CACHE_SIZE;

	/* Update statistics. */
	dlfs_kstat.fln++;

	/*
	 * Return EBUSY if subsequent buffer is not available.
	 * That means we ran out of free buffers.
	 */
	if (dlfs_buf_cache[nbi].dirty) {
		/* Update statistics. */
		dlfs_kstat.fen++;

		return EBUSY;
	}

	/* Move to the next free sector buffer. */
	dlfs_files[fd].sbufp = &dlfs_buf_cache[nbi].sbuf;
	dlfs_ll_write_buf_num = nbi;

	dlfs_buf_cache[cbi].sec = sec;
	dlfs_buf_cache[cbi].write_eof_mark = write_eof_mark;
	dlfs_buf_cache[cbi].update_inode_time = update_inode_time;

	/* Mark current buffer 'dirty', i.e. to be flushed. */
	dlfs_buf_cache[cbi].dirty = TRUE;

	return 0; 
}

/* global functions */

/*
 * Initialization routine.
 *
 * Returns: 0      - success
 *          ENODEV - storage not available
 *          EINVAL - mount failed
 */
int
dlfs_init(void)
{
	int fd = 0;
	int i;

	/* Assume backend storage is not available. */
	dlfs_media_ready = FALSE;
	dlfs_media_mounted = FALSE;

	/* Initialize file descriptor. */
	dlfs_files[fd].sec = 0;
	dlfs_files[fd].pos = 0;
	/*
	 * Initialize buffer cache framework.
	 */
	dlfs_ll_write_buf_num = 0;
	dlfs_ul_flush_buf_num = 0;
	for (i = 0; i < DLFS_BUF_CACHE_SIZE; i++) {
		dlfs_buf_cache[i].dirty = FALSE;
	}
	/* Pointer to buffer cache */
	dlfs_files[fd].sbufp = &dlfs_buf_cache[0].sbuf;

	/* Initialize structure for measuring IO statistics. */
	dlfs_kstat_clear();

	/* Initializes I/O lines connected to the media. */
	MDD_InitIO();

	/*
	 * Determine if backend media is available.
	 * MDD_MediaInitialize() called as a side effect - among other things,
	 * obtains information about media size.
	 */
	if (!MDD_MediaDetect()) {
		return ENODEV;
	}

	/* Mark backend storage as available. */
	dlfs_media_ready = TRUE;
	
	/*
	 * Make an attempt to "mount" media.
	 * As a side effect, sets dlfs_media_mounted.
	 */
	if (dlfs_mount() != 0) {
		return EINVAL;
	}

	return 0;
}

#if 0
/*
 * Formats media.
 *
 * Returns: 0      - success
 *          ENODEV - storage not available
 */
int
dlfs_format(void)
{
	int i;

	/* Media not available, do not proceed. */
	if (!dlfs_media_ready) {
		return ENODEV;
	}

	for (i = 0; i < DLFS_GET_MAX_INODES; i++) {
		unsigned long long inode_sec = DLFS_INODE2SEC(i);

		/* Erase inode slot and following data sector. */
		if (dlfs_write_sector(inode_sec, (void *)dlfs_empty_sec,
		    FALSE) != 0) {
			return EIO;
		}

		if (dlfs_write_sector(inode_sec + 1, (void *)dlfs_empty_sec,
		    FALSE) != 0) {
			return EIO;
		}
	}

	return 0;
}
#endif

/*
 * Opens file.
 *
 * Returns: 0      - success
 *          ENODEV - storage not available
 *          ENOSPC - media full
 *          EIO    - error encountered when reading/writing media
 */
int
dlfs_open(const char *path, int oflag)
{
	int inode;
	int fd = 0;
	unsigned long long inode_sec;

	/* Update statistics */
	dlfs_kstat.ocn++;

	/* Media not available, do not proceed. */
	if (!dlfs_media_ready) {
		/*dlfs_kstat.oen++;*/
		return ENODEV;
	}
	
	inode = dlfs_find_first_free_inode();

	/* Out of space, return. */
	if (inode == -1) {
		dlfs_kstat.oen++;
		return ENOSPC;
	}

	inode_sec = DLFS_INODE2SEC(inode);

	/* Initialize file descriptor. */
	dlfs_files[fd].sec = inode_sec + 1;
	dlfs_files[fd].pos = 0;

	/* Clean up data buffer. */
	dlfs_clean_db_data(&dlfs_files[fd].sbufp->db);

	/* Create new 'inode' and write it to the media. */
	/* Populate magic word. */
	dlfs_populate_magic(dlfs_files[fd].sbufp->inode.magic,
	    DLFS_MAGIC_INODE);

	/*
	 * Flush the inode sector to the media.
	 * Write EOF mark.
	 * Update inode time.
	 */
	return dlfs_flush(inode_sec, TRUE, TRUE);
}

/*
 * Writes data to the file.
 * It is non-blocking function, so can be called from interrupt
 * context. Sectors are flushed to the media by upper layer
 * called from main application.
 *
 * Returns: number of bytes successfully written.
 */
ssize_t
dlfs_write(int fildes, const void *buf, size_t nbyte)
{
	int fd = 0;
	int pos = dlfs_files[fd].pos;
	size_t written = 0;

	/* Update statistics */
	dlfs_kstat.wcn++;

	/* Media not available, do not proceed. */
	if (!dlfs_media_ready) {
		return 0;
	}

	/*
	 * We do not currently support records which
	 * do not fit into one sector.
	 */
	if (nbyte > DLFS_MAX_DATA_LN) {
		return 0;
	}

	/*
	 * Check if there is enough space in currenty processed sector.
	 * If not, then write as much bytes as possible to the current sector,
	 * flush it to the media and write the remaining portion of log record
	 * to the new sector.
	 */
	if (pos + nbyte > DLFS_MAX_DATA_LN) {
		/* Populate magic word. */
		dlfs_populate_magic(dlfs_files[fd].sbufp->db.magic,
		    DLFS_MAGIC_DB);

		/*
		 * Write as much as possible of the log record to the
 		 * current sector.
		 */
		written = DLFS_MAX_DATA_LN - pos;

		if (written > 0) {
			nbyte -= written;
			memcpy(dlfs_files[fd].sbufp->db.data + pos, buf,
			    written);
		}

		/*
		 * Flush the current sector to the media.
		 * Write 'EOF mark' sector as well.
		 * Do not update inode time.
		 * If flush operation fails, then do not write anything.
		 */
		if (dlfs_flush(dlfs_files[fd].sec, TRUE, FALSE) != 0) {
			return 0;
		}

		/* Start populating the next sector. */
		dlfs_files[fd].sec++;
		pos = dlfs_files[fd].pos = 0;
	}

	/* Copy rest of the data to the buffer. */
	memcpy(dlfs_files[fd].sbufp->db.data + pos, buf + written, nbyte);
	written += nbyte;

	/* Update index. */
	dlfs_files[fd].pos += nbyte;

	/*
	 * Write 'end of record' mark after the last written character.
	 * But only if there is a space for it.
	 */
	if (dlfs_files[fd].pos < DLFS_MAX_DATA_LN) {
		dlfs_files[fd].sbufp->db.data[dlfs_files[fd].pos] = 0xff;
	}
	
	return written;
}

/*
 * Upper layer service that takes care of flushing current data block
 * to the media.
 */
void
dlfs_flush_ul_svc(void)
{
	int cbi;

	/* Wait until core services are initialized. */
	if (!pcore_svc_online()) {
		return;
	}

	cbi = dlfs_ul_flush_buf_num;

	/*
	 * Nothing to be done.
	 * Atomic operation, since 'dirty' flag is set from interrupt context.
	 */
	if (!dlfs_buf_cache[cbi].dirty) {
		return;
	}
	
	/* Write EOF mark if required. */
	if (dlfs_buf_cache[cbi].write_eof_mark) {
		dlfs_write_sector(dlfs_buf_cache[cbi].sec + 1,
		    (void *)dlfs_empty_sec, FALSE);
	}

	/* Update inode time if desired. */
	if (dlfs_buf_cache[cbi].update_inode_time) {
		dlfs_buf_cache[cbi].sbuf.inode.ctime =
		    (unsigned long long)time(NULL);
	}

	/* Now write data. */
	dlfs_write_sector(dlfs_buf_cache[cbi].sec, dlfs_buf_cache[cbi].sbuf.buf,
	    TRUE);

	/* Monitor next cache entry. */
	dlfs_ul_flush_buf_num = (cbi + 1) % DLFS_BUF_CACHE_SIZE;

	/*
	 * Buffer flushed to the media, mark it as 'available'.
	 * Atomic operation, since 'dirty' flag is set from interrupt context.
	 */
	dlfs_buf_cache[cbi].dirty = FALSE;
}

/*
 * Returns TRUE if specified inode slot is indeed occupied by inode
 * (it could be also used by data block); FALSE otherwise.
 */
BOOL
dlfs_inode_in_use(int in)
{
	int fd = 0;

	/* Read sector which can hold an inode. */
	if (dlfs_read_sector(DLFS_INODE2SEC(in), &dlfs_files[fd]) != 0) {
		return FALSE;
	}

	return strncmp((char *)dlfs_files[fd].sbufp->inode.magic,
	    DLFS_MAGIC_INODE, DLFS_MAGIC_SIZE) == 0;
}

/*
 * Returns TRUE if specified inode is occupied by data block. FALSE otherwise.
 */
BOOL
dlfs_inode_used_by_db(int in)
{
	return dlfs_block_is_db(DLFS_INODE2SEC(in));
}

/*
 * Returns TRUE if specified inode is free. FALSE otherwise.
 */
BOOL
dlfs_inode_is_free(int in)
{
	int fd = 0;

	if (dlfs_read_sector(DLFS_INODE2SEC(in), &dlfs_files[fd]) != 0) {
		return FALSE;
	}

	return dlfs_files[fd].sbufp->buf_ul[0] == DLFS_DATA_UNUSED;
}

/*
 * Returns number of sectors occupied by an inode.
 */
unsigned long long
dlfs_blocks_in_use(int in)
{
	unsigned long long in_use, l, i;
	BOOL is_db;
	int curr_inode;

	in_use = 0;

	/*
	 * First find number of subsequent inode slots which are
	 * occupied by data blocks.
	 */
	for (curr_inode = in + 1, is_db = TRUE; is_db; curr_inode++) {
		if ((is_db = dlfs_inode_used_by_db(curr_inode))) {
			in_use += DLFS_SECS_PER_INODE;
			/*
			 * The first block of the first inode slot
			 * is occupied by inode itself, subtract it.
			 */
			if (curr_inode == in + 1) {
				in_use--;
			}
		}
	}

	/* Now calculate number of data blocks occupied by last inode slot. */
	curr_inode -= 2;

	/*
	 * We can't use binary search, since the inode slot may contain
	 * data blocks from deleted log record, so we basically
	 * have to sequentially search for EOF mark.
	 */
	l = DLFS_INODE2SEC(curr_inode);
	/*
	 * The first block of the first inode slot is occupied by inode record,
	 * skip it.
	 */
	for (is_db = TRUE, i = curr_inode == in ? 1 : 0;
	    is_db && (i < DLFS_SECS_PER_INODE); i++) {
		if (dlfs_block_is_db(l + i)) {
			in_use++;
		} else {
			is_db = FALSE;
		}
	}

	return in_use;
}

/*
 * Clear statistics.
 */
int
dlfs_kstat_clear(void)
{
	dlfs_kstat.srn = dlfs_kstat.swn = 0;
	dlfs_kstat.rsen = dlfs_kstat.wsen = 0;

	dlfs_kstat.srt_min.tv_sec = dlfs_kstat.swt_min.tv_sec = 1000;
	dlfs_kstat.srt_min.tv_usec = dlfs_kstat.swt_min.tv_usec = 0;
	dlfs_kstat.srt_max.tv_sec = dlfs_kstat.swt_max.tv_sec = 0;
	dlfs_kstat.srt_max.tv_usec = dlfs_kstat.swt_max.tv_usec = 0;

	dlfs_kstat.fln = dlfs_kstat.fen = 0;
	dlfs_kstat.wcn = 0;

	return 0;
}

/*
 * Displays status of log device.
 */
void
dlfs_display_media_status(int (*print_str)(const char *))
{
	char s[81];

	/* header */
	print_str("-------------------------------------\n\r");

	/* Media status. */
	snprintf(s, sizeof (s),
	    "Media: %s\n\r", dlfs_media_ready ? "present" : "not present");
	print_str(s);

	/* Status of dlfs filesystem. */
	snprintf(s, sizeof (s),
	    "dlfs: %s\n\r", dlfs_media_mounted ? "mounted" : "not mounted");
	print_str(s);

	/* footer */
	print_str("-------------------------------------\n\r");
}

/*
 * Displays IO statistics.
 */
void
dlfs_display_kstat(int (*print_str)(const char *))
{
	char s[81];
	long fep;

	/* header */
	print_str("-------------------------------------\n\r");

	/* Read statistics. */
	snprintf(s, sizeof (s), "sectors read: %ld\n\r", dlfs_kstat.srn);
	print_str(s);

	snprintf(s, sizeof (s), "read errors: %ld\n\r", dlfs_kstat.rsen);
	print_str(s);

	snprintf(s, sizeof (s), "ior min time: %ld.%03ld[ms]\n\r",
	    dlfs_kstat.srt_min.tv_sec * 1000 +
	    dlfs_kstat.srt_min.tv_usec / 1000,
	    dlfs_kstat.srt_min.tv_usec % 1000);
	print_str(s);

	snprintf(s, sizeof (s), "ior max time: %ld.%03ld[ms]\n\r\n\r",
	    dlfs_kstat.srt_max.tv_sec * 1000 +
	    dlfs_kstat.srt_max.tv_usec / 1000,
	    dlfs_kstat.srt_max.tv_usec % 1000);
	print_str(s);

	/* Write statistics. */
	snprintf(s, sizeof (s), "flush calls: %ld\n\r", dlfs_kstat.fln);
	print_str(s);

	if (dlfs_kstat.fln != 0) {
		fep = (1000L * dlfs_kstat.fen) / dlfs_kstat.fln;
	} else {
		fep = 0;
	}
	snprintf(s, sizeof (s), "flush errors: %ld (%ld.%ld%%)\n\r",
	    dlfs_kstat.fen, fep / 10L, fep % 10L);
	print_str(s);
	
	snprintf(s, sizeof (s), "open calls: %ld\n\r", dlfs_kstat.ocn);
	print_str(s);
	snprintf(s, sizeof (s), "open errors: %ld\n\r", dlfs_kstat.oen);
	print_str(s);

	snprintf(s, sizeof (s), "write calls: %ld\n\r", dlfs_kstat.wcn);
	print_str(s);
	
	snprintf(s, sizeof (s), "sectors written: %ld\n\r", dlfs_kstat.swn);
	print_str(s);

	snprintf(s, sizeof (s), "write errors: %ld\n\r", dlfs_kstat.wsen);
	print_str(s);

	snprintf(s, sizeof (s), "iow min time: %ld.%03ld[ms]\n\r",
	    dlfs_kstat.swt_min.tv_sec * 1000 +
	    dlfs_kstat.swt_min.tv_usec / 1000,
	    dlfs_kstat.swt_min.tv_usec % 1000);
	print_str(s);

	snprintf(s, sizeof (s), "iow max time: %ld.%03ld[ms]\n\r",
	    dlfs_kstat.swt_max.tv_sec * 1000 +
	    dlfs_kstat.swt_max.tv_usec / 1000,
	    dlfs_kstat.swt_max.tv_usec % 1000);
	print_str(s);

	/* footer */
	print_str("-------------------------------------\n\r");
}
