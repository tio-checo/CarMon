#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "dlfs.h"

/* local constants */
/* look-up table for CRC calculation */
static const unsigned short crc_table[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

/* local variables */
/* file descriptors for processed and generated files */
static int fdi, fdo;

/* crc checksum */
static unsigned short crc;

/* local functions */
static BOOL block_is_empty(dlfs_file_t *);

/* Prints usage message. */
static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: dlfsdump -f file\n");
}

/* Gracefully aborts. */
static void
gracefully_abort(void)
{
	if (fdi != -1) {
		close(fdi);
		fdi = -1;
	}

	if (fdo != -1) {
		close(fdo);
		fdo = -1;
	}

	exit(EXIT_FAILURE);
}

/*
 * Initializes 16bit CRC-CCITT calculation algorithm
 */
void
crc_init(void)
{
	crc = 0xffff;
}

/*
 * Calculates 16bit CRC-CCITT
 */
void
crc_calc(unsigned char data)
{
	unsigned short i;

	i = (crc >> 12) ^ (data >> 4);
	crc = crc_table[i & 0x0F] ^ (crc << 4);
	i = (crc >> 12) ^ (data >> 0);
	crc = crc_table[i & 0x0F] ^ (crc << 4);
}

/*
 * Finishes calculation of CRC-CCITT and returns resulting value.
 */
unsigned short
crc_finalize(void)
{
	/*
	 * Per standard, empty messages should be appended
	 * with 16 zero bits. We do not handle that for now,
	 * as we do not deal with empty messages at all.
	 */
/*
	crc_calc(0);
	crc_calc(0);
*/
	return crc;
}

/* Reads one dlfs block. */
static void
read_block(dlfs_file_t *df)
{
	size_t num = read(fdi, df->u.buf, DLFS_SECTOR_SIZE);

	if (num == -1) {
		perror("Read error");
		gracefully_abort();
	} else if (!block_is_empty(df) && df->u.db.crc != 0xaa55) {
		/* Validate CRC. Empty blocks obviously do not contain it. */
		int i;

		crc_init();
		for (i = 0; i < DLFS_SECTOR_SIZE - DLFS_CRC_SIZE; i++) {
			crc_calc(df->u.buf[i]);
		}

		if (df->u.db.crc != crc_finalize()) {
			(void) fprintf(stderr, "CRC error, aborting.\n");
			gracefully_abort();
		}
	} else if (num < DLFS_SECTOR_SIZE) {
		(void) fprintf(stderr,
		    "Filesystem truncated, aborting.\n");
		gracefully_abort();
	}
}

/*
 * Returns TRUE if the block is empty; FALSE otherwise.
 */
static BOOL
block_is_empty(dlfs_file_t *df)
{
	return df->u.buf_ul[0] == (unsigned long)-1;
}

/*
 * Returns TRUE if it is inode; FALSE otherwise.
 */
static BOOL
block_is_inode(dlfs_file_t *df)
{
	return strncmp((char *)df->u.inode.magic,
	    DLFS_MAGIC_INODE, DLFS_MAGIC_SIZE) == 0;
}

/*
 * Returns TRUE if it is data block; FALSE otherwise.
 */
static BOOL
block_is_db(dlfs_file_t *df)
{
	return strncmp((char *)df->u.db.magic,
	    DLFS_MAGIC_DB, DLFS_MAGIC_SIZE) == 0;
}

/* global functions */
int
main(int argc, char *argv[])
{
	int c;
	extern char *optarg;
	extern int optind, opterr, optopt;
	char *fpath = NULL;
	char ofname[1024];
	BOOL done, is_db;
	int fnum;
	unsigned long long blk_num;
	dlfs_file_t dlfs_file;
	off_t pos;
	time_t inode_time;
	struct tm *inode_tm;
	/* Number of bytes per inode. */
	off_t bpi = DLFS_SECS_PER_INODE * DLFS_SECTOR_SIZE;

	/* Invalidate file descriptors. */
	fdi = fdo = -1;

	/* Process options and arguments. */
	while ((c = getopt(argc, argv, "f:")) != EOF) {
		switch(c) {
		case 'f':
			fpath = optarg;
			break;

		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}

	/* Path to file name required. */
	if (fpath == NULL) {
		usage();
		exit(EXIT_FAILURE);
	}
	
	if ((fdi = open(fpath, O_RDONLY)) == -1) {
		perror("Failed to open input file");
		exit(EXIT_FAILURE);
	}
	
	/* Move to the first sector used by the filesystem. */
	if (lseek(fdi, DLFS_1STINODE_SECTOR * DLFS_SECTOR_SIZE, SEEK_SET) ==
	    (off_t)-1) {
		perror("Failed to lseek the 1st inode");
		gracefully_abort();
	}

	/*
	 * Process file system inode by inode.
	 * Create separate file for each used inode.
	 */
	done = FALSE;
	fnum = 0;
	blk_num = 0;
	while (!done) {
		/* Read dlfs block. */
		read_block(&dlfs_file);

		/*
		 * It can be either inode or empty slot.
		 * If it is empty slot, then we are done.
		 */
		if (block_is_empty(&dlfs_file)) {
			done = TRUE;
			continue;
		} else if (!block_is_inode(&dlfs_file)) {
			(void) fprintf(stderr,
			    "Filesystem corrupted, invalid block %llu.\n",
			    blk_num);
			gracefully_abort();
		}

		/*
		 * Obtain time when log record was created from inode structure
		 * and use that to construct name of output file.
		 */
		inode_time = dlfs_file.u.inode.ctime;
		inode_tm = gmtime(&inode_time);

		/* Valid inode found, dump associated log record. */
		fnum++;
		snprintf(ofname, sizeof (ofname),
		    "obd%03d-%04d%02d%02d-%02d%02d%02d.csv",
		    fnum, inode_tm->tm_year + 1900, inode_tm->tm_mon + 1,
		    inode_tm->tm_mday, inode_tm->tm_hour, inode_tm->tm_min,
		    inode_tm->tm_sec);

		printf("Dumping file %s... ", ofname);

		/* Create output file. */
		if ((fdo = open(ofname, O_CREAT | O_TRUNC | O_WRONLY, 0644)) ==
		    -1) {
			perror("failed to create output file");
			gracefully_abort();
		}

		is_db = TRUE;
		while (is_db) {
			/* Read dlfs block. */
			read_block(&dlfs_file);

			/*
			 * Can be either empty block, data block
			 * or next inode.
			 */
			/*
			 * Empty block - close the output file and move
			 * to the next inode.
			 */
			if (block_is_empty(&dlfs_file)) {
				printf("empty block detected, done.\n");

				is_db = FALSE;
			} else if (block_is_db(&dlfs_file)) {
				int	i;

				/*
				 * Write only portion of block containing valid
				 * data, since block may not have been
				 * completely written due to power outage.
				 */
				/* Identify part of block carrying data. */
				for (i = DLFS_MAX_DATA_LN - 1; i >= 0; i--) {
					if (dlfs_file.u.db.data[i] !=
					    (char)-1) {
						break;
					}
				}

				/* Write data to the file. */
				if (write(fdo, dlfs_file.u.db.data,
				    i + 1) == -1) {
					perror("failed to write data");
					gracefully_abort();
				}

				/*
				 * Only last data block in log record could
				 * be incomplete.
				 * Move to the next inode in such case.
				 */
				if (i < DLFS_MAX_DATA_LN - 1) {
					printf("incomplete data block"
					    " detected, done.\n");

					is_db = FALSE;
				}
			} else if (block_is_inode(&dlfs_file)) {
				/* We reached next log record, handle that. */
				printf("inode immediately following data block"
				    " detected, done.\n");

				is_db = FALSE;

				/* "Unprocess" the block carrying next inode. */
				pos = lseek(fdi, -DLFS_SECTOR_SIZE, SEEK_CUR);
				if (pos == -1) {
					perror("failed to seek previous block");
					gracefully_abort();
				}
			}
		} /* while */

		/* Close the output file. */
		close(fdo);
		fdo = -1;

		/*
		 * Move to the block which is supposed
		 * to contain next inode.
		 */
		/* Get current position first. */
		pos = lseek(fdi, 0, SEEK_CUR);
		if (pos == -1) {
			perror("Failed to lseek current pos");
			gracefully_abort();
		}

		/* Now move to the next inode slot. */
		pos = lseek (fdi, ((pos + bpi - DLFS_SECTOR_SIZE) / bpi) * bpi,
		    SEEK_SET);
		if (pos == -1 ) {
			perror("Failed to lseek next inode");
			gracefully_abort();
		}
	}

	printf("Fileystem processed.\n");

	close(fdi);
	exit(EXIT_SUCCESS);
}
