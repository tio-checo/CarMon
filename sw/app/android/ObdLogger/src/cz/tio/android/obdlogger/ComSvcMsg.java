package cz.tio.android.obdlogger;

import android.os.Bundle;

/**
 * Represents transport mechanism for delivering messages from main activity
 * to the communication service.
 */
public class ComSvcMsg {
    /* Message keys */
    /* Command - message received from UI */
    public static final String KEY_CMD = "cmd";
    /* Argument 1 */
    public static final String KEY_ARG1 = "arg1";

    /* Commands */
    /* Invalid command */
    public static final int CMD_INVALID = -1;
    /* Terminate communication service. */
    public static final int CMD_QUIT = 0;
    /* Get list of supported PIDs. */
    public static final int CMD_GET_SUPPORTED_PIDS = 1;
    /* Get PID value. */
    public static final int CMD_GET_PID_VALUE = 2;

    /* Results */
    /* ECU not available */
    public static final int RES_ECU_NOT_AVAILABLE = 0;
    /* Connection to ECU established */
    public static final int RES_ECU_CONNECTED = 1;
    /* List of supported PIDs */
    public static final int RES_GET_SUPPORTED_PIDS = 2;
    /* Get PID value. */
    public static final int RES_GET_PID_VALUE = 3;

    /** Set to <code>true</code> when object contains unprocessed message. */
    private boolean mValid;

    /** Reference to data payload. */
    private Bundle mData;

    /**
     * Checks if the message contains new data to be processed.
     *
     * @return  <code>true</code> if message contains new data;
     *          <code>false</code> otherwise.
     */
    synchronized public boolean hasData() {
        return mValid;
    }

    /**
     * Sends a message to the consumer. Assumes that previous message
     * was processed.
     *
     * @param b the data payload
     */
    synchronized public void put(Bundle b) {
        mData = b;

        mValid = true;

        notifyAll();
    }

    /**
     * Gets data payload from the message. Blocks until new message
     * is available.
     *
     * @return  the data payload in form of Bundle
     */
    synchronized public Bundle get() throws InterruptedException {
        while (mValid == false) {
            wait();
        }

        mValid = false;

        return mData;
    }

    /**
     * Constructor
     */
    public ComSvcMsg() {
        mData = null;
        mValid = false;
    }
}
