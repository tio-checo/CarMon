package cz.tio.android.obdlogger;

import android.app.Activity;
import android.app.ProgressDialog;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.preference.PreferenceManager;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import cz.tio.android.obdlogger.IPicComCallback;
import cz.tio.android.obdlogger.LogMaster;
import cz.tio.android.obdlogger.Obd;
import cz.tio.android.obdlogger.PicCom;
import cz.tio.android.obdlogger.PicConnectionLostException;
import cz.tio.android.obdlogger.PicTimeoutExpiredException;
import cz.tio.android.obdlogger.Probe;

import java.util.ArrayList;

/**
 * Represents message passed from main activity to the communication service
 * running as a background thread.
 */
class ComSvcMsg {
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
        super();

        mData = null;
        mValid = false;
    }
}

/**
 * Represents communication service running as a background thread.
 */
class CommunicationService {
    static final String TAG = "ComSvc";

    /* Modes of operation. */
    /* OBD data logger. */
    public static final int MODE_LOGGER = 0;
    /* OBD diagnostic device. */
    public static final int MODE_DIAG = 1;

    /* Initiates communication with PIC module. */
    private static final int ST_COMM_INIT_PIC = 0;
    private static final int ST_COMM_INIT_PIC1 = 1;
    /* Set ECU communication mode. */
    private static final int ST_COMM_INIT_ECU_MODE = 2;
    /* Initiates communication with ECU. */
    private static final int ST_COMM_INIT_ECU = 3;
    /* Acquire probe value. */
    private static final int ST_ACQUIRE_PROBE_VALUE = 4;

    /* Diagnostic mode. */
    /* Wait for command from UI. */
    private static final int ST_DIAG_WAIT_FOR_CMD = 5;

    private boolean mDebug = false;
    private boolean mQuit = false;
    private int mState;

    /*
     * Underlying protocol used to communicate with ECU.
     * Default to 'KWP2000 mixed with CAN' mode.
     */
    private String mEcuProtocol = Obd.ECU_COMM_MODE_KWP2K_MIXED;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Reference to main Activity */
    private Activity mActivity;

    /* Message handler delivering messages to main UI thread. */
    private Handler mHandler;

    /* Container which delivers messages from main UI thread. */
    private ComSvcMsg mUiMsg;

    /* Mode of operation. */
    private int mMode;

    /* Progress dialog displayed when the device is connecting to PIC. */
    private ProgressDialog mPicConnectPd;

    /* Dashboard */
    private Dashboard mDashboard;

    /* OBD communication module. */
    private Obd mObd;

    /* OBD packet seqence number. */
    private int mSeqNum;

    /* Pid speed groups */
    private PidGroups mPidSpeedGroups;

    /* List of all measured PIDs */
    private ArrayList<String> mPidLogList;

    /* List of PIDs passed to PIC module for auto-scan mode */
    private String mPidAutoScanList;

    /* Status of ECU communication */
    private Button mStsEcu;

    /* GPS status */
    private Button mStsGps;

    /**
     * Signals to communication thread that it should terminate.
     */
    public void finish() {
        mQuit = true;

        /* If operating in diag mode, send thread 'quit' message. */
        if (mMode == MODE_DIAG) {
            Bundle b = new Bundle();

            b.putInt(ComSvcMsg.KEY_CMD, ComSvcMsg.CMD_QUIT);

            mUiMsg.put(b);
        }
    }

    /**
     * Implements communication thread running in background.
     */
    public Runnable loop = new Runnable() {
        public void run() {
            /* Currently processed PID */
            String currPid = "";
            PicCom pc = mObd.getPicCom();
            boolean captureStartVoltage = false;
            int connRetry = 1;

            while (!mQuit) {
                /* Establish communication with PIC module */
                if (mState == ST_COMM_INIT_PIC) {
                    /* Pop-up progress dialog. */
                    mActivity.runOnUiThread(new Runnable() {
                        public void run() {
                            mPicConnectPd.show();
                        }
                    });

                    mState = ST_COMM_INIT_PIC1;
                } else if (mState == ST_COMM_INIT_PIC1) {
                    boolean retry = true;
                    final int ret;

                    /* Try to open communication channel to PIC module. */
                    ret = pc.open();

                    if (ret == 0) {
                        /* Communication established. */

                        /* Dismiss progress dialog. */
                        mActivity.runOnUiThread(new Runnable() {
                            public void run() {
                                mPicConnectPd.dismiss();
                            }
                        });

                        mState = ST_COMM_INIT_ECU_MODE;
                        continue;
                    } else if (ret == -2) {
                    /* Non-recoverable error */
                        retry = false;
                    } else {
                    /* Recoverable error */

                        /* Number of attempts exceeded, give up. */
                        if (connRetry > 3) {
                            retry = false;
                        }
                    }

                    /* Re-try. */
                    if (retry) {
                        mLog.w("Retrying to establish communication " +
                                "with PIC...");

                        /* Sleep for a while and retry. */
                        try {
                            Thread.sleep(1000);
                        } catch (InterruptedException e) {
                            Log.w(ObdLogger.TAG,
                                    "Thread CommunicationService " +
                                    "interrupted.", e);
                        }

                        /* Update progress dialog. */
                        final String pdText = "Retry #" + connRetry++ + ".";
                        mActivity.runOnUiThread(new Runnable() {
                            public void run() {
                                mPicConnectPd.setMessage(pdText);
                            }
                        });

                        continue;
                    }

                    /* Connection to PIC failed, abort. */
                    mLog.w("Failed to establish communication " +
                                "with PIC, aborting.");

                    /* Dismiss progress dialog. */
                    mActivity.runOnUiThread(new Runnable() {
                        public void run() {
                            mPicConnectPd.dismiss();
                        }
                    });

                    /* Let user know that the connection failed. */
                    mActivity.runOnUiThread(new Runnable() {
                        public void run() {
                            Toast.makeText(mActivity, ret == -2 ?
                                    "PIC module not paired.":
                                    "Connection to PIC failed.",
                                    Toast.LENGTH_LONG).show();
                        }
                    });

                    /* Terminate communication thread. */
                    mQuit = true;

                    /* Let the parent activity know that we are done. */
                    mActivity.runOnUiThread(new Runnable() {
                        public void run() {
                            mActivity.finish();
                        }
                    });
                } else if (mState == ST_COMM_INIT_ECU_MODE) {
                    int res = -1;

                    /* Set ECU communication protocol. */
                    try {
                        if (mObd.setEcuCommMode(mEcuProtocol)) {
                            mState = ST_COMM_INIT_ECU;
                        } else {
                            /* Command failed, re-try. */
                            mLog.w("Failed to configure ECU comm mode.");
                        }
                    } catch (PicTimeoutExpiredException e) {
                        /* Timeout expired, re-try. */
                    } catch (PicConnectionLostException e) {
                        /* Lost connection with PIC, try to re-connect. */
                        mState = ST_COMM_INIT_PIC;
                    }
                } else if (mState == ST_COMM_INIT_ECU) {
                /* Establish communication with ECU */
                    boolean res = false;

                    /*
                     * Wait until communication is established. Any packets
                     * previously received from PIC module are discarded.
                     */
                    try {
                        res = mObd.open();
                    } catch (PicConnectionLostException e) {
                        /* Lost connection with PIC, try to re-connect. */
                        mState = ST_COMM_INIT_PIC;

                        continue;
                    }
                    
                    if (!res) {
                        /* Sleep for a second and retry. */
                        try {
                            Thread.sleep(1000);
                        } catch (InterruptedException e) {
                            Log.w(ObdLogger.TAG,
                                    "Thread CommunicationService " +
                                    "interrupted.", e);
                        }

                        continue;
                    }

                    /*
                     * Connection with ECU established.
                     * Based on current mode, either send message to UI
                     * or let user know via indicator on status bar.
                     */
                    if (mMode == MODE_DIAG) {
                        if (!mHandler.sendMessage(mHandler.obtainMessage(
                                ComSvcMsg.RES_ECU_CONNECTED)))
                            mLog.e(TAG, "Sending message to UI failed.");

                        mState = ST_DIAG_WAIT_FOR_CMD;
                        continue;
                    }

                    updateStatusIndicator(mStsEcu, true);

                    mPidSpeedGroups.initPidStateMachine();

                    currPid = mPidSpeedGroups.getNextPidToProcess();

                    /*
                     * If engine is idle, capture battery voltage during
                     * starting phase. Test by measuring battery voltage.
                     * If engine is running, charging is active and battery
                     * voltage is higher comparing with idle phase.
                     */
                    int[] v = new int[1];

                    try {
                        double batt;

                        v[0] = pc.getAdValue("08");
                        batt = v[0] * 0.01837;

                        if (batt > 13.0) {
                            /* Engine running */
                            captureStartVoltage = false;
                        } else {
                            /* Engine idle, measure starting voltage. */
                            captureStartVoltage = true;
                        }
                    } catch (PicTimeoutExpiredException e) {
                        /* Failed to obtain battery voltage, skip this phase. */
                        mLog.w("Failed to obtain battery voltage.");

                        captureStartVoltage = false;
                    } catch (PicConnectionLostException e) {
                        /* Lost connection with PIC, try to re-connect. */
                        mState = ST_COMM_INIT_PIC;

                        continue;
                    }

                    try {
                        if (!confAcqModule(captureStartVoltage)) {
                            /* Failed to configure data acq module, re-try. */
                            continue;
                        }
                    } catch (PicConnectionLostException e) {
                        /* Lost connection with PIC, try to re-connect. */
                        mState = ST_COMM_INIT_PIC;

                        continue;
                    }

                    /* Update battery voltage on dashboard. */
                    Probe battProbe = mObd.getProbe("Vst");

                    /* Update appropriate probe */
                    if (battProbe.update(v)) {
                        /* Update indicator with new value */
                        mDashboard.updateIndicator(battProbe);
                    }

                    if (!captureStartVoltage) {
                        try {
                            /*
                             * Configure and start OBD auto-scan mode.
                             * Set sample rate to 50 ms.
                             */
                            pc.configObdAutoScan(mPidAutoScanList, "0032");
                        } catch (PicTimeoutExpiredException e) {
                            /* Timeout expired, re-try. */
                            mLog.w("Failed to configure OBD auto-scan.");
                            continue;
                        } catch (PicConnectionLostException e) {
                            /* Lost connection with PIC, try to re-connect. */
                            mState = ST_COMM_INIT_PIC;

                            continue;
                        }
                    }

                    mState = ST_ACQUIRE_PROBE_VALUE;
                } else if (mState == ST_ACQUIRE_PROBE_VALUE) {
                    int tout;
                    String p = null;

                    /*
                     * If capturing battery voltage during engine start,
                     * wait longer, since it may take a while.
                     */
                    if (captureStartVoltage) {
                        tout = 60;
                    } else {
                        tout = 1;
                    }
      
                    /* Wait for packet from PIC module. */
                    boolean connLost = false;

                    do {
                        try {
                            p = pc.getPacket();
                        } catch (PicTimeoutExpiredException e) {
                            /* Timeout expired. */
                            p = null;
                        } catch (PicConnectionLostException e) {
                            /* Connection with PIC lost. */
                            connLost = true;
                        }
                    } while (p == null && --tout > 0 && !mQuit && !connLost);

                    /* Application finished. */
                    if (mQuit) {
                        continue;
                    }

                    /* Connection with PIC module lost. */
                    if (connLost) {
                        mState = ST_COMM_INIT_PIC;

                        /*
                         * Sleep for a while, then retry to establish
                         * communication again.
                         */
                        try {
                            Thread.sleep(5000);
                        } catch (InterruptedException e) {
                            Log.w(ObdLogger.TAG,
                                    "Thread CommunicationService " +
                                    "interrupted.", e);
                        }

                        continue;
                    }

                    /* Timeout occurred */
                    if (p == null) {
                        mLog.w("Connection with ECU lost.");

                        /*
                         * Communication with ECU is broken,
                         * try to establish it again.
                         */
                        /* Let user know via indicator on status bar. */
                        updateStatusIndicator(mStsEcu, false);

                        /* Close ECU communication channel. */
                        mObd.close();

                        mState = ST_COMM_INIT_ECU;

                        continue;
                    }

                    int[] v;

                    /*
                     * NAK response received from ECU indicates broken
                     * connection.
                     */
                    if (p.startsWith(Obd.PKT_NAK)) {
                        mLog.w(String.format(
                                "Communication with ECU lost, got <%s> NAK " +
                                "response.", p));

                        /* First inform user via indicator on status bar. */
                        updateStatusIndicator(mStsEcu, false);

                        /* Stop data acquisition. */
                        try {
                            pc.stopAcq();
                        } catch (PicTimeoutExpiredException e) {
                            /* Timeout expired. */
                        } catch (PicConnectionLostException e) {
                            mState = ST_COMM_INIT_PIC;

                            continue;
                        }

                        /* Close ECU communication channel. */
                        mObd.close();

                        /* Try to re-establish communication with ECU. */
                        mState = ST_COMM_INIT_ECU;
                    } else if (p.startsWith("Q00")) {
                    /* Data acquisition module */

                        /*
                         * Phase of measuring starting voltage done, log
                         * acquired data to file and switch to periodic
                         * acquisition of average battery voltage.
                         */
                        if (captureStartVoltage) {
                            mLog.i("Measuring of starting voltage done.");

                            /* Log acquired data to file. */
                            mLog.logStartingVoltage(p);

                            captureStartVoltage = false;

                            /* Reconfigure acquisition module. */
                            try {
                                confAcqModule(false);
                            } catch (PicConnectionLostException e) {
                                mState = ST_COMM_INIT_PIC;

                                continue;
                            }

                            /* Configure and start OBD auto-scan mode */
                            try {
                                /* Set sample rate to 50 ms. */
                                pc.configObdAutoScan(mPidAutoScanList, "0032");
                            } catch (PicTimeoutExpiredException e) {
                                /* Failed to configure auto-scan mode, retry. */
                                mState = ST_COMM_INIT_ECU;

                                /* Close ECU communication channel. */
                                mObd.close();

                                /* Try to re-establish communication with ECU. */
                                mState = ST_COMM_INIT_ECU;

                                continue;
                            } catch (PicConnectionLostException e) {
                                mState = ST_COMM_INIT_PIC;

                                continue;
                            }

                            /* Packet processed, wait for another one. */
                            continue;
                        }

                        /*
                         * Sanity check if valid data packet has been received.
                         */
                        if (p.length() != 59) {
                                mLog.w("Invalid data acquisition packet " +
                                        "received.");

                                /* Discard the packet. */
                                continue;
                        }

                        /* Valid data packet received, process it. */

                        String[] updatedPids = {"Vst", "Vrms", "Vmin", "Vmax",
                                "F1", "M1", "F2", "M2", "F3", "M3", "F4", "M4",
                                "F5", "M5"};
                        v = new int[1];

                        for (int i = 0; i < updatedPids.length; i++) {
                            currPid = updatedPids[i];
                            Probe currProbe = mObd.getProbe(currPid);
                            int b = 3 + i * 4;
                            int e = 7 + i * 4;

                            /* extract value from packet */
                            v[0] = Integer.parseInt(p.substring(b, e), 16);

                            /* Vmin and Vmax are 16-bit signed values */
                            if (((i == 2) || (i == 3)) && (v[0] >= 32768)) {
                                v[0] -= 65536;
                            }

                            /* Update appropriate probe */
                            if (currProbe.update(v)) {
                                /* Update indicator with new value */
                                mDashboard.updateIndicator(currProbe);
                            }
                        }
                    } else if (p.startsWith(Obd.PKT_GET_PID_AS_RESP)) {
                        /* Validate packet sequence number */
                        int curSeqNum = Integer.parseInt(p.substring(2, 4), 16);

                        /* If out of sync, log the warning and resync */
                        if (curSeqNum != mSeqNum) {
                            mLog.w(TAG, String.format("Bad sequence number " +
                                    "(expected <%d>, got <%d>, resyncing...",
                                    mSeqNum, curSeqNum));

                            mSeqNum = curSeqNum;
                        }

                        /* Update packet sequence number */
                        if (++mSeqNum > 255) {
                            mSeqNum = 1;
                        }

                        currPid = p.substring(4, 6);
                        Probe currProbe = mObd.getProbe(currPid);

                        /* extract OBD PID value from packet */
                        int ln = (p.length() - 6) / 2;
                        v = new int[ln];

                        for (int i = 0; i < ln; i++) {
                            v[i] = Integer.parseInt(p.substring(6 + (i * 2),
                                    8 + (i * 2)), 16);
                        }

                        /* Update appropriate probe */
                        if (currProbe.update(v)) {
                            /* Update indicator with new value */
                            mDashboard.updateIndicator(currProbe);
                        }

                        /*
                         * Recalculate and update TurboIndicator if applicable.
                         */
                        TurboProbe turboPar =
                                (TurboProbe) mObd.getProbe("Turbo");

                        if (turboPar.update(currPid, mObd)) {
                            mDashboard.updateIndicator(turboPar);
                        }

                        /*
                         * Log data into log file every time all PIDs
                         * in the first group get processed.
                         */
                        /* TODO: Use some method for this check */
                        if (currPid.equals("49")) {
                            mLog.captureProbes(mPidLogList, mObd);
                        }
                    } else {
                        mLog.w(TAG, String.format("Discarding packet <%s>.",
                                p));

                        continue;
                    }
                } else if (mState == ST_DIAG_WAIT_FOR_CMD) {
                    Bundle msg;

                    /* Block until UI asks us to do something. */
                    try {
                        mLog.d(TAG, "Waiting for UI message...");
                        msg = mUiMsg.get();
                    } catch (InterruptedException e) {
                        mLog.w(TAG, "Communication thread interrupted when " +
                                "waiting for message from UI.");
                        continue;
                    }

                    /* Data payload empty, there is nothing to process. */
                    if (msg == null) {
                        mLog.w(TAG, "EMPTY.");

                        continue;
                    }

                    mLog.d(TAG, "RECEIVED.");

                    /* Process the message. */
                    int cmd = msg.getInt(ComSvcMsg.KEY_CMD,
                            ComSvcMsg.CMD_INVALID);

                    mLog.d(TAG, String.format("UI command: %d", cmd));

                    switch (cmd) {
                    /* Get list of supported PIDs. */
                    case ComSvcMsg.CMD_GET_SUPPORTED_PIDS:
                        ArrayList<String> pidList = null;

                        try {
                            mLog.d(TAG, "Obtaining list of supported PIDs...");
                            pidList = mObd.getSupportedPids();
                        } catch (PicTimeoutExpiredException e) {
                            /* Timeout expired, let UI know. */
                            mLog.e(TAG, "TIMEOUT.");
                        } catch (PicConnectionLostException e) {
                            /* Connection with PIC lost. */
                            mLog.e(TAG, "CONNECTION LOST.");

                            mState = ST_COMM_INIT_PIC;
                            continue;
                        }

                        /* Pass the result to the UI. */
                        if (!mHandler.sendMessage(mHandler.obtainMessage(
                                ComSvcMsg.RES_GET_SUPPORTED_PIDS, pidList))) {
                            mLog.e(TAG, "Sending list of supported PIDs to " +
                                    "UI failed.");
                        }

                        break;

                    /* Get PID value. */
                    case ComSvcMsg.CMD_GET_PID_VALUE:
                        String pidId = msg.getString(ComSvcMsg.KEY_ARG1, "");

                        if (pidId.equals("")) {
                            mLog.w(TAG, "PID ID not provided.");
                            continue;
                        }

                        Probe probe = mObd.getProbe(pidId);

                        if (probe == null) {
                            mLog.w(TAG, "Probe for PID <" + pidId + "> not " +
                                    "found.");
                            continue;
                        }

                        try {
                            mLog.d(TAG, "Obtaining value of PID <" + pidId +
                                    ">...");
                            probe.acquire();
                        } catch (PicTimeoutExpiredException e) {
                            /* Timeout expired, let UI know. */
                            mLog.e(TAG, "TIMEOUT.");
                            continue;
                        } catch (PicConnectionLostException e) {
                            /* Connection with PIC lost. */
                            mLog.e(TAG, "CONNECTION LOST.");

                            mState = ST_COMM_INIT_PIC;
                            continue;
                        }

                        /* Pass the result to the UI. */
                        String resStr = " " + probe.getLabel() + ": " +
                                probe.physToText();

                        if (!mHandler.sendMessage(mHandler.obtainMessage(
                                ComSvcMsg.RES_GET_PID_VALUE, resStr))) {
                            mLog.e(TAG, "Sending PID value to UI failed.");
                        }

                        break;

                    /* Terminate the thread. */
                    case ComSvcMsg.CMD_QUIT:
                        mQuit = true;
                        break;

                    case ComSvcMsg.CMD_INVALID:
                        mLog.w(TAG, "Invalid command.");
                        break;
                    }
                }
            } /* while */

            /*
             * Upper layer signaled that communication service should finish
             * its job. Clean up things.
             */

            /* Stop acquisition - send only if we are connected to the PIC. */
            if (mState > ST_COMM_INIT_PIC1) {
                try {
                    pc.stopAcq();
                } catch (PicTimeoutExpiredException e) {
                    /* Timeout expired. */
                    mLog.w("Failed to stop acquisition module.");
                } catch (PicConnectionLostException e) {
                    /* Connection with PIC lost. */
                    mLog.w("Failed to stop acquisition module.");
                }

                /* Close ECU communication channel. */
                if (!mObd.close())
                    mLog.w("Failed to close ECU communication channel.");

                /* Close PIC communication channel. */
                if (pc.close() != 0)
                    mLog.w("Failed to close PIC communication channel.");
            }
        }
    };

    /**
     * Constructor.
     *
     * @param activity  the reference to activity which spawned this service
     * @param handler   the message handler delivering messages to UI thread
     * @param uiMsg     the container delivering messages from UI thread
     * @param mode      the mode of operation
     * @param obd       the OBD communication object
     */
    public CommunicationService(Activity activity, Handler handler,
            ComSvcMsg uiMsg, int mode, Obd obd) {
        super();

        /* Save reference to OBD communication object. */
        mObd = obd;

        /* Initialize OBD packet sequence number. */
        mSeqNum = 1;

        /*
         * Determine ECU communication protocol.
         * Default to 'KWP2000 mixed with CAN' mode.
         */
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(activity);

        mEcuProtocol = prefs.getString("pref_ecu_protocol",
                Obd.ECU_COMM_MODE_KWP2K_MIXED);

        /* Create progress dialog displayed when connecting to PIC. */
        mPicConnectPd = new ProgressDialog(activity);
        mPicConnectPd.setTitle("Connecting to ECU...");
        mPicConnectPd.setMessage("Please wait.");
        mPicConnectPd.setCancelable(false);
        mPicConnectPd.setIndeterminate(true);
        
        /* Instantiate dashboard */
        mDashboard = new Dashboard(activity);

        /* Initialize state machine */
        mState = ST_COMM_INIT_PIC;

        /* Save reference to main Activity. */
        mActivity = activity;

        /* Save reference to UI message handler. */
        mHandler = handler;

        /* Save reference to message. */
        mUiMsg = uiMsg;

        /* Save mode. */
        mMode = mode;

        /* Initialize references to status indicators */
        mStsEcu = (Button) activity.findViewById(R.id.ecuState);
        mStsGps = (Button) activity.findViewById(R.id.gpsState);

        /* Create list of PID priority groups */
        mPidSpeedGroups = new PidGroups();

        /* Fast - RPM, Rail Pressure, Accelerator pedal position D */
        String grpF[] = { "0C", "23", "49" };
        mPidSpeedGroups.create(0, "fast", grpF);

        /* Medium - EGR, MAF, Intake Manifold Abs. Pressure */
        String grpM[] = {"2C", "10", "0B"};
        mPidSpeedGroups.create(1, "medium", grpM);

        /*
         * Slow - Water & Air Temperature, Vehicle Speed, Barometric Pressure,
         *        Battery Voltage
         */
        String grpS[] = {"05", "0D", "0F", "33"};
        mPidSpeedGroups.create(2, "slow", grpS);

        /*
         * Generate list of all PIDs to be processed. Start with the highest
         * priority. The list is used for logging purposes.
         */
        mPidLogList = mPidSpeedGroups.getAllPids();

        /*
         * Add calculated 'Turbo' to the list, so that it gets logged.
         */
        mPidLogList.add("Turbo");

        /*
         * Add battery voltage probe to the list, so that it gets logged.
         */
        mPidLogList.add("Vst");

        Log.d(ObdLogger.TAG, "PID log list:" + mPidLogList);

        /*
         * Generate list of PIDs to be auto-scanned by PIC module.
         */
        mPidAutoScanList = mPidSpeedGroups.createAutoScanPidList(-1);
        Log.d(ObdLogger.TAG, "PID auto scan list:" + mPidAutoScanList);
    }

    /**
     * Updates status indicator.
     */
    private void updateStatusIndicator(final Button stsIndicator,
            final boolean on) {

        /* If indicator does not exist, there is nothing to update. */
        if (stsIndicator == null)
            return;

        mActivity.runOnUiThread(new Runnable() {
            public void run() {
                stsIndicator.setEnabled(on);
            }
        });
    }

    /**
     * Configures data acquisition module.
     * The module is configure either for capturing battery voltage during
     * engine starting phase or for measuring charging voltage when
     * engine is running.
     *
     * @param  startingPhase <code>true</code> to configure module
     *                       to measure voltage during starting phase.
     *                       <code>false</code> to configure module
     *                       to measure voltage when engine is running.
     * @return               <code>true</code> if module was successfully
     *                       configured; <code>false</code> otherwise.
     * @throws PicConnectionLostException when lost connection with PIC
     */
    private boolean confAcqModule(boolean startingPhase)
            throws PicConnectionLostException {
        PicCom pc = mObd.getPicCom();

        /*
         * Make sure acquisition module is idle as well as turn off
         * post-processing module.
         */
        try {
            pc.stopAcq();
        } catch (PicTimeoutExpiredException e) {
            mLog.w("Failed to stop data acquisition.");
            return false;
        }

        try {
            pc.configPpModule("00");
        } catch (PicTimeoutExpiredException e) {
            mLog.w("Failed to deactivate post-processing module.");

            return false;
        }

        /*
         * Configure acquisition module for capturing battery voltage
         * during starting phase.
         */
        if (startingPhase) {
            /*
             * Configure data acquisition module
             *  - channel            08
             *  - acquisition mode   Single
             *  - trigger mode       Normal
             *  - trigger event      Rising edge
             *  - trigger value      735 (13.5 V)
             *  - sample rate        10 ms
             *  - refresh rate       0 ms (not used)
             *  - pre-trigger size   0 (all acquired data is returned)
             *  - post-trigger size  150 samples (1.5 second)
             */
            try {
                pc.configAcqModule("0801010102DF00A1000000000096");
            } catch (PicTimeoutExpiredException e) {
                mLog.w("Failed to configure data acquisition module 1.");

                return false;
            }
        } else {
            /*
             * Configure data acquisition module
             *  - channel            08
             *  - acquisition mode   Continuous
             *  - trigger mode       Auto
             *  - trigger event      Rising edge (not used)
             *  - trigger parameter  Not used
             *  - sample rate        400 us
             *  - refresh rate       500 ms
             *  - pre-trigger size   128 samples
             *  - post-trigger size  128 samples
             */
            try {
                pc.configAcqModule("08020201000019001F4100800080");
            } catch (PicTimeoutExpiredException e) {
                mLog.w("Failed to configure data acquisition module 2.");

                return false;
            }

            /*
             * Activate signal identification post-processing module.
             * Calculates following characteristics of acquired signal:
             *  - mean value
             *  - RMS value of AC part of signal
             *  - minimum of AC part of signal
             *  - maximum of AC part of signal
             *  - five strongest frequencies of signal spectrum
             */
            try {
                pc.configPpModule("02A");
            } catch (PicTimeoutExpiredException e) {
                mLog.w("Failed to activate post-processing module.");

                return false;
            }
        }

        /* Start data acquisition */
        try {
            pc.runAcq();
        } catch (PicTimeoutExpiredException e) {
            mLog.w("Failed to start data acquisition.");

            return false;
        }

        return true;
    }
}

/**
 * Callback methods called when communication with PIC module is established
 * or lost.
 */
class PicComCallback implements IPicComCallback {
    /** Reference to parent Activity */
    private Activity mActivity;

    /** Reference to Pic status button */
    private Button mStsPic;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /**
     * Called when connection with PIC module is established.
     */
    public void onConnectionEstablished() {
        mLog.i("Connection with PIC established.");

        updateStatusIndicator(mStsPic, true);
    }

    /**
     * Called when connection with PIC module is lost.
     */
    public void onConnectionLost() {
        mLog.i("Connection with PIC lost.");

        updateStatusIndicator(mStsPic, false);
    }

    /**
     * Constructor
     *
     * @param activity     the reference to parent Activity
     */
    public PicComCallback(Activity activity) {
        mActivity = activity;

        mStsPic = (Button) mActivity.findViewById(R.id.picState);
    }

    /**
     * Updates status indicator.
     */
    private void updateStatusIndicator(final Button stsIndicator,
            final boolean on) {

        /* If indicator does not exist, there is nothing to update. */
        if (stsIndicator == null)
            return;

        mActivity.runOnUiThread(new Runnable() {
            public void run() {
                stsIndicator.setEnabled(on);
            }
        });
    }

}

/**
 * Represents dashboard indicator.
 */
class DashboardIndicator {
    /* Reference to main Activity */
    private Activity mActivity;
    /* UI id of label describing the indicator */
    private int mLabelId;
    /* UI id of indicator value */
    private int mValueId;
    /* UI id of progress bar */
    private int mPbId;
    /* OBD PID displayed by this indicator */
    public String mPid;

    /* Previous value of indicator */
    private String mPrevText = "";

    /**
     * Constructor
     */
    public DashboardIndicator(Activity activity, int labelId,
            int valueId, int pbId, String pid) {
        mActivity = activity;

        mLabelId = labelId;
        mValueId = valueId;
        mPbId = pbId;
        mPid = pid;
    }

    /**
     * Refreshes indicator.
     */
    public void update(final String newText) {
        /*
         * Check if new value is different from previous one.
         * If both are the same, there is no need to update
         * indicators.
         */
        if (newText.equals(mPrevText)) {
            return;
        }

        mPrevText = newText;

        /* Update label with new text. */
        final TextView lbl = (TextView) mActivity.findViewById(mValueId);

        mActivity.runOnUiThread(new Runnable() {
            public void run() {
                lbl.setText(newText);
            }
        });

        /*
         * Update specified progress bar with new fraction.
         * Fraction has to fit <0.0. 1.0> interval.
         */
/*
        if pb_fraction < 0.0:
            pb_fraction = 0.0
        elif pb_fraction > 1.0:
            pb_fraction = 1.0

        pb = self.builder.get_object(pb_name)
        pb.set_fraction(pb_fraction)
*/

    }
}

/**
 * Represents complete dashboard - currently contains 7 indicators.
 */
class Dashboard {
    /* Array holding dashboard indicators */
    private DashboardIndicator[] mDashboardIndicators;

    /**
     * Constructor
     */
    public Dashboard(Activity activity) {
        /* Instantiate indicators and associate them with OBD PIDs.*/
        mDashboardIndicators = new DashboardIndicator[7];
        /* RPM */
        mDashboardIndicators[0] = new DashboardIndicator(activity,
                R.id.indicator1Label, R.id.indicator1Value, 0, "0C");
        /* Calculated Engine Load */
        mDashboardIndicators[1] = new DashboardIndicator(activity,
                R.id.indicator2Label, R.id.indicator2Value, 0, "Turbo");
//                R.id.indicator2Label, R.id.indicator2Value, 0, "04");
        /* MAF */
        mDashboardIndicators[2] = new DashboardIndicator(activity,
                R.id.indicator3Label, R.id.indicator3Value, 0, "10");
        /* WATER */
        mDashboardIndicators[3] = new DashboardIndicator(activity,
                R.id.indicator4Label, R.id.indicator4Value, 0, "05");
        /* RAIL */
        mDashboardIndicators[4] = new DashboardIndicator(activity,
                R.id.indicator5Label, R.id.indicator5Value, 0, "23");
        /* EGR */
        mDashboardIndicators[5] = new DashboardIndicator(activity,
                R.id.indicator6Label, R.id.indicator6Value, 0, "2C");
        /* Batt */
        mDashboardIndicators[6] = new DashboardIndicator(activity,
                R.id.indicator7Label, R.id.indicator7Value, 0, "Vst");
    }

    /**
     * Returns indicator associated with given PID.
     * Returns null if there is no indicator associated with given PID.
     */
    private DashboardIndicator getIndicator(String pid) {
        for (int i = 0; i < mDashboardIndicators.length; i++) {
            if (mDashboardIndicators[i].mPid.equals(pid)) {
                return mDashboardIndicators[i];
            }
        }

        return null;
    }

    /**
     * Updates indicator with new value.
     */
    public void updateIndicator(Probe probe) {
        DashboardIndicator indicator = getIndicator(probe.getId());
        /* If PID has no indicator associated, there is nothing to update. */
        if (indicator == null) {
            return;
        }

        /* Calculate updated values for indicators. */
        String textValue = probe.physToText();

/*
        double fraction = par.physToPbFraction();
*/

        /* update indicator */
        indicator.update(textValue);
    }
}

/**
 * Represents groups of probes which are refreshed with the same priority.
 */
class PidGroup {
    /* group ID */
    private String mGroupName;

    /* PID list */
    private ArrayList<String> mPidList;

    /* Index of currently processed PID */
    private int mPidIndex;

    /**
     * Constructor
     */
    public PidGroup(String groupName, String[] pidArray) {
        mGroupName = groupName;

        /* Create and populate ArrayList with PIDs */
        mPidList = new ArrayList<String>();

        for (int i = 0; i < pidArray.length; i++) {
            mPidList.add(pidArray[i]);
        }

        /* Reset index of currently processed PID */
        mPidIndex = 0;
    }

    /**
     * Returns next PID to be processed or empty string if all PIDs
     * in the group were processed.
     */
    public String getNext() {
        if (mPidIndex >= mPidList.size()) {
            mPidIndex = 0;

            return "";
        }

        return mPidList.get(mPidIndex++);
    }

    /**
     * Initializes state machine - first PID is to be processed next.
     */
    public void reset() {
        mPidIndex = 0;
    }

    /**
     * Generates list of all PIDs stored in the group.
     */
    public ArrayList<String> getAllPids() {
        ArrayList<String> pidList = new ArrayList<String>();

        for (int i = 0; i < mPidList.size(); i++) {
            pidList.add(mPidList.get(i));
        }

        return pidList;
    }

    /**
     * Returns true if all pids were processed. Otherwise returns false.
     */
    public boolean allPidsProcessed() {
        return (mPidIndex >= mPidList.size());
    }
}

/**
 * Represents set of groups of probes.
 */
class PidGroups {
    /* PID group */
    private ArrayList<PidGroup> mPidGroups;

    /* Index of currently processed group */
    private int mGroupIndex;

    /**
     * Constructor
     */
    public PidGroups() {
        mPidGroups = new ArrayList<PidGroup>();

        mGroupIndex = 0;
    }

    /**
     * Creates new group with given index. Higher index means lower priority.
     */
    public void create(int index, String groupId, String[] pidArray) {
        PidGroup pidGroup = new PidGroup(groupId, pidArray);

        mPidGroups.add(index, pidGroup);
    }

    /**
     * Generates list of all PIDs stored in all groups.
     */
    public ArrayList<String> getAllPids() {
        ArrayList<String> pidList = new ArrayList<String>();

        for (int i = 0; i < mPidGroups.size(); i++) {
                pidList.addAll(mPidGroups.get(i).getAllPids());
        }

        return pidList;
    }

    /**
     * Initializes PID state machine.
     */
    public void initPidStateMachine() {
        for (int i = 0; i < mPidGroups.size(); i++) {
            mPidGroups.get(i).reset();
        }

        mGroupIndex = 0;
    }

    /**
     * Move to the next PID to be queried. Start with the first group.
     * If all PIDs has been processed there, continue with next group, etc.
     */
    public String getNextPidToProcess() {
        String nextPid;

        for (int i = 0; i < mPidGroups.size(); i++) {
            nextPid = mPidGroups.get(i).getNext();

            /* Got valid PID, return */
            if (!nextPid.equals("")) {
                return nextPid;
            }
        }

        /*
         * If we got here, it means that PID from last group is to be served
         * now, though all PIDs in the last group were already processed.
         * In such case, go with the first PID in the last group.
         */
        return mPidGroups.get(mPidGroups.size() - 1).getNext();
    }

    /**
     * Returns true if all PIDs in specified group have been served.
     * Otherwise returns false.
     */
    public boolean groupProcessed(int grpNum) {
        return (mPidGroups.get(grpNum).allPidsProcessed());
    }

    /**
     * Constructs list of PID which will be sent to PIC when initiating
     * auto-scan mode. This is one of the examples where using recursion
     * is reasonable thing to do, since it simplifies implementation
     * and improves readability of the code.
     */
    public String createAutoScanPidList(int grpIndex) {
        PidGroup grp;
        String pidList = new String();

        /* Init phase */
        if (grpIndex == -1) {
            grpIndex = mPidGroups.size() - 1;
        }

        grp = mPidGroups.get(grpIndex);
        /* Make sure we start from the first PID. */
        grp.reset();

        /* Enumerate through all PIDs in current group */
        while (!grp.allPidsProcessed()) {
            int i;

            /*
             * First process all PIDs in groups with higher priority (if there
             * is one). In check we take advantage of the fact that highest
             * priority group is the first one in the group list -
             * it has index 0.
             */
            for (i = grpIndex - 1; i >= 0; i--) {
                pidList += this.createAutoScanPidList(i);
            }

            /* Now add to the list PID in currently processed group */
            pidList += grp.getNext();
        }

        return pidList;
    }
}

