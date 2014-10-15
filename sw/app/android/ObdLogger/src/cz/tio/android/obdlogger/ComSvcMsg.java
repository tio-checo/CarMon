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
    /* Get list of logged freeze frames. */
    public static final int CMD_GET_FF_LIST = 3;
    /* Get list of PIDs logged for given freeze frame. */
    public static final int CMD_GET_FF_PIDS = 4;

    /* Results */
    /* ECU not available */
    public static final int RES_ECU_NOT_AVAILABLE = 0;
    /* Connection to ECU established */
    public static final int RES_ECU_CONNECTED = 1;
    /* List of supported PIDs */
    public static final int RES_GET_SUPPORTED_PIDS = 2;
    /* PID value. */
    public static final int RES_GET_PID_VALUE = 3;
    /* List of logged freeze frames. */
    public static final int RES_GET_FF_LIST = 4;
    /* List of PIDs logged for given freeze frame. */
    public static final int RES_GET_FF_PIDS = 5;

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
     * Asks for list of OBD PIDs supported by the ECU.
     */
    public void getSupportedPids() {
        Bundle b = new Bundle();

        b.putInt(KEY_CMD, CMD_GET_SUPPORTED_PIDS);

        put(b);
    }

    /**
     * Asks for current PID value.
     *
     * @param pid   the PID
     */
    public void getPidValue(String pid) {
        Bundle b = new Bundle();

        b.putInt(KEY_CMD, CMD_GET_PID_VALUE);
        b.putString(KEY_ARG1, pid);

        put(b);
    }

    /**
     * Asks for list of logged freeze frames.
     */
    public void getFreezeFrameList() {
        Bundle b = new Bundle();

        b.putInt(KEY_CMD, CMD_GET_FF_LIST);

        put(b);
    }

    /**
     * Asks for list of PIDs captured for given freeze frame.
     *
     * @param freezeFrameNum   the freeze frame number
     */
    public void getFreezeFramePids(int freezeFrameNum) {
        Bundle b = new Bundle();

        b.putInt(KEY_CMD, CMD_GET_FF_PIDS);
        b.putInt(KEY_ARG1, freezeFrameNum);

        put(b);
    }

    /**
     * Let underlying state machine know we want to quit.
     */
    public void sendQuitMsg() {
        Bundle b = new Bundle();
        b.putInt(KEY_CMD, CMD_QUIT);
        put(b);
    }

    /**
     * Constructor.
     */
    public ComSvcMsg() {
        mData = null;
        mValid = false;
    }
}
