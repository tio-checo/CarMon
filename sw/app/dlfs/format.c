#include <sys/stat.h>
#include <sys/types.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>
#include <fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "dlfs.h"

/* local constants */
/*
 * 'empty' media sector - all bits set to 'ones'
 */
static const char dlfs_empty_sec[DLFS_SECTOR_SIZE] =
    {[0 ... DLFS_SECTOR_SIZE - 1] = 0xff};

/* local variables */
/* target device file descriptor */
static int fdi;

/* Gracefully aborts. */
static void
gracefully_abort(void)
{
	if (fdi != -1) {
		close(fdi);
		fdi = -1;
	}

	exit(EXIT_FAILURE);
}

/* Writes one sector. */
/* Obtains total number of sectors on target device. */
static diskaddr_t
MDD_ReadCapacity(void)
{
	struct dk_minfo mi;
	int ret;

	if ((ret = ioctl(fdi, DKIOCGMEDIAINFO, &mi)) != 0) {
		(void) fprintf(stderr,
			"Failed to obtain media capacity: err=%d\n", ret);

		gracefully_abort();
	}

	/* Only 512 sectors supported for now. */
	if (mi.dki_lbsize != DLFS_SECTOR_SIZE) {
		(void) fprintf(stderr,
			"Unsupported sector size: %d\n", mi.dki_lbsize);
		gracefully_abort();
	}

	printf("Media capacity: %lld sectors\n", mi.dki_capacity);

	return mi.dki_capacity;
}

/* Prints usage message. */
static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: dlfsformat -f file | device\n");
}

static void
write_sector(unsigned long long secaddr, const void *buff)
{
	ssize_t num;
	
	if (lseek(fdi, secaddr * DLFS_SECTOR_SIZE, SEEK_SET) == (off_t)-1) {
		perror("Failed to lseek the sector");
		gracefully_abort();
	}

	num = write(fdi, buff, DLFS_SECTOR_SIZE);

	if (num == -1) {
		perror("Write error");
		fprintf(stderr, "Failed to write sector %lld\n", secaddr);
		gracefully_abort();
	} else if (num < DLFS_SECTOR_SIZE) {
		(void) fprintf(stderr,
		    "Incomplete sector %lld written, aborting.\n", secaddr);
		gracefully_abort();
	}
}

/* global functions */
int
main(int argc, char *argv[])
{
	int c, i, inum;
	extern char *optarg;
	extern int optind, opterr, optopt;
	char *fpath = NULL;

	/* Invalidate file descriptor. */
	fdi = -1;

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
	
	if ((fdi = open(fpath, O_RDWR)) == -1) {
		perror("Failed to obtain read/write access to target device");
		exit(EXIT_FAILURE);
	}

	inum = DLFS_GET_MAX_INODES;

	printf("Formatting");
	fflush(stdout);

	for (i = 0; i < inum; i++) {
		unsigned long long inode_sec = DLFS_INODE2SEC(i);

		/* Erase inode slot and following data sector. */
		write_sector(inode_sec, (void *)dlfs_empty_sec);

		inode_sec++;

		write_sector(inode_sec, (void *)dlfs_empty_sec);

		/* Show progress */
		if ((i % 100) == 0) {
			printf(".");
			fflush(stdout);
		}
	}

	printf("done\n%d dlfs inodes created.\n", inum);
	printf("Target device is formatted and ready for use.\n");

	close(fdi);
	exit(EXIT_SUCCESS);
}
