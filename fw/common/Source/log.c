/*********************************************************************
 *
 * Logging service.
 *
 *********************************************************************/

#include "HardwareProfile.h"
#include "xc.h"

#include "GenericTypeDefs.h"
#include "log.h"
#include "SD-SPI.h"
#include "FSIO.h"

#include <errno.h>
#include <strings.h>

/* constants */
/* max length of logged message including null terminating byte */
#define LOG_MSG_MAXLN	128

/* Log file name */
#define LOG_FILENAME	"rfid.log"

/* private types */

/* private variables */
/* TRUE if log device is available */
static BOOL log_device_ready;

/* buffer holding log event */
static char log_msg_buf[LOG_MSG_MAXLN];

/* buffer holding read record */
static char log_rec_buf[LOG_MSG_MAXLN];

/*
 * Pointer to the buffer holding read record. Set to NULL if all records
 * have been read.
 */
static char *log_read_rec_pointer;

/* Size of the log record */
static int log_rec_size;

/* If TRUE, log is busy processing given command */
static BOOL log_cmd_run;

/*
 * TRUE when record has been read from log device
 * and is available to the consumer.
 */
static BOOL log_record_read;

/* File handle used when upper layer is reading records from the log file. */
static FSFILE *read_fh;

/* Logging command */
static enum {
	LOG_CMD_NOOP = 0,	/* No operation currently in progress. */
	LOG_CMD_POST,		/* write log message to the log device */
	LOG_CMD_CLEAR,		/* delete logged records */
	LOG_CMD_GET_FIRST,	/* read the first log record from log device */
	LOG_CMD_GET_NEXT	/* read next log record from log device */
} log_cmd;

/* private functions */
/*
 * Service taking care of dispatching message buffer to the log device.
 */
static int
log_post_svc(void)
{
	FSFILE *fh;
	int ln;

	fh = FSfopen(LOG_FILENAME, "a");
	if (fh == NULL) {
		return ENODEV;
	}

	ln = strlen(log_msg_buf);
	if (FSfwrite(log_msg_buf, 1, ln, fh) != ln) {
		return ENODEV;
	}

	if (FSfclose(fh) != 0) {
		return ENODEV;
	}

	return 0;
}

/*
 * Service taking care of wiping out log device.
 */
static int
log_clean_svc(void)
{
	FSFILE *fh;

	fh = FSfopen(LOG_FILENAME, "w");
	if (fh == NULL) {
		return ENODEV;
	}

	if (FSfclose(fh) != 0) {
		return ENODEV;
	}

	return 0;
}

/*
 * Service taking care of reading records from log device.
 */
static int
log_read_svc(int cmd)
{
	int err = 0;
	int ln;

	/* If first record is to be read, open the log device. */
	if (cmd == LOG_CMD_GET_FIRST) {
		/* File already open, just set file pointer to the beginning. */
		if (read_fh != NULL) {
			if (FSfseek(read_fh, 0, SEEK_SET) != 0) {
				return FSerror();
			}
		} else {
			read_fh = FSfopen(LOG_FILENAME, "r");
			if (read_fh == NULL) {
				return FSerror();
			}
		}
	}

	/* File handle not initialized, signal EOF. */
	if (read_fh == NULL) {
		log_read_rec_pointer = NULL;
		log_record_read = TRUE;

		return 0;
	}

	/* Read the record. */
	ln = FSfread(log_rec_buf, 1, log_rec_size, read_fh);

	/* Make sure, it's null terminated. */
	log_rec_buf[ln] = '\0';

	err = FSerror();

	/* End of file reached. */
	if (err == CE_EOF) {
		/* Close the file. */
		err = FSfclose(read_fh);
		if (err == 0) {
			read_fh = NULL;
		} else {
			err = FSerror();
		}

		log_read_rec_pointer = NULL;
		log_record_read = TRUE;

		return err;
	}

	/* Error occurred during reading process. */
	if (err != CE_GOOD) {
		return err;
	}

	/* Record successfully read, pass it to the consumer. */
	log_read_rec_pointer = log_rec_buf;
	log_record_read = TRUE;

	return 0;
}

/* public functions */
/*
 * Initialize logging service.
 *
 * Returns: ENODEV - log device not present
 */
int
log_init(void)
{
	FSFILE *fh;

	/* Assume that log device is not present. */
	log_device_ready = FALSE;

	/* Logging service is idle. */
	log_cmd = LOG_CMD_NOOP;
	log_cmd_run = FALSE;
	log_record_read = FALSE;
	log_read_rec_pointer = NULL;
	log_rec_size = 0;

	/* Attach chip select signal to SD card. */
	SD_CS_TRIS = 0;

	/* If log device (SD card) not present, return. */
	if (!MDD_SDSPI_MediaDetect()) {
		return ENODEV;
	}

	/* Initialize SD card and filesystem API */
	if (!FSInit()) {
		return ENODEV;
	}

	/* Create log file if it does not exist and verify it can be written. */
	fh = FSfopen(LOG_FILENAME, "a");
	if (fh == NULL) {
		return ENODEV;
	}

	if (FSfclose(fh) != 0) {
		return ENODEV;
	}

	/*
	 * Initialize file handle used when upper layer is reading records
	 * from the log file.
	 */
	read_fh = NULL;

	/*
	 * Log device has been initialized and is now ready
	 * to receive messages.
	 */
	log_device_ready = TRUE;

	return 0;
}

/*
 * Clean up log file.
 */
int
log_clean(void)
{
	if (!log_device_ready)
		return ENODEV;

	/*
	 * Either there is a logging operation currently in progress
	 * or log file is now being read by upper layer. Refuse deleting
	 * the file in such situations.
	 */
	if (log_busy() || (read_fh != NULL))
		return EBUSY;

	/* Kick off clean up process */
	log_cmd = LOG_CMD_CLEAR;
	log_cmd_run = TRUE;

	return 0;
}

/*
 * Returns TRUE if logging service is currently busy.
 */
BOOL
log_busy(void)
{
	return log_cmd_run;
}

/*
 * Log an event.
 */
int
log_post(char *msg)
{
	if (!log_device_ready)
		return ENODEV;

	/* There is already operation in progress, return */
	if (log_busy())
		return EBUSY;

	/*
	 * Copy message terminated with new line to the log buffer
	 * and instruct upper layer to flush it to the log file
	 * at the earliest opportunity.
	 */
	snprintf(log_msg_buf, sizeof (log_msg_buf), "%s\r\n", msg);

	/* Start the operation */
	log_cmd = LOG_CMD_POST;
	log_cmd_run = TRUE;

	return 0;
}

/*
 * Read record from log device.
 *
 * Parameters:
 *   is_first - TRUE initializes reading state machine and reads the first
 *              record from log device.
 *   *rec - will point to the read record
 *   size - size of the record in bytes
 *
 * Returns:
 *   0 - record read, *rec points to the read record - null terminated string
 *   EBUSY  - log machine is currently busy carrying out other operation.
 *   EAGAIN - record is still being read.
 */
int
log_get_record(BOOL is_first, char **rec, int size)
{
	if (log_busy()) {
		/* Record is currently being read. */
		if ((log_cmd == LOG_CMD_GET_FIRST) ||
		    (log_cmd == LOG_CMD_GET_NEXT))
			return EAGAIN;
		else
			/* Logging service busy. */
			return EBUSY;
	}

	/* Record obtained - pass it to the consumer. */
	if (log_record_read) {
		*rec = log_read_rec_pointer;

		/* Clear the flag. */
		log_record_read = FALSE;

		return 0;
	}

	/* Start the operation. */
	log_rec_size = size;
	log_cmd = is_first ? LOG_CMD_GET_FIRST: LOG_CMD_GET_NEXT;
	log_cmd_run = TRUE;

	return EAGAIN;
}

/*
 * Upper layer logging service.
 */
int
log_svc(void)
{
	int err = 0;

	/* If there is nothing to do, return. */
	if (!log_cmd_run)
		return 0;

	switch (log_cmd) {
	/* No operation */
	case LOG_CMD_NOOP:
		break;

	/* Clean up log file. */
	case LOG_CMD_CLEAR:
		err = log_clean_svc();
		break;

	/* Write record to the log device. */
	case LOG_CMD_POST:
		err = log_post_svc();
		break;

	/* Get the first log record from log device. */
	case LOG_CMD_GET_FIRST:
	/* Get next log record from log device. */
	case LOG_CMD_GET_NEXT:
		log_read_svc(log_cmd);
		break;

	} /* switch */

	/*
	 * If any of previous filesystem operations failed, mark the log device
	 * as unusable. That basically disables logging facility.
	 */
	if (err != 0)
		log_device_ready = FALSE;

	/* The log command has been processed. */
	/*
	 * Need to reset the flag first to avoid race condition
	 * in log_get_record() function.
	 */
	log_cmd_run = FALSE;
	log_cmd = LOG_CMD_NOOP;

	return err;
}
