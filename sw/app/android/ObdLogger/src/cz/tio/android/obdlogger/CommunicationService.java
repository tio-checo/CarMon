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

import cz.tio.android.obdlogger.ComSvcMsg;
import cz.tio.android.obdlogger.ComSvcState;
import cz.tio.android.obdlogger.ComSvcStateMachine;
import cz.tio.android.obdlogger.IPicComCallback;
import cz.tio.android.obdlogger.LogMaster;
import cz.tio.android.obdlogger.Obd;
import cz.tio.android.obdlogger.PicCom;
import cz.tio.android.obdlogger.PicConnectionLostException;
import cz.tio.android.obdlogger.PicTimeoutExpiredException;
import cz.tio.android.obdlogger.Probe;

import java.util.ArrayList;
import java.util.HashMap;

/**
 * Indicates what kind of action should be taken by state machine
 * in the next step. Returned by current state.
 */
enum ComSvcStateAction {
    STAY_IN_CURRENT,   /* Stay in current state. */
    GOTO_NEXT,         /* Move to the next state. */
    QUIT               /* Terminate the state machine. */
}

/**
 * State responsible for establishing connection to PIC module.
 */
class ComSvcStateConnectToPic extends ComSvcState {
    /* Progress dialog displayed when the device is connecting to PIC. */
    private ProgressDialog mPicConnectPd;

    /* Number of connection retries. */
    private int mConnRetry;

    /* Indicates if and why connection failed. */
    private int mConnRet;

    /**
     * Called when state is entered.
     */
    public void enter() {
        /* Display pop-up progress dialog. */
        getActivity().runOnUiThread(new Runnable() {
            public void run() {
                mPicConnectPd.show();
            }
        });
    }

    /**
     * Called when state is left.
     */
    public void leave() {
        final Activity activity = getActivity();

        /* Dismiss progress dialog. */
        activity.runOnUiThread(new Runnable() {
            public void run() {
                mPicConnectPd.dismiss();
            }
        });

        /*
         * Connection to PIC established, do not display
         * 'connection failed' message.
         */
        if (mConnRet == 0) {
            return;
        }

        /* Let user know via pop-up message that the connection failed. */
        activity.runOnUiThread(new Runnable() {
            public void run() {
                Toast.makeText(activity, mConnRet == -2 ?
                        "PIC module not paired.": "Connection to PIC failed.",
                        Toast.LENGTH_LONG).show();
            }
        });
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Activity activity = getActivity();

        boolean retry = true;

        PicCom pc = getObd().getPicCom();

        /* Try to open communication channel to PIC module. */
        mConnRet = pc.open();

        if (mConnRet == 0) {
            /* Communication established. */
            return ComSvcStateAction.GOTO_NEXT;
        } else if (mConnRet == -2) {
            /* Non-recoverable error */
            retry = false;
        } else {
            /* Recoverable error */

            /* Number of attempts exceeded, give up. */
            if (mConnRetry > 3) {
                retry = false;
            }
        }

        /* Re-try. */
        if (retry) {
            mLog.w("Retrying to establish communication with PIC...");

            /* Sleep for a while and retry. */
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {
                Log.w(ObdLogger.TAG, "Thread ComService interrupted.", e);
            }

            /* Update progress dialog. */
            final String pdText = "Retry #" + mConnRetry++ + ".";
            activity.runOnUiThread(new Runnable() {
                public void run() {
                    mPicConnectPd.setMessage(pdText);
                }
            });

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        mLog.w("Failed to establish communication with PIC, aborting.");

        /* Could not connect to PIC, there is no point to continue. */
        return ComSvcStateAction.QUIT;
    }

    /**
     * Constructor
     */
    public ComSvcStateConnectToPic(ComSvcStateMachine stateMachine) {
        super(stateMachine);

        /* Create progress dialog displayed when connecting to PIC. */
        mPicConnectPd = new ProgressDialog(getActivity());
        mPicConnectPd.setTitle("Connecting to ECU...");
        mPicConnectPd.setMessage("Please wait.");
        mPicConnectPd.setCancelable(false);
        mPicConnectPd.setIndeterminate(true);

        /* Number of connection retries. */
        mConnRetry = 1;
    }
}

/**
 * Configures ECU communication mode.
 */
class ComSvcStateConfEcuComMode extends ComSvcState {
    /* ECU communication mode */
    private String mEcuProtocol;

    /**
     * Called when state is entered.
     */
    public void enter() {
        Activity activity = getActivity();

        /*
         * Get ECU communication protocol from preferences.
         * Default to 'KWP2000 mixed with CAN' mode.
         */
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(activity);

        mEcuProtocol = prefs.getString("pref_ecu_protocol",
                Obd.ECU_COMM_MODE_KWP2K_MIXED);
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Obd obd = getObd();

        /* Set ECU communication protocol. */
        if (obd.setEcuCommMode(mEcuProtocol)) {
            return ComSvcStateAction.GOTO_NEXT;
        } else {
            /* Command failed, re-try. */
            mLog.w("Failed to configure ECU comm mode.");
        }

        return ComSvcStateAction.STAY_IN_CURRENT;
    }

    /**
     * Constructor
     */
    public ComSvcStateConfEcuComMode(ComSvcStateMachine stateMachine) {
        super(stateMachine);

        /* Default ECU communication protocol. */
        mEcuProtocol = Obd.ECU_COMM_MODE_KWP2K_MIXED;
    }
}

/**
 * Establishes connection to ECU.
 */
class ComSvcStateConnectToEcu extends ComSvcState {
    /**
     * Called when state is entered.
     */
    public void enter() {
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Obd obd = getObd();

        /*
         * Wait until communication with ECU is established. Any packets
         * previously received from PIC module are discarded.
         */
        if (obd.open()) {
            return ComSvcStateAction.GOTO_NEXT;
        }

        /* Sleep for a second and retry. */
        try {
            Thread.sleep(1000);
        } catch (InterruptedException e) {
            Log.w(ObdLogger.TAG, "Thread ComService interrupted.", e);
        }

        return ComSvcStateAction.STAY_IN_CURRENT;
    }

    /**
     * Constructor
     */
    public ComSvcStateConnectToEcu(ComSvcStateMachine stateMachine) {
        super(stateMachine);
    }
}

/**
 * Configures auto-scan probe acquisition mode
 * and initiates measuring of starting voltage.
 */
class ComSvcStateConfAcqMode extends ComSvcState {
    /**
     * Called when state is entered.
     */
    public void enter() {
        /* Initialize flag indicating if starting voltage is to be captured. */
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(getActivity());

        setStateDataBoolean("captureStartVoltage", prefs.getBoolean(
                "pref_meas_start_voltage", false));

        /*
         * Create list of PID priority groups based on selected logging profile.
         * Default to 'RAIL' profile.
         * TODO: Deliver definition of logging profiles via XML file.
         */
        String logProfile = prefs.getString("pref_logging_profile", "RAIL");

        PidGroups pidSpeedGroups = new PidGroups();

        if (logProfile.equals("RAIL")) {
            /* Fast - RPM, Rail Pressure, Accelerator pedal position D */
            String grpF[] = {"0C", "23", "49"};

            /*
             * Medium - Calculated Load, EGR, MAF,
             *          Intake Manifold Abs. Pressure, Vehicle Speed
             */
            String grpM[] = {"04", "2C", "10", "0B", "0D"};

            pidSpeedGroups.create(0, "fast", grpF);
            pidSpeedGroups.create(1, "medium", grpM);
        } else if (logProfile.equals("EGT")) {
            /*
             * Fast - RPM, Rail Pressure, EGT Bank1,
             * Accelerator pedal position D
             */
            String grpF[] = {"0C", "23", "78", "49"};

            /*
             * Medium - Calculated Load, EGR, MAF,
             *          Intake Manifold Abs. Pressure, Vehicle Speed
             */
            String grpM[] = {"04", "2C", "10", "0B", "0D"};

            pidSpeedGroups.create(0, "fast", grpF);
            pidSpeedGroups.create(1, "medium", grpM);
        } else if (logProfile.equals("EGR")) {
            /* Fast - RPM, EGR, Accelerator pedal position D, EGR error */
            String grpF[] = {"0C", "2C", "49", "2D"};

            /*
             * Medium - Calculated Load, Rail Pressure, MAF,
             *          Intake Manifold Abs. Pressure, Vehicle Speed
             */
            String grpM[] = {"04", "23", "10", "0B", "0D"};

            pidSpeedGroups.create(0, "fast", grpF);
            pidSpeedGroups.create(1, "medium", grpM);
        } else if (logProfile.equals("THROTTLE")) {
            /*
             * Fast - RPM, Absolute Throttle Position,
             *        Accelerator pedal position D, Commanded Throttle Actuator
             */
            String grpF[] = {"0C", "11", "49", "4C"};

            /*
             * Medium - Calculated Load, Rail Pressure, MAF,
             *          Intake Manifold Abs. Pressure, Vehicle Speed
             */
            String grpM[] = {"04", "23", "10", "0B", "0D"};

            pidSpeedGroups.create(0, "fast", grpF);
            pidSpeedGroups.create(1, "medium", grpM);
        } else {
        /* 'TURBO' profile */
            /*
             * Fast - RPM, Intake Manifold Absolute Pressure,
             *        Accelerator pedal position D
             */
            String grpF[] = {"0C", "0B", "49"};

            /*
             * Medium - Calculated Load, Rail Pressure, MAF, EGR,
             *          Vehicle Speed
             */
            String grpM[] = {"04", "23", "10", "2C", "0D"};

            pidSpeedGroups.create(0, "fast", grpF);
            pidSpeedGroups.create(1, "medium", grpM);
        }

        /*
         * Slow - common to all profiles
         *        Water & Air Temperature, Barometric Pressure
         */
        String grpS[] = {"05", "0F", "33"};

        pidSpeedGroups.create(2, "slow", grpS);

        /* Save created, so that other states can use it as well. */
        setStateData("pidSpeedGroups", pidSpeedGroups);
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Obd obd = getObd();
        PicCom pc = obd.getPicCom();
        int[] v = new int[1];

        /*
         * If desired, capture battery voltage during starting
         * phase. But only if engine is not already running.
         * Test by measuring battery voltage.
         * If engine is running, charging is active and battery
         * voltage is higher comparing with idle phase.
         */
        if (getStateDataBoolean("captureStartVoltage")) {
            double batt;

            v[0] = pc.getAdValue("08");
            batt = v[0] * 0.01837;

            /* Update battery voltage on dashboard. */
            Probe battProbe = obd.getProbe("Vst");

            if (battProbe.update(v)) {
                Dashboard d = new Dashboard(getActivity());
                d.updateIndicator(battProbe);
            }

            /* Engine running */
            if (batt > 13.0) {
                setStateDataBoolean("captureStartVoltage", false);
                mLog.i("Engine running, skipping measuring starting voltage.");
            } else {
                mLog.i("Measuring of starting voltage initiated.");
            }
        }

        if (!pc.confBatteryMeasuring(getStateDataBoolean(
                "captureStartVoltage"))) {
            /* Failed to configure data acq module, re-try. */
            mLog.w("Failed to configure module for measuring battery voltage.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        /*
         * Generate list of probes to be auto-scanned by PIC module.
         */
        String pidAutoScanList = ((PidGroups)getStateData("pidSpeedGroups")).
                createAutoScanPidList(-1);

        /*
         * Configure and start OBD auto-scan mode. Set sample rate to 50 ms.
         */
        obd.configObdAutoScan(pidAutoScanList, "0032");

        return ComSvcStateAction.GOTO_NEXT;
    }

    /**
     * Constructor
     */
    public ComSvcStateConfAcqMode(ComSvcStateMachine stateMachine) {
        super(stateMachine);
    }
}

/**
 * Acquires probe values. Updates dashboard. Logs probes to log file.
 */
class ComSvcStateAcquireProbeValue extends ComSvcState {
    /* Dashboard */
    private Dashboard mDashboard;

    /* List of all measured probes. */
    private ArrayList<String> mPidLogList;

    /* PID which triggers logging. */
    private String mTriggerLogPid;

    /* OBD packet sequence number. */
    private int mObdSeqNum;

    /* Data acquisition packet sequence number. */
    private int mAcqSeqNum;

    /* Data acquisition buffer. */
    private String mAcqBuf;

    /**
     * Called when state is entered.
     */
    public void enter() {
        PidGroups pidSpeedGroups = (PidGroups)getStateData("pidSpeedGroups");
        /*
         * Generate list of all PIDs to be processed. Start with the highest
         * priority. The list is used for logging purposes.
         */
        mPidLogList = pidSpeedGroups.getAllPids();

        /* Initialize PID triggering log operation. */
        mTriggerLogPid = pidSpeedGroups.getTriggerLogPid();

        /*
         * Add calculated 'Turbo' to the list, so that it gets logged.
         */
        mPidLogList.add("Turbo");

	/*
         * If EGT sensors are to be logged, log converted values 
         * instead of raw OBD data.
         */
        if (mPidLogList.contains("78")) {
            mPidLogList.remove("78");

            mPidLogList.add("EGT11");
            mPidLogList.add("EGT12");
        }

        /*
         * Add battery voltage probe to the list, so that it gets logged.
         */
        mPidLogList.add("Vst");

        Log.d(ObdLogger.TAG, "PID log list:" + mPidLogList);

        /* Initialize packet sequence numbers. */
        mAcqSeqNum = 1;
        mObdSeqNum = 1;

        /* Initialize acquisition buffer */
        mAcqBuf = "";
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        int[] v;

        Obd obd = getObd();

        PicCom pc = obd.getPicCom();

        /* Wait for packet from PIC. */
        String p = pc.getPacket();

        /*
         * NAK response received from ECU indicates broken communication.
         * For now, just ignore those responses, as that may indicate
         * an intermittent problem.
         */
        if (p.startsWith(Obd.PKT_NAK)) {
            mLog.w(String.format("Communication with ECU broken, got <%s> NAK" +
                    " response.", p));

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        /* Probe value received. */
        if (p.startsWith(Obd.PKT_GET_CURR_PID_AS_RESP)) {
            /* Validate packet sequence number */
            int curSeqNum = Integer.parseInt(p.substring(2, 4), 16);

            /* If out of sync, log the warning and resync */
            if (curSeqNum != mObdSeqNum) {
                mLog.w(String.format("Bad PID sequence number (expected " +
                        "<%d>, got <%d>, resyncing...", mObdSeqNum, curSeqNum));

                mObdSeqNum = curSeqNum;
            }

            /* Update packet sequence number */
            if (++mObdSeqNum > 255) {
                mObdSeqNum = 1;
            }

            String currPid = p.substring(4, 6);
            Probe currProbe = obd.getProbe(currPid);

            /* extract OBD PID value from packet */
            int ln = (p.length() - 6) / 2;
            v = new int[ln];

            for (int i = 0; i < ln; i++) {
                v[i] = Integer.parseInt(p.substring(6 + (i * 2), 8 + (i * 2)),
                        16);
            }

            /* Update appropriate probe */
            if (currProbe.update(v)) {
                /* Update indicator with new value */
                mDashboard.updateIndicator(currProbe);
            }

            /*
             * Recalculate and update TurboIndicator if applicable.
             */
            TurboProbe turboPar = (TurboProbe) obd.getProbe("Turbo");

            if (turboPar.update(currPid, obd)) {
                mDashboard.updateIndicator(turboPar);
            }

            /* Recalculate and update EGT indicatiors if applicable. */
            EgtProbe egtPar = (EgtProbe) obd.getProbe("EGT11");
            if (egtPar.update(currPid, obd)) {
                mDashboard.updateIndicator(egtPar);
            }

            egtPar = (EgtProbe) obd.getProbe("EGT12");
            if (egtPar.update(currPid, obd)) {
                mDashboard.updateIndicator(egtPar);
            }

            /*
             * Log data into log file every time all PIDs
             * in the first group get processed.
             */
            /* TODO: Use some method for this check */
            if (currPid.equals(mTriggerLogPid)) {
                mLog.captureProbes(mPidLogList, obd);
            }
        } else if (p.startsWith("Q")) {
        /* Response from data acquisition module */
            /*
             * Response from acquisition module can be fragmented in which case
             * it is necessary to re-assemble the whole message.
             */
            int currSeqNum = Integer.parseInt(p.substring(1, 3), 16);

            /*
             * If sequence number gets out of sync, then probably
             * some fragments have been lost. Discard the whole
             * message in such case.
             * Zero means "last fragment", so handle it separately.
             */
            if ((currSeqNum != 0) && (currSeqNum != mAcqSeqNum)) {
                mLog.w(String.format("Bad Acq sequence number (expected " +
                        "<%d>, got <%d>, discarding the packet...",
                        mAcqSeqNum, currSeqNum));

                /*
                 * Stop processing fragments from data acquisition
                 * module.
                 */
                mAcqSeqNum = -1;
            } else {
                /* Append fragment at the end of the buffer. */
                mAcqBuf += p.substring(3);

                /*
                 * If this was not the last fragment,
                 * wait for another one.
                 */
                if (currSeqNum != 0) {
                    /* Update packet sequence number */
                    if (++mAcqSeqNum > 255) {
                        mAcqSeqNum = 1;
                    }

                    /* Wait for another packet. */
                    return ComSvcStateAction.STAY_IN_CURRENT;
                }
            }

            /*
             * Phase of measuring starting voltage done, log
             * acquired data to file and switch to periodic
             * acquisition of average battery voltage.
             */
            if (getStateDataBoolean("captureStartVoltage")) {
                if (mAcqSeqNum != -1) {
                    mLog.i("Measuring of starting voltage done.");

                    /* Log acquired data with sample time 10ms. */
                    mLog.logStartingVoltage(mAcqBuf, 0.01);
                } else {
                    mLog.i("Measuring of starting voltage failed.");
                }

                setStateDataBoolean("captureStartVoltage", false);

                /* Reconfigure acquisition module. */
                if (!pc.confBatteryMeasuring(false)) {
                    /* Failed to configure data acq module. */
                    mLog.w("Failed to re-configure module for measuring " +
                            "battery voltage.");
                }

                /* Wait for another packet. */
                return ComSvcStateAction.STAY_IN_CURRENT;
            }

            /*
             * We get here after measuring of starting voltage has been
             * done and after acquisition module was reconfigured
             * from oscilloscope mode to measure battery voltage.
             *
             * Sanity check of received data is done by validating
             * length of incoming packet.
             */
            if (p.length() != 59) {
                mLog.w("Invalid data acquisition packet received.");

                /* Discard the packet and wait for another one. */
                return ComSvcStateAction.STAY_IN_CURRENT;
            }

            /* Valid data packet received, process it. */
            String[] updatedPids = {"Vst", "Vrms", "Vmin", "Vmax",
                    "F1", "M1", "F2", "M2", "F3", "M3", "F4", "M4", "F5", "M5"};
            v = new int[1];

            for (int i = 0; i < updatedPids.length; i++) {
                String currPid = updatedPids[i];
                Probe currProbe = obd.getProbe(currPid);
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
        } else {
            mLog.w(String.format("Discarding unknown packet <%s>.", p));
        }

        /* Stay in current state by default. */
        return ComSvcStateAction.STAY_IN_CURRENT;
    }

    /**
     * Constructor
     */
    public ComSvcStateAcquireProbeValue(ComSvcStateMachine stateMachine) {
        super(stateMachine);

        /* Instantiate dashboard */
        mDashboard = new Dashboard(getActivity());
    }
}

/**
 * Diagnostic Live Data mode. Processes messages from UI, asks ECU
 * for required data and passes responses back to UI.
 */
class ComSvcStateDiagLiveData extends ComSvcState {
    /**
     * Called when state is entered.
     */
    public void enter() {
        Handler comMsgHandler = getComMsgHandler();
        /*
         * Let UI know that connection with ECU has been established
         * and we are ready to receive commands.
         */
        if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                ComSvcMsg.RES_ECU_CONNECTED))) {
            mLog.e("Sending message to UI failed.");
        }
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Handler comMsgHandler = getComMsgHandler();
        ComSvcMsg uiMsgHandler = getUiMsgHandler();

        Bundle msg;

        /* Block until UI asks us to do something. */
        try {
            mLog.d("Waiting for UI message...");
            msg = uiMsgHandler.get();
        } catch (InterruptedException e) {
            mLog.w("Communication thread interrupted when waiting " +
                    "for message from UI.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        /* Data payload empty, there is nothing to process. */
        if (msg == null) {
            mLog.w("EMPTY.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        mLog.d("RECEIVED.");

        /* Process the message. */
        int cmd = msg.getInt(ComSvcMsg.KEY_CMD, ComSvcMsg.CMD_INVALID);

        mLog.d(String.format("UI command: %d", cmd));

        switch (cmd) {
        /* Get list of supported PIDs. */
        case ComSvcMsg.CMD_GET_SUPPORTED_PIDS:
            ArrayList<String> pidList = null;

            mLog.d("Obtaining list of supported PIDs...");
            pidList = getObd().getSupportedPids();

            /* Pass the result to the UI. */
            if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                    ComSvcMsg.RES_GET_SUPPORTED_PIDS, pidList))) {
                mLog.e("Sending list of supported PIDs to UI failed.");
            }
            break;

        /* Get PID value. */
        case ComSvcMsg.CMD_GET_PID_VALUE:
            String pidId = msg.getString(ComSvcMsg.KEY_ARG1, "");

            if (pidId.equals("")) {
                mLog.w("PID ID not provided.");

                return ComSvcStateAction.STAY_IN_CURRENT;
            }

            Probe probe = getObd().getProbe(pidId);

            if (probe == null) {
                mLog.w("Probe for PID <" + pidId + "> not found.");

                return ComSvcStateAction.STAY_IN_CURRENT;
            }

            mLog.d("Obtaining value of PID <" + pidId + ">...");
            probe.acquire();

            /* Pass the result to the UI. */
            String resStr = " " + probe.getLabel() + ": " + probe.physToText();

            if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                    ComSvcMsg.RES_GET_PID_VALUE, resStr))) {
                mLog.e("Sending PID value to UI failed.");
            }

            break;

        /* Terminate the thread. */
        case ComSvcMsg.CMD_QUIT:
            return ComSvcStateAction.QUIT;

        case ComSvcMsg.CMD_INVALID:
            mLog.w("Invalid command.");
            break;
        }

        /* Continue in current state. */
        return ComSvcStateAction.STAY_IN_CURRENT;
    }

    /**
     * Constructor
     */
    public ComSvcStateDiagLiveData(ComSvcStateMachine stateMachine) {
        super(stateMachine);
    }
}

/**
 * Diagnostic Freeze Frame mode. Processes messages from UI, asks ECU
 * for required data and passes responses back to UI.
 */
class ComSvcStateDiagFreezeFrame extends ComSvcState {
    /* Progress dialog displayed when obtaining freeze frame. */
    private ProgressDialog mFreezeFramePd;

    /**
     * Called when state is entered.
     */
    public void enter() {
        Handler comMsgHandler = getComMsgHandler();
        /*
         * Let UI know that connection with ECU has been established
         * and we are ready to receive commands.
         */
        if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                ComSvcMsg.RES_ECU_CONNECTED))) {
            mLog.e("Sending message to UI failed.");
        }
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Handler comMsgHandler = getComMsgHandler();
        ComSvcMsg uiMsgHandler = getUiMsgHandler();

        Bundle msg;

        /* Block until UI asks us to do something. */
        try {
            mLog.d("Waiting for UI message...");
            msg = uiMsgHandler.get();
        } catch (InterruptedException e) {
            mLog.w("Communication thread interrupted when waiting " +
                    "for message from UI.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        /* Data payload empty, there is nothing to process. */
        if (msg == null) {
            mLog.w("EMPTY.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        mLog.d("RECEIVED.");

        /* Get command. */
        int cmd = msg.getInt(ComSvcMsg.KEY_CMD, ComSvcMsg.CMD_INVALID);

        mLog.d(String.format("UI command: %d", cmd));

        /* Process the message. */
        switch (cmd) {
        /* Get list of logged freeze frames. */
        case ComSvcMsg.CMD_GET_FF_LIST:
            ArrayList<String> freezeFrameList;

            mLog.d("Obtaining list of logged freeze frames...");

            /*
             * Get list of logged freeze frames. It is in form of array
             * of Strings where each String contains DTC number identifying
             * particular freeze frame.
             */
            freezeFrameList = getObd().getFreezeFrameList();

            /* Pass the result to the UI. */
            if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                    ComSvcMsg.RES_GET_FF_LIST, freezeFrameList))) {
                mLog.e("Sending list of freeze frames to UI failed.");
            }

            break;

        /* Get list of PIDs along with their values for chosen freeze frame. */
        case ComSvcMsg.CMD_GET_FF_PIDS:
            /* Get freeze frame number. */
            int freezeFrameId = msg.getInt(ComSvcMsg.KEY_ARG1, 0);

            ArrayList<String> pidList = null;
            ArrayList<String> pidValues = new ArrayList<String>();

            /* Display progress dialog. */
            getActivity().runOnUiThread(new Runnable() {
                public void run() {
                    mFreezeFramePd.show();
                }
            });

            mLog.d("Obtaining list of freeze frame PIDs...");
            pidList = getObd().getFreezeFramePids(freezeFrameId);

            /*
             * Obtain value for each PID, construct string containing
             * PID label, PID value, PID units and put that string
             * into the list.
             */

            for (int i = 0; i < pidList.size(); i++) {
                String pidId = pidList.get(i);

                ObdProbe probe = (ObdProbe) getObd().getProbe(pidId);

                if (probe == null) {
                    mLog.w("Probe for PID <" + pidId + "> not found.");

                    continue;
                }

                mLog.d("Getting value of freeze frame PID <" + pidId + ">...");
                probe.acquire(freezeFrameId);

                String resStr = " " + probe.getLabel() + ": " +
                        probe.physToText();

                pidValues.add(resStr);
            }

            if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                    ComSvcMsg.RES_GET_FF_PIDS, pidValues))) {
                mLog.e("Sending list of freeze frame PID values to UI failed.");
            }

            /* Dismiss progress dialog. */
            getActivity().runOnUiThread(new Runnable() {
                public void run() {
                    mFreezeFramePd.dismiss();
                }
            });

            break;

        /* Terminate the thread. */
        case ComSvcMsg.CMD_QUIT:
            return ComSvcStateAction.QUIT;

        case ComSvcMsg.CMD_INVALID:
            mLog.w("Invalid command.");
            break;
        }

        /* Continue in current state. */
        return ComSvcStateAction.STAY_IN_CURRENT;
    }

    /**
     * Constructor
     */
    public ComSvcStateDiagFreezeFrame(ComSvcStateMachine stateMachine) {
        super(stateMachine);

        /* Create progress dialog displayed when obtaining freeze frame. */
        mFreezeFramePd = new ProgressDialog(getActivity());
        mFreezeFramePd.setTitle("Obtaining freeze frame...");
        mFreezeFramePd.setMessage("Please wait.");
        mFreezeFramePd.setCancelable(false);
        mFreezeFramePd.setIndeterminate(true);
    }
}

/**
 * Diagnostic 'Read permanent DTCs' mode. Processes messages from UI, asks ECU
 * for required data and passes responses back to UI.
 */
class ComSvcStateDiagPermDtc extends ComSvcState {
    /* Progress dialog displayed when reading permanent DTCs. */
    private ProgressDialog mPermDtcPd;

    /**
     * Called when state is entered.
     */
    public void enter() {
        Handler comMsgHandler = getComMsgHandler();
        /*
         * Let UI know that connection with ECU has been established
         * and we are ready to receive commands.
         */
        if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                ComSvcMsg.RES_ECU_CONNECTED))) {
            mLog.e("Sending message to UI failed.");
        }
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Handler comMsgHandler = getComMsgHandler();
        ComSvcMsg uiMsgHandler = getUiMsgHandler();

        Bundle msg;

        /* Block until UI asks us to do something. */
        try {
            mLog.d("Waiting for UI message...");
            msg = uiMsgHandler.get();
        } catch (InterruptedException e) {
            mLog.w("Communication thread interrupted when waiting " +
                    "for message from UI.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        /* Data payload empty, there is nothing to process. */
        if (msg == null) {
            mLog.w("EMPTY.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        mLog.d("RECEIVED.");

        /* Get command. */
        int cmd = msg.getInt(ComSvcMsg.KEY_CMD, ComSvcMsg.CMD_INVALID);

        mLog.d(String.format("UI command: %d", cmd));

        /* Process the message. */
        switch (cmd) {
        /* Get list of logged permanent DTCs. */
        case ComSvcMsg.CMD_GET_PDTC_LIST:
            int dtcTotalNum;
            ArrayList<String> dtcList = new ArrayList<String>();

            mLog.d("Obtaining list of logged permanent DTCs...");

            /* Get number of logged permanent DTCs. */
            dtcTotalNum = getObd().getPermDtcNum();

            /* No DTC logged, return empty list. */
            if (dtcTotalNum == 0) {
                /* Pass the result to the UI. */
                if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                        ComSvcMsg.RES_GET_PDTC_LIST, dtcList))) {
                    mLog.e("Sending list of permanent DTCs to UI failed.");
                }

                break;
            }

            /* Display progress dialog. */
            getActivity().runOnUiThread(new Runnable() {
                public void run() {
                    mPermDtcPd.show();
                }
            });

            /*
             * Get list of logged permanent DTCs. It is in form of array
             * of Strings where each String contains DTC number.
             */
            for (int i = 0; i < dtcTotalNum; i++) {
                String dtc = getObd().getPermDtc(i);

                /* Failure occurred, return empty list. */
                if (dtc == null ) {
                    mLog.e("Failed to construct list of permanent DTCs.");

                    dtcList = new ArrayList<String>();

                    break;
                }

                /* Add DTC to the list. */
                dtcList.add(dtc);
            }

            /* Dismiss progress dialog. */
            getActivity().runOnUiThread(new Runnable() {
                public void run() {
                    mPermDtcPd.dismiss();
                }
            });

            /* Pass the result to the UI. */
            if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                    ComSvcMsg.RES_GET_PDTC_LIST, dtcList))) {
                mLog.e("Sending list of permanent DTCs to UI failed.");
            }

            break;

        /* Terminate the thread. */
        case ComSvcMsg.CMD_QUIT:
            return ComSvcStateAction.QUIT;

        case ComSvcMsg.CMD_INVALID:
            mLog.w("Invalid command.");
            break;
        }

        /* Continue in current state. */
        return ComSvcStateAction.STAY_IN_CURRENT;
    }

    /**
     * Constructor
     */
    public ComSvcStateDiagPermDtc(ComSvcStateMachine stateMachine) {
        super(stateMachine);

        /* Create progress dialog displayed when obtaining DTCs. */
        mPermDtcPd = new ProgressDialog(getActivity());
        mPermDtcPd.setTitle("Obtaining list of DTCs...");
        mPermDtcPd.setMessage("Please wait.");
        mPermDtcPd.setCancelable(false);
        mPermDtcPd.setIndeterminate(true);
    }
}

/**
 * Diagnostic 'Clear DTCs' mode. Processes messages from UI, asks ECU
 * for required data and passes responses back to UI.
 */
class ComSvcStateDiagClearDtc extends ComSvcState {
    /**
     * Called when state is entered.
     */
    public void enter() {
        Handler comMsgHandler = getComMsgHandler();
        /*
         * Let UI know that connection with ECU has been established
         * and we are ready to receive commands.
         */
        if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                ComSvcMsg.RES_ECU_CONNECTED))) {
            mLog.e("Sending message to UI failed.");
        }
    }

    /**
     * Called when state is left.
     */
    public void leave() {
    }

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        Handler comMsgHandler = getComMsgHandler();
        ComSvcMsg uiMsgHandler = getUiMsgHandler();

        Bundle msg;

        /* Block until UI asks us to do something. */
        try {
            mLog.d("Waiting for UI message...");
            msg = uiMsgHandler.get();
        } catch (InterruptedException e) {
            mLog.w("Communication thread interrupted when waiting " +
                    "for message from UI.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        /* Data payload empty, there is nothing to process. */
        if (msg == null) {
            mLog.w("EMPTY.");

            return ComSvcStateAction.STAY_IN_CURRENT;
        }

        mLog.d("RECEIVED.");

        /* Get command. */
        int cmd = msg.getInt(ComSvcMsg.KEY_CMD, ComSvcMsg.CMD_INVALID);

        mLog.d(String.format("UI command: %d", cmd));

        /* Process the message. */
        switch (cmd) {
        /* Get total number of permanent DTCs. */
        case ComSvcMsg.CMD_GET_PDTC_NUM:
            int dtcTotalNum;

            /* Get number of logged permanent DTCs and return it to the UI. */
            dtcTotalNum = getObd().getPermDtcNum();

            if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                    ComSvcMsg.RES_GET_PDTC_NUM, dtcTotalNum, 0))) {
                mLog.e("Sending number of permanent DTCs to UI failed.");
            }

            break;

        /* Clear DTCs, freeze frame data, ... */
        case ComSvcMsg.CMD_CLEAR_DTC:
            boolean res = getObd().clearDtc();

            /* If operation failed, log that. */
            if (!res) {
                mLog.e("ECU refused to clear DTCs.");
            }

            /* Pass the result to the UI. */
            if (!comMsgHandler.sendMessage(comMsgHandler.obtainMessage(
                    ComSvcMsg.RES_CLEAR_DTC, res ? 0 : -1))) {
                mLog.e("Sending result of 'clear DTC' operation to UI failed.");
            }

            break;

        /* Terminate the thread. */
        case ComSvcMsg.CMD_QUIT:
            return ComSvcStateAction.QUIT;

        case ComSvcMsg.CMD_INVALID:
            mLog.w("Invalid command.");
            break;
        }

        /* Continue in current state. */
        return ComSvcStateAction.STAY_IN_CURRENT;
    }

    /**
     * Constructor
     */
    public ComSvcStateDiagClearDtc(ComSvcStateMachine stateMachine) {
        super(stateMachine);
    }
}

/**
 * Implements state machine handling data logger screen.
 */
class ComSvcLoggerStateMachine extends ComSvcStateMachine {
    /**
     * Constructor
     *
     * @param activity  the reference to activity which spawned this service
     * @param obd       the OBD communication object
     */
    public ComSvcLoggerStateMachine(Activity activity, Obd obd) {
        super(activity, obd);

        /* Instantiate states and create state diagram. */

        /* Initiates communication with PIC module. */
        addState(new ComSvcStateConnectToPic(this));
        /* Configures PIC for communication with ECU. */
        addState(new ComSvcStateConfEcuComMode(this));
        /* Initiates communication with ECU. */
        addState(new ComSvcStateConnectToEcu(this));
        /*
         * Configures OBD auto-scan mode and initiates measuring
         * of starting voltage.
         */
        addState(new ComSvcStateConfAcqMode(this));
        /* Acquires probe value, updates dashboard and logs it. */
        addState(new ComSvcStateAcquireProbeValue(this));

        /* Initialize state machine. */
        goToFirstState();
    }
}

/**
 * Implements state machine handling 'diagnostic live data' mode.
 */
class ComSvcDiagLiveDataStateMachine extends ComSvcStateMachine {
    /**
     * Constructor
     *
     * @param activity  the reference to activity which spawned this service
     * @param comMsgH   the message handler delivering messages to UI thread
     * @param uiMsgH    the message handler delivering messages from UI thread
     * @param obd       the OBD communication object
     */
    public ComSvcDiagLiveDataStateMachine(Activity activity,
            Handler comMsgH,ComSvcMsg uiMsgH, Obd obd) {
        super(activity, obd, comMsgH, uiMsgH);

        /* Instantiate states and create state diagram. */

        /* Initiates communication with PIC module. */
        addState(new ComSvcStateConnectToPic(this));
        /* Configures PIC for communication with ECU. */
        addState(new ComSvcStateConfEcuComMode(this));
        /* Initiates communication with ECU. */
        addState(new ComSvcStateConnectToEcu(this));
        /* Receives commands from UI and passes back data obtained from ECU. */
        addState(new ComSvcStateDiagLiveData(this));

        /* Initialize state machine. */
        goToFirstState();
    }
}

/**
 * Implements state machine handling 'diagnostic freeze frame' mode.
 */
class ComSvcDiagFreezeFrameStateMachine extends ComSvcStateMachine {
    /**
     * Constructor
     *
     * @param activity  the reference to activity which spawned this service
     * @param comMsgH   the message handler delivering messages to UI thread
     * @param uiMsgH    the message handler delivering messages from UI thread
     * @param obd       the OBD communication object
     */
    public ComSvcDiagFreezeFrameStateMachine(Activity activity,
            Handler comMsgH,ComSvcMsg uiMsgH, Obd obd) {
        super(activity, obd, comMsgH, uiMsgH);

        /* Instantiate states and create state diagram. */

        /* Initiates communication with PIC module. */
        addState(new ComSvcStateConnectToPic(this));
        /* Configures PIC for communication with ECU. */
        addState(new ComSvcStateConfEcuComMode(this));
        /* Initiates communication with ECU. */
        addState(new ComSvcStateConnectToEcu(this));
        /* Receives commands from UI and passes back data obtained from ECU. */
        addState(new ComSvcStateDiagFreezeFrame(this));

        /* Initialize state machine. */
        goToFirstState();
    }
}

/**
 * Implements state machine handling 'diagnostic permanent DTCs' mode.
 */
class ComSvcDiagPermDtcStateMachine extends ComSvcStateMachine {
    /**
     * Constructor
     *
     * @param activity  the reference to activity which spawned this service
     * @param comMsgH   the message handler delivering messages to UI thread
     * @param uiMsgH    the message handler delivering messages from UI thread
     * @param obd       the OBD communication object
     */
    public ComSvcDiagPermDtcStateMachine(Activity activity,
            Handler comMsgH,ComSvcMsg uiMsgH, Obd obd) {
        super(activity, obd, comMsgH, uiMsgH);

        /* Instantiate states and create state diagram. */

        /* Initiates communication with PIC module. */
        addState(new ComSvcStateConnectToPic(this));
        /* Configures PIC for communication with ECU. */
        addState(new ComSvcStateConfEcuComMode(this));
        /* Initiates communication with ECU. */
        addState(new ComSvcStateConnectToEcu(this));
        /* Receives commands from UI and passes back data obtained from ECU. */
        addState(new ComSvcStateDiagPermDtc(this));

        /* Initialize state machine. */
        goToFirstState();
    }
}

/**
 * Implements state machine handling 'clear DTCs' mode.
 */
class ComSvcDiagClearDtcStateMachine extends ComSvcStateMachine {
    /**
     * Constructor
     *
     * @param activity  the reference to activity which spawned this service
     * @param comMsgH   the message handler delivering messages to UI thread
     * @param uiMsgH    the message handler delivering messages from UI thread
     * @param obd       the OBD communication object
     */
    public ComSvcDiagClearDtcStateMachine(Activity activity,
            Handler comMsgH,ComSvcMsg uiMsgH, Obd obd) {
        super(activity, obd, comMsgH, uiMsgH);

        /* Instantiate states and create state diagram. */

        /* Initiates communication with PIC module. */
        addState(new ComSvcStateConnectToPic(this));
        /* Configures PIC for communication with ECU. */
        addState(new ComSvcStateConfEcuComMode(this));
        /* Initiates communication with ECU. */
        addState(new ComSvcStateConnectToEcu(this));
        /* Receives commands from UI and passes back data obtained from ECU. */
        addState(new ComSvcStateDiagClearDtc(this));

        /* Initialize state machine. */
        goToFirstState();
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
    /* OBD diagnostic tester - live data. */
    public static final int MODE_LIVE_DATA = 1;
    /* OBD diagnostic tester - freeze frame data. */
    public static final int MODE_FREEZE_FRAME = 2;
    /* OBD diagnostic tester - permanent DTCs. */
    public static final int MODE_PERM_DTC = 3;
    /* OBD diagnostic tester - clear DTCs. */
    public static final int MODE_CLEAR_DTC = 4;

    private boolean mDebug = false;
    private boolean mQuit = false;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* State machine taking care of communication with ECU and UI. */
    private ComSvcStateMachine mComSvcStateMachine;

    /* OBD communication module. */
    private Obd mObd;

    /* GPS status. */
    private Button mStsGps;

    /**
     * Used by UI thread to signal communication thread
     * that it should terminate.
     */
    public void finish() {
        mQuit = true;
    }

    /**
     * Implements communication thread running in background.
     */
    public Runnable loop = new Runnable() {
        public void run() {
            /*
             * Run the service until either user decides to terminate
             * the current activity (which is signaled via mQuit)
             * or until state machine reaches its final state
             * (e.g. when it can't connect to Bluetooth module.
             */
            while (!mQuit && mComSvcStateMachine.runStateMachine());

            /*
             * Let the UI know if the decision to terminate the current
             * activity came from the state machine.
             */
            if (!mQuit) {
                final Activity a = (Activity)mComSvcStateMachine.getStateData(
                        "activity");

                a.runOnUiThread(new Runnable() {
                    public void run() {
                        a.finish();
                    }
                });
            }

            /*
             * Clean up things.
             */
            PicCom pc = mObd.getPicCom();

            if (pc.connectionEstablished()) {
                try {
                    pc.stopAcq();
                } catch (PicTimeoutExpiredException e) {
                    /* Timeout expired. */
                    mLog.w("Failed to stop acquisition module.");
                } catch (PicConnectionLostException e) {
                    /* Connection with PIC lost. */
                    mLog.w("Failed to stop acquisition module.");
                }

                if (mObd.connectionEstablished()) {
                    /* Close ECU communication channel. */
                    if (!mObd.close())
                        mLog.w("Failed to close ECU communication channel.");
                }

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
        /* Save reference to Obd communication engine. */
        mObd = obd;

        /* Instantiates state machine for given mode. */
        if (mode == MODE_LOGGER) {
            mComSvcStateMachine = new ComSvcLoggerStateMachine(activity, obd);
        } else if (mode == MODE_LIVE_DATA) {
            mComSvcStateMachine = new ComSvcDiagLiveDataStateMachine(activity,
                    handler, uiMsg, obd);
        } else if (mode == MODE_FREEZE_FRAME) {
            mComSvcStateMachine = new ComSvcDiagFreezeFrameStateMachine(
                    activity, handler, uiMsg, obd);
        } else if (mode == MODE_PERM_DTC) {
            mComSvcStateMachine = new ComSvcDiagPermDtcStateMachine(
                    activity, handler, uiMsg, obd);
        } else {
            mComSvcStateMachine = new ComSvcDiagClearDtcStateMachine(
                    activity, handler, uiMsg, obd);
        }

        /* Initialize reference to GPS status indicator. */
        mStsGps = (Button) activity.findViewById(R.id.gpsState);
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
     * Called when connection to PIC module is closed.
     */
    public void onConnectionLost() {
        mLog.i("Connection to PIC closed.");

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
 * Callback methods called when communication with ECU is established
 * or lost when operating in 'data logger' mode.
 */
class EcuComCallbackLogger implements IEcuComCallback {
    /* Reference to parent Activity */
    private Activity mActivity;

    /* Reference to Ecu status button */
    private Button mStsEcu;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

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
     * Called when connection with ECU is established.
     */
    public void onConnectionEstablished() {
        mLog.i("Connection with ECU established.");

        updateStatusIndicator(mStsEcu, true);
    }

    /**
     * Called when connection to ECU module is closed.
     */
    public void onConnectionLost() {
        mLog.i("Connection to ECU closed.");

        updateStatusIndicator(mStsEcu, false);
    }

    /**
     * Constructor
     *
     * @param activity     the reference to parent Activity
     */
    public EcuComCallbackLogger(Activity activity) {
        mActivity = activity;

        mStsEcu = (Button) mActivity.findViewById(R.id.ecuState);
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
        mDashboardIndicators = new DashboardIndicator[8];
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
        /* EGT11 */
        mDashboardIndicators[5] = new DashboardIndicator(activity,
                R.id.indicator6Label, R.id.indicator6Value, 0, "EGT11");
        /* EGT12 */
        mDashboardIndicators[6] = new DashboardIndicator(activity,
                R.id.indicator7Label, R.id.indicator7Value, 0, "EGT12");
        /* Batt */
        mDashboardIndicators[7] = new DashboardIndicator(activity,
                R.id.indicator8Label, R.id.indicator8Value, 0, "Vst");
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

    /**
     * Returns PID which triggers generating log record (last PID
     * in "fast" PID group).
     */
    public String getTriggerLogPid() {
        ArrayList<String> pidList = mPidGroups.get(0).getAllPids();
        return pidList.get(pidList.size() - 1);
    }

    /**
     * Constructor
     */
    public PidGroups() {
        mPidGroups = new ArrayList<PidGroup>();

        mGroupIndex = 0;
    }
}

