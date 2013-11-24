package cz.tio.android.obdlogger;

import android.app.Activity;
import android.app.ProgressDialog;
import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.HashMap;

import cz.tio.android.obdlogger.EditPreferences;
import cz.tio.android.obdlogger.IPicComCallback;
import cz.tio.android.obdlogger.PicCom;
import cz.tio.android.obdlogger.PicComBt;

import cz.tio.android.obdlogger.PicConnectionLostException;
import cz.tio.android.obdlogger.PicTimeoutExpiredException;

/**
 * Abstract model of measurement probe as a source of measured
 * or calculated physical quantity. The class carries following attributes:
 *
 *   mId - unique probe ID
 *   mLabel - short label (displayed on dashboard and in log file)
 *   mDesc - description string
 *   mFormatString - format string used to convert parameter value to text
 *                   which can be displayed
 *   mPhysToPbAdd, mPhysToPbMul   - additive and multiplicative constant used
 *                                  when converting physical value
                                    to progress bar fraction
 *   mUnits - string holding physical units
 *   mCurrY - last acquired value
 *   mCurrYValid - true if last acquired value is valid
 */
abstract class Probe {
    /* Probe unique ID */
    String mId;

    /* last acquired value */
    double mCurrY = 0.0;

    /* indicates that mCurrY contains valid information */
    boolean mCurrYValid = false;

    /* private attributes */
    /* Label - displayed on "dashboard" and in log file - e.g. PRESSURE */
    private String mLabel = "";

    /* Description - e.g. "Atmospheric pressure" */
    private String mDesc = "";

    /* Physical units - e.g. MPa */
    private String mUnits = "-";

    /*
     * Format string used when physical value is converted to text string -
     * e.g. for purposes of displaying the text on dashboard or logging it.
     */
    private String mFormatString = "%f";

    /*
     * Additive and multiplicative constant used for conversion
     * of physical value to progress bar fraction.
     */
    private double mPhysToPbAdd = 0.0;
    private double mPhysToPbMul = 1.0;

    /**
     * Constructor
     */
    public Probe(String id, String units, String desc, String label, 
        String formatString, double pbAdd, double pbMul) {

        mId = id;
        mDesc = desc;
        mLabel = label;
        mFormatString = formatString;
        mUnits = units;

        mPhysToPbAdd = pbAdd;
        mPhysToPbMul = pbMul;
    }

    /**
     * Updates probe value. Returns true in case of success. Otherwise
     * returns false.
     */
    public boolean update(int[] value) {
        return true;
    }

    /**
     * Asks for probe value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public boolean acquire()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        return true;
    }

    /**
     * Returns probe ID.
     */
    public String getId() {
        return mId;
    }

    /**
     * Returns label.
     */
    public String getLabel() {
        return mLabel;
    }

    /**
     * Returns units.
     */
    public String getUnits() {
        return mUnits;
    }

    /**
     * Returns true if last acquired value is valid. Otherwise returns false.
     */
    public boolean currYIsValid() {
        return mCurrYValid;
    }

    /**
     * Returns last acquired value.
     */
    public double getCurrY() {
        return mCurrY;
    }

    /**
     * Converts physical value to text which is to be displayed on "dashboard".
     * Appends physical units if there are any. Replaces decimal point
     * with decimal comma.
     */
    public String physToText() {
        if (!mCurrYValid) {
            return "";
        }

        StringBuffer s = new StringBuffer(String.format(mFormatString, mCurrY));

        if (!mUnits.equals("-")) {
            s.append(" " + mUnits);
        }

        if (s.indexOf(".") != -1) {
            s.setCharAt(s.indexOf("."), ',');
        }

        return s.toString();
    }

    /**
     * Converts PID value to text which is to be logged to file. Replaces
     * decimal comma with decimal point.
     */
    public String physToLog() {
        if (!mCurrYValid) {
            return "";
        }

        StringBuffer s = new StringBuffer(String.format(mFormatString, mCurrY));

        if (s.indexOf(",") != -1) {
            s.setCharAt(s.indexOf(","), '.');
        }

        return s.toString();
    }

    /**
     * Converts parameter value to fraction displayed via progress bar
     * on dashboard.
     */
    public double physToPbFraction() {
        if (!mCurrYValid) {
            return 0.0;
        }

        return (mCurrY + mPhysToPbAdd) * mPhysToPbMul;
    }
}

/**
 * Represents PIC probe acquired directly by PIC module. Extends Probe class.
 * Adds following attributes:
 *
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 *   mPicCom - reference to PicCom object providing methods for communication
 *             with PIC module.
 */
class PicProbe extends Probe {
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;
    private PicCom mPicCom = null;

    /**
     * Constructor
     */
    public PicProbe(String pid, String units, String desc, String label,
            String formatString, double kAdd, double kMul,
            double pbAdd, double pbMul, PicCom picCom) {

        /* Initialize attributes of parent class */
        super(pid, units, desc, label, formatString, pbAdd, pbMul);

        mRawToPhysAdd = kAdd;
        mRawToPhysMul = kMul;

        mPicCom = picCom;
    }

    /**
     * Updates probe value. Returns true in case of success. Otherwise
     * returns false.
     */
    public boolean update(int[] value) {
        mCurrY = (value[0] + mRawToPhysAdd) * mRawToPhysMul;

        mCurrYValid = true;

        return true;
    }

    /**
     * Asks PIC for probe value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public boolean acquire()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        int[] picAdValue = new int[1];

        try {
            picAdValue[0] = mPicCom.getAdValue("08");
        } catch (PicTimeoutExpiredException e) {
            mCurrYValid = false;

            throw e;
        } catch (PicConnectionLostException e) {
            mCurrYValid = false;

            throw e;
        } 

        return this.update(picAdValue);
    }
}

/**
 * Represents OBD probe measured by ECU and obtained via diagnostic
 * communication protocol. Extends Probe class. Adds following attributes:
 *
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 *   mObd - reference to Obd object providing methods for communication
 *          with ECU.
 */
class ObdProbe extends Probe {
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;
    private Obd mObd = null;

    /**
     * Constructor
     */
    public ObdProbe(String pid, String units, String desc, String label,
            String formatString, double kAdd, double kMul,
            double pbAdd, double pbMul, Obd obd) {

        /* Initialize attributes of parent class */
        super(pid, units, desc, label, formatString, pbAdd, pbMul);

        mRawToPhysAdd = kAdd;
        mRawToPhysMul = kMul;

        mObd = obd;
    }

    /**
     * Updates probe value. Returns true in case of success. Otherwise
     * returns false.
     */
    public boolean update(int[] value) {
        /**
         * Converts PID data returned by ECU in form of an array of bytes
         * to physical value using following formula:
         *
         *   mCurrY = (mRawY + mRawToPhysAdd) * mRawToPhysMul,
         *
         *   where mRawY is summary of weighted bytes returned from ECU.
         *   For instance, if ECU returned two bytes, mRawY would be
         *   calculated as:
         *
         *   mRawY = 256 * value[0] + value[1]
         */
        int mRawY = 0;
    
        for (int d : value) {
            mRawY *= 256;
            mRawY += d;
        }

        mCurrY = (mRawY + mRawToPhysAdd) * mRawToPhysMul;

        mCurrYValid = true;

        return true;
    }

    /**
     * Obtains value of OBD parameter from ECU.
     * Updates probe with new value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public boolean acquire()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        int[] ecuData;

        try {
            ecuData = mObd.getPidValue(mId);
        } catch (PicTimeoutExpiredException e) {
            mCurrYValid = false;

            throw e;
        } catch (PicConnectionLostException e) {
            mCurrYValid = false;

            throw e;
        } 

        return this.update(ecuData);
    }
}

/**
 * Represents Turbo Indicator, extends Probe class . It is calculated
 * as Intake Pressure/Barometric Pressure ratio. It adds following attributes:
 *
 *   mProbeList - list of probes used to calculate the value.
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 */
class TurboProbe extends Probe {
    private String[] mProbeList = {"0B", "33"};
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;

    /**
     * Constructor
     */
    public TurboProbe() {
        /* Initialize attributes of parent class */
        super("Turbo", "-", "Turbo Indicator", "TURBO", "%.2f",
                -0.7, 1.0 / (2.0 - 0.7));

        mRawToPhysAdd = 0.0;
        mRawToPhysMul = 1.0;
    }

    /**
     * Recalculates value if given PID contributes to the formula.
     * Returns true if value has been recalculated. Otherwise returns false.
     */
    public boolean update(String pid,
            HashMap<String, Probe> probePool) {
        for (String s : mProbeList) {
            if (pid.equals(s)) {
                if (probePool.get("0B").currYIsValid() &&
                        probePool.get("33").currYIsValid() &&
                        probePool.get("33").getCurrY() != 0) {
                    mCurrY = probePool.get("0B").getCurrY() /
                            probePool.get("33").getCurrY();

                    mCurrYValid = true;

                    return true;
                }

                return false;
            }
        }
        return false;
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
//                R.id.indicator2Label, R.id.indicator2Value, 0, "Turbo");
                R.id.indicator2Label, R.id.indicator2Value, 0, "04");
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
 * Implements KWP2000 communication protocol.
 */
class Kwp2000 {
    /* Reference to lower communication layer */
    public PicCom mPicCom;

    /* Debug mode */
    private boolean mDebug = false;

    /**
     * Constructor
     */
    public Kwp2000(PicCom picCom) {
        /* Instantiate lower layer communication object. */
        mPicCom = picCom;
    }

    /**
     * Assembles and sends out KWP2000 packet.
     *
     * @return false if attempt to send the packet fails. Otherwise return true.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public void sendPacket(String p)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        if (mDebug) {
            Log.d(ObdLogger.TAG, "<- " + p);
        }

        mPicCom.sendPacket("S" + p);
    }

    /**
     * Waits for KWP2000 packet from PIC module.
     *
     * @return   received packet
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    public String recvPacket()
            throws PicConnectionLostException {
        String p = null;

        while (true) {
            try {
                p = mPicCom.getPacket();
            } catch (PicTimeoutExpiredException e) {
                /* Timeout expired, will re-try. */
            } catch (PicConnectionLostException e) {
                /* Connection with PIC lost, there is nothing, we can do. */
                throw e;
            }

            /*
             * Wait for either valid KWP packet or NAK response.
             * Discard other packets.
             */
            if (p.startsWith("Mr")) {
                if (mDebug) {
                    Log.d(ObdLogger.TAG, "-> " + p);
                }

                return p;
            }

            if (p.startsWith("MN")) {
                Log.e(ObdLogger.TAG, String.format("[%.3f] NAK received: %s",
                    ObdLogger.getRunTime(), p));

                return p;
            }
        }
    }
}

/**
 * Implements OBD communication protocol.
 */
class Obd {
    /* Reference to lower communication layer */
    public Kwp2000 mKwp2000;

    static final String PKT_ACK = "EA";
    static final String PKT_NAK = "EN";
    static final String PKT_GET_PID = "EOP";
    static final String PKT_GET_PID_RESP = "E00P";

    /* private attributes */
    private static final int ST_COMM_INIT = 0;
    private static final int ST_GET_PID = 1;

    /* Reference to lower communication layer */
    private PicCom mPicCom;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* debug mode */
    private boolean mDebug = false;

    /* indicates if communication with ECU is established */
    private boolean mComEcuEstablished = false;

    /**
     * Establishes communication with ECU.
     *
     * @return  <code>true</code> if communication has been established;
     *          <code>false</code> otherwise.
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    public boolean open() throws PicConnectionLostException {
        int[] p;

        /* If communication channel is already established, return. */
        if (mComEcuEstablished) {
            return true;
        }

        /*
         * To establish communication with ECU, send 'get pid 00' packet
         * to ECU. Low level part of that handshake is handled by PIC.
         * Let receive state machine process a response.
         */
        try {
            getPidValue("00");
        } catch (PicTimeoutExpiredException e) {
            mLog.e("Timeout when trying to initiate communication with ECU.");

            return false;
        } catch (PicConnectionLostException e) {
            mLog.e("Lost connection with PIC when trying to initiate " +
                    "communication with ECU.");

            throw e;
        }

        /* Communication established, set appropriate flag and return. */
        mLog.i("Connection with ECU established.");

        mComEcuEstablished = true;

        return true;
    }

    /**
     * Finishes communication with ECU.
     * Returns true in case of success, otherwise returns false.
     */
    public boolean close() {
        /* Assume success. */
        boolean ret = true;
        int i = 0;

        /* Turn off auto-scan mode. */
        if (mComEcuEstablished) {
            try {
                mPicCom.configObdAutoScan("");
            } catch (PicTimeoutExpiredException e) {
                ret = false;
            } catch (PicConnectionLostException e) {
                ret = false;
            }
        }

        mComEcuEstablished = false;

        /*
         * Discard buffered packets, as these all belong to communication
         * session which is now being closed. Waits for finite number
         * of packets, so that the method does not hang here in case
         * auto-scan mode has not been successfully canceled.
         */
        try {
            for (i = 0; i < 100; i++) {
                mPicCom.getPacket();
            }
        } catch (PicTimeoutExpiredException e) {
            /* Buffer successfully emptied. */
        } catch (PicConnectionLostException e) {
            /* Connection with PIC lost. */
            ret = false;
        }

        /* Failed to clean up the buffer. */
        if (i == 100)
            ret = false;

        return ret;
    }

    /**
     * Asks ECU for PID value.
     *
     * @return   PID value in form of byte array
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int[] getPidValue(String pidId)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        String sp = PKT_GET_PID + pidId;
        String rp = null;

        /* Ask ECU for PID value. */
        mPicCom.sendPacket(sp);

        /*
         * Loop until valid packet is received or
         * until receive buffer is emptied.
         */
        while (true) {
            try {
                /* Wait for response. */
                rp = mPicCom.getPacket("E");
            } catch (PicTimeoutExpiredException e) {
                /*
                 * Timeout expired. Communication needs to be established again.
                 */
                mLog.w("Timeout when waiting for ECU response.");

                mComEcuEstablished = false;

                throw e;
            } catch (PicConnectionLostException e) {
                mComEcuEstablished = false;

                throw e;
            }

            /*
             * NAK received, log it and wait for another packet, since
             * it may not be our NAK.
             */
            if (rp.startsWith(PKT_NAK)) {
                mLog.w(String.format(
                        "ECU responded with NAK <%s> to <%s> packet.", rp, sp));

                continue;
            }

            /* Valid packet received. Escape from the loop. */
            if (rp.startsWith(PKT_GET_PID_RESP + pidId)) {
                break;
            }

            /* Invalid packet received. Log this and wait for another one. */
            mLog.w(String.format(
                    "Invalid ECU response, awaiting <%s>, got <%s>.",
                    PKT_GET_PID_RESP + pidId, rp));
        }

        int[] pidData = new int[(rp.length() - 6) / 2];

        for (int i = 0; i < pidData.length; i++) {
            pidData[i] = Integer.parseInt(rp.substring(
                    6 + (i * 2), 8 + (i * 2)), 16);
        }

        return pidData;
    }

    /**
     * Constructor
     */
    public Obd(Kwp2000 kwp2000) {
        /* Save reference to lower layer communication object. */
        mKwp2000 = kwp2000;

        mPicCom = kwp2000.mPicCom;
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

/**
 * Provides mechanism for
 *  - capturing probe values in file
 *  - capturing state of application in file
 *
 * Designed as singleton.
 */
class LogMaster {
    /* Log file capturing values of probes. */
    private static String mFilenameProbe;
    private static File mFileProbe;
    private static FileWriter mFileWriterProbe;

    /* Log file capturing various status messages. */
    private static String mFilenameSts;
    private static File mFileSts;
    private static FileWriter mFileWriterSts;

    private static double mLogStartTime;

    /* Header written to log file */
    private static boolean mLogHeaderWritten = false;

    /* Log files are ready to receive log information. */
    private static boolean mLogFilesReady = false;

    /* Log files were initialized */
    private static boolean mLogFilesInitialized = false;

    /* Reference to singleton. */
    private static LogMaster mInstance = null;

    /**
     * Private constructor
     */
    private LogMaster() { }

    /**
     * Initializes log files. Returns 0 in case of success, -1 otherwise.
     */
    private static int init() {
        /* Probe filename is in format of 'obd-YYYYMMDD-HHMMSS.txt' */
        mFilenameProbe = String.format("obd-%1$tY%1$tm%1$td-%1$tH%1$tM%1$tS.txt",
                Calendar.getInstance());

        /*
         * There is just one file used for capturing status messages.
         * It operates in append mode.
         */
        mFilenameSts = "obd-sts.txt";

        /* Initialize log timer. */
        mLogStartTime = ObdLogger.getTime();

        /* Create log files if they don't exist . */
        mFileProbe = new File(Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DOWNLOADS), mFilenameProbe);

        mFileSts = new File(Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DOWNLOADS), mFilenameSts);

        if (!mFileProbe.exists()) {
            try {
                mFileProbe.createNewFile();
            } catch (IOException e) {
                Log.e(ObdLogger.TAG, "Failed to create file " +
                        mFilenameProbe, e);

                return -1;
            }
        }

        if (!mFileSts.exists()) {
            try {
                mFileSts.createNewFile();
            } catch (IOException e) {
                Log.e(ObdLogger.TAG, "Failed to create file " +
                        mFilenameSts, e);

                return -1;
            }
        }

        /* Open files in append mode. */
        try {
            mFileWriterProbe = new FileWriter(mFileProbe, true);
        } catch (IOException e) {
                Log.e(ObdLogger.TAG, "Failed to create FileWriter.", e);

                return -1;
        }

        try {
            mFileWriterSts = new FileWriter(mFileSts, true);
        } catch (IOException e) {
                Log.e(ObdLogger.TAG, "Failed to create FileWriter.", e);

                return -1;
        }

        mLogFilesReady = true;

        return 0;
    }

    /**
     * Returns reference to singleton.
     */
    public static LogMaster getInstance() {
        /* Create singleton if it does not exist yet */
        if (mInstance == null) {
            Log.d(ObdLogger.TAG, "Creating LogMaster singleton.");
            mInstance = new LogMaster();
        }

        /*
         * Init log files.
         */
        if (!mLogFilesInitialized) {
            if (init() == -1) {
                Log.e(ObdLogger.TAG, "Failed to initialize log files.");
            } else {
                mLogFilesInitialized = true;
                Log.d(ObdLogger.TAG, "Log files initialized.");
            }
        }

        return mInstance;
    }

    /**
     * Logs data captured when measuring starting voltage.
     *
     * @param  p captured data
     * @return   True if data was successfully logged, false otherwise.
     */
    public boolean logStartingVoltage(String p) {
        /* Capture only if log file was successfully initialized. */
        if (!mLogFilesReady) {
            return false;
        }

        /* Write acquired data to the log file. */
        try {
            /* Header */
            mFileWriterProbe.write("# Starting phase battery voltage\n");

            /*
             * Data - packet is in form of
             *  Q00aaaabbbbccccdddd...zzzz
             *
             * Replace decimal comma with decimal point.
             */
            for (int i = 0; i < (p.length() - 3) / 4; i++) {
                mFileWriterProbe.write(String.format(" %.1f",
                        0.01837 * Integer.parseInt(p.substring(3 + (4 * i),
                        7 + (4 * i)), 16)).replace(',', '.'));
            }

            /* Footer */
            mFileWriterProbe.write("\n# Starting phase battery voltage\n");
        } catch (IOException e) {
            Log.e(ObdLogger.TAG, "Failed to write into log file.", e);

            return false;
        }

        return true;
    }

    /**
     * Captures values of given list of probes in file
     */
    public void captureProbes(ArrayList<String> probeList,
            HashMap<String, Probe> probePool) {

        /* Capture only if log file was successfully initialized. */
        if (!mLogFilesReady) {
            return;
        }

        /* Create header if entered for the first time */
        if (!mLogHeaderWritten) {
            mLogHeaderWritten = true;

            String logHeader = "Time[s]";
            for (int i = 0; i < probeList.size(); i++) {
                Probe probe = probePool.get(probeList.get(i));
                logHeader += String.format("\t%s[%s]",
                        probe.getLabel(), probe.getUnits());
            }

            /* Write header to log file */
            try {
                mFileWriterProbe.write(logHeader + "\n");
            } catch (IOException e) {
                Log.e(ObdLogger.TAG, "Failed to writer header into log file.", e);

                return;
            }
        }

        /* Log time first. */
        StringBuffer s = new StringBuffer(String.format("%.3f",
                (ObdLogger.getTime() - mLogStartTime)));

        /* Replace decimal comma with decimal point. */
        if (s.indexOf(",") != -1) {
            s.setCharAt(s.indexOf(","), '.');
        }

        String logStr = s.toString();

        /*
         * Now enumerate through the list of all PIDs
         * measured and log value of each of them.
         */
        for (int i = 0; i < probeList.size(); i++) {
            Probe probe = probePool.get(probeList.get(i));

            logStr += String.format("\t%s", probe.physToLog());
        }

        try {
            mFileWriterProbe.write(logStr + "\n");
        } catch (IOException e) {
            Log.e(ObdLogger.TAG, "Writing to log file failed.", e);
        }
    }

    /**
     * Generates time stamp for status log file.
     */
    static private String getTimestamp() {
        return String.format("%1$te.%1$tB %1$tY %1$tH:%1$tM:%1$tS",
                Calendar.getInstance());
    }

    /**
     * Prepends message with relative timestamp.
     *
     * @param  msg Message to be logged.
     * @return     Message with timestamp prepended.
     */
    private String prependRelTimestamp(String msg) {
        return String.format("[%.3f] %s", ObdLogger.getRunTime(), msg);
    }

    /**
     * Implements common logging code.
     */
    private void common(String level, String msg) {
        try {
            mFileWriterSts.write(String.format("[%s] %s: %s\n",
                     getTimestamp(), level, msg));
        } catch (IOException e) {
            Log.e(ObdLogger.TAG, "Writing to status log file failed.", e);
        }
    }

    /**
     * Logs error messages.
     */
    synchronized public void e(String msg) {
        common("ERROR", msg);

        Log.e(ObdLogger.TAG, prependRelTimestamp(msg));
    }

    /**
     * Logs warning messages.
     */
    synchronized public void w(String msg) {
        common("WARNING", msg);

        Log.w(ObdLogger.TAG, prependRelTimestamp(msg));
    }

    /**
     * Logs info messages.
     */
    synchronized public void i(String msg) {
        common("INFO", msg);

        Log.i(ObdLogger.TAG, prependRelTimestamp(msg));
    }

    /**
     * Logs debug messages.
     */
    synchronized public void d(String msg) {
        common("DEBUG", msg);

        Log.d(ObdLogger.TAG, prependRelTimestamp(msg));
    }
    
    /**
     * Closes logging files.
     */
    public static void finish() {
        try {
            mFileWriterProbe.write("==============================================\n");
            mFileWriterProbe.write(String.format("Final time: %.3f\n",
                    (ObdLogger.getTime() - mLogStartTime)));

            mFileWriterProbe.write("==============================================\n");
            mFileWriterProbe.flush();
            mFileWriterProbe.close();
        } catch (IOException e) {
            Log.e(ObdLogger.TAG, "Failed to finalize probe log file.", e);
        }

        try {
            mFileWriterSts.flush();
            mFileWriterSts.close();
        } catch (IOException e) {
            Log.e(ObdLogger.TAG, "Failed to finalize status log file.", e);
        }

        Log.d(ObdLogger.TAG, "Log files closed.");
        mLogFilesInitialized = false;
        mLogHeaderWritten = false;
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

        mActivity.runOnUiThread(new Runnable() {
            public void run() {
                stsIndicator.setEnabled(on);
            }
        });
    }

}

/**
 * Represents communication service running as a background thread.
 */
class CommunicationService {
    /* Initiates communication with PIC module. */
    private static final int ST_COMM_INIT_PIC = 0;
    private static final int ST_COMM_INIT_PIC1 = 1;
    /* Set ECU communication mode. */
    private static final int ST_COMM_INIT_ECU_MODE = 2;
    /* Initiates communication with ECU. */
    private static final int ST_COMM_INIT_ECU = 3;
    /* Acquire probe value. */
    private static final int ST_ACQUIRE_PROBE_VALUE = 4;

    private boolean mDebug = false;
    private boolean mQuit = false;
    private int mState;

    /* Runs in debug mode talking to ECU simulator. */
    private boolean mEcuSim = false;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Reference to main Activity */
    private Activity mActivity;

    /* Progress dialog displayed when the device is connecting to PIC. */
    private ProgressDialog mPicConnectPd;

    /* Dashboard */
    private Dashboard mDashboard;

    /* OBD communication module. */
    private Obd mObd;

    /* Pid speed groups */
    private PidGroups mPidSpeedGroups;

    /*
     * Pool of all available probes in form of dictionary.
     * Probe IDs (String) is used as a key.
     */
    public HashMap<String, Probe> mProbePool;

    /* List of all measured PIDs */
    private ArrayList<String> mPidLogList;

    /* List of PIDs passed to PIC module for auto-scan mode */
    private String mPidAutoScanList;

    /* Status of PIC module communication */
    private Button mStsPic;

    /* Status of ECU communication */
    private Button mStsEcu;

    /* GPS status */
    private Button mStsGps;

    /**
     * Signals to communication thread that it should terminate.
     */
    public void finish() {
        mQuit = true;
    }

    /**
     * Implements communication thread running in background.
     */
    public Runnable loop = new Runnable() {
        public void run() {
            /* Currently processed PID */
            String currPid = "";
            PicCom pc = mObd.mKwp2000.mPicCom;
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
                    if (!pc.open()) {
                        /* Number of attempts exceeded, give up. */
                        if (connRetry > 3) {
                            mLog.w("Failed to establish communication " +
                                    "with PIC, aborting.");

                            /* Dismiss progress dialog. */
                            mActivity.runOnUiThread(new Runnable() {
                                public void run() {
                                    mPicConnectPd.dismiss();
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

                            continue;
                        }

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

                        mLog.w("Retrying to establish communication " +
                                "with PIC...");

                        continue;
                    }

                    /* Valid response received, communication established. */

                    /* Dismiss progress dialog. */
                    mActivity.runOnUiThread(new Runnable() {
                        public void run() {
                            mPicConnectPd.dismiss();
                        }
                    });

                    mState = ST_COMM_INIT_ECU_MODE;
                } else if (mState == ST_COMM_INIT_ECU_MODE) {
                    String mode;
                    int res = -1;

                    /* Set ECU communication mode. */
                    if (mEcuSim) {
                        mode = PicCom.ECU_COMM_MODE_KWP2K_DEBUG;
                    } else {
                        mode = PicCom.ECU_COMM_MODE_KWP2K_ENHANCED;
                    }

                    try {
                        if (pc.setEcuCommMode(mode) == 0) {
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
                     * Connection with ECU established,
                     * let user know via indicator on status bar.
                     */
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
                    Probe battProbe = mProbePool.get("Vst");

                    /* Update appropriate probe */
                    if (battProbe.update(v)) {
                        /* Update indicator with new value */
                        mDashboard.updateIndicator(battProbe);
                    }

                    if (!captureStartVoltage) {
                        try {
                            /*
                             * Configure and start OBD auto-scan mode.
                             */
                            pc.configObdAutoScan(mPidAutoScanList);
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
                                pc.configObdAutoScan(mPidAutoScanList);
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
                            Probe currProbe = mProbePool.get(currPid);
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
                    } else if (p.startsWith(Obd.PKT_GET_PID_RESP)) {
                        currPid = p.substring(4, 6);
                        Probe currProbe = mProbePool.get(currPid);

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
                                (TurboProbe) mProbePool.get("Turbo");

                        if (turboPar.update(currPid, mProbePool)) {
                            mDashboard.updateIndicator(turboPar);
                        }

                        /*
                         * Log data into log file every time all PIDs
                         * in the first group get processed.
                         */
                        /* TODO: Use some method for this check */
                        if (currPid.equals("0B")) {
                            mLog.captureProbes(mPidLogList, mProbePool);
                        }
                    } else {
                        mLog.w(String.format("Discarding packet <%s>.", p));

                        continue;
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
                if (!pc.close())
                    mLog.w("Failed to close PIC communication channel.");
            }
        }
    };

    /**
     * Constructor.
     *
     * @param activity  the reference to activity which spawned this service
     */
    public CommunicationService(Activity activity) {
        super();

         /*
         * Instantiates OBD communication module along with all lower
         * layer communication modules.
         */
        mObd = new Obd(new Kwp2000(new PicCom(
                new PicComBt("OBD converter"),
                new PicComCallback(activity))));

        /*
         * Determine if we should run in debug mode talking to to ECU simulator.
         * Default to normal mode when talking to real ECU.
         */
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(activity);

        mEcuSim = prefs.getBoolean("pref_debug_mode", false);

        /* Create progress dialog displayed when connecting to PIC. */
        mPicConnectPd = new ProgressDialog(activity);
        mPicConnectPd.setTitle("Connecting to PIC...");
        mPicConnectPd.setMessage("Please wait.");
        mPicConnectPd.setCancelable(false);
        mPicConnectPd.setIndeterminate(true);
        
        /*
         * Create pool of available probes - TODO read data from external file.
         */
        mProbePool = new HashMap<String, Probe>();

        /* OBD parameters measured and supplied by ECU. */
        mProbePool.put("04", new ObdProbe("04", "%",
                "Calculated Engine Load", "LOAD", "%.0f",
                0.0, 100.0 / 255.0, 0.0, 1.0 / 100.0, mObd));

        mProbePool.put("05", new ObdProbe("05", "stC",
                "Engine coolant temperature", "WATER", "%.0f",
                -40, 1, 40.0, 1.0 / (40.0 + 110.0), mObd));

        mProbePool.put("0B", new ObdProbe("0B", "kPa",
                "Intake Manifold Absolute Pressure", "AIR", "%.0f",
                0.0, 1.0, 0.0, 1.0 / 255.0, mObd));

        mProbePool.put("0C", new ObdProbe("0C", "-",
                "Engine RPM", "RPM", "%.0f",
                0.0, 0.25, 0.0, 1.0 / 5000.0, mObd));

        mProbePool.put("0D", new ObdProbe("0D", "km/h",
                "Vehicle Speed", "SPEED", "%.0f",
                0.0, 1.0, 0.0, 1.0 / 195.0, mObd));

        mProbePool.put("0F", new ObdProbe("0F", "stC",
                "Intake Air Temperature", "AIR", "%.0f",
                -40, 1, 40.0, 1.0 / (40.0 + 50.0), mObd));

        mProbePool.put("10", new ObdProbe("10", "g/s",
                "MAF Air Flow Rate", "MAF", "%.0f",
                0.0, 0.01, 0.0, 1.0 / 150.0, mObd));

        mProbePool.put("11", new ObdProbe("11",  "%",
                "Throttle Position", "THROT", "%.0f",
                0.0, 100.0 / 255.0, 0.0, 1.0 / 100.0, mObd));

        mProbePool.put("23", new ObdProbe("23", "MPa",
                "Fuel Rail Pressure", "RAIL", "%.0f",
                0.0, 0.01, 0.0, 1.0 / 120.0, mObd));

        mProbePool.put("2C", new ObdProbe("2C", "%",
                "Commanded EGR", "EGR", "%.0f",
                0.0, 100.0 / 255.0, 0.0, 0.01, mObd));

        mProbePool.put("2D", new ObdProbe("2D", "%",
                "EGR Error", "EGRe", "%.0f",
                -128.0, 100.0 / 128.0, 100.0, 0.01, mObd));

        mProbePool.put("33", new ObdProbe("33", "kPa",
                "Barometric Pressure", "BAR", "%.0f",
                0.0, 1.0, 0.0, 1.0 / 255.0, mObd));

        /*
         * Battery voltage - measured directly by PIC A/D converter.
         */
        mProbePool.put("Vst", new PicProbe("Vst", "V",
                "Battery Voltage", "Vst", "%.1f",
                0.0, 0.01837, 0.0, 1.0 / (0.01837 * 1024),
                mObd.mKwp2000.mPicCom));
        mProbePool.put("Vrms", new PicProbe("Vrms", "-",
                "RMS battery Voltage", "Vrms", "%.0f",
                0.0, 1.0, 0.0, 1.0 / (1.0 * 1024),
                mObd.mKwp2000.mPicCom));
        mProbePool.put("Vmin", new PicProbe("Vmin", "-",
                "Min battery Voltage", "Vmin", "%.0f",
                0.0, 1.0, 0.0, 1.0 / (1.0 * 1024),
                mObd.mKwp2000.mPicCom));
        mProbePool.put("Vmax", new PicProbe("Vmax", "-",
                "Max battery Voltage", "Vmax", "%.0f",
                0.0, 1.0, 0.0, 1.0 / (1.0 * 1024),
                mObd.mKwp2000.mPicCom));
        mProbePool.put("F1", new PicProbe("F1", "-",
                "Frequency 1", "F1", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("M1", new PicProbe("M1", "-",
                "Amplitude 1", "M1", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("F2", new PicProbe("F2", "-",
                "Frequency 2", "F2", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("M2", new PicProbe("M2", "-",
                "Amplitude 2", "M2", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("F3", new PicProbe("F3", "-",
                "Frequency 3", "F3", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("M3", new PicProbe("M3", "-",
                "Amplitude 3", "M3", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("F4", new PicProbe("F4", "-",
                "Frequency 4", "F4", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("M4", new PicProbe("M4", "-",
                "Amplitude 4", "M4", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("F5", new PicProbe("F5", "-",
                "Frequency 5", "F5", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));
        mProbePool.put("M5", new PicProbe("M5", "-",
                "Amplitude 5", "M5", "%.0f",
                0.0, 1.0, 0.0, 1.0, mObd.mKwp2000.mPicCom));

        /*
         * 'Turbo' indicator. Calculated as Intake pressure/Barometric pressure
         * ratio.
         */
        mProbePool.put("Turbo", new TurboProbe());

        /* Instantiate dashboard */
        mDashboard = new Dashboard(activity);

        /* Initialize state machine */
        mState = ST_COMM_INIT_PIC;

        /* Save reference to main Activity */
        mActivity = activity;

        /* Initialize references to status indicators */
        mStsPic = (Button) activity.findViewById(R.id.picState);
        mStsEcu = (Button) activity.findViewById(R.id.ecuState);
        mStsGps = (Button) activity.findViewById(R.id.gpsState);

        /* Create list of PID priority groups */
        mPidSpeedGroups = new PidGroups();

        /* Fast - Calculated Engine Load, Intake Manifold Absolute Pressure */
        String grpF[] = {"04", "0B"};
        mPidSpeedGroups.create(0, "fast", grpF);

        /* Medium - RPM, MAF, Rail Pressure, EGR */
        String grpM[] = {"0C", "10", "23", "2C"};
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
        PicCom pc = mObd.mKwp2000.mPicCom;

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

public class ObdLogger extends Activity {
    static final String TAG = "ObdLogger";
    static final int REQUEST_ENABLE_BT = 1;

    /* Reference to application context */
//    private Context mContext;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Records time when application started. */
    private static double mTimeStart;

    /* Communication service handling communication with PIC module and ECU */
    private CommunicationService mComService = null;

    /* Communication thread */
    private Thread commThread;

    /* OBD communication module */
    private Obd mObd;

    /**
     * Launches communication service.
     */
    private void launchComService() {
        /* Instantiate communication service. */
        mComService = new CommunicationService(this);

        /* Create background thread handling communication with PIC module. */
        commThread = new Thread(null, mComService.loop, "ComService");

        /* Launch the service. */
        commThread.start();
    }

    /**
     * Returns current time in seconds.
     */
    public static double getTime() {
        return System.currentTimeMillis() / 1000.0;
    }

    /**
     * Returns run time of the application in seconds.
     */
    public static double getRunTime() {
        return ObdLogger.getTime() - mTimeStart;
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        /* Bluetooth adapter */
        BluetoothAdapter mBluetoothAdapter = null;

        Log.d(TAG, "-> O.onCreate()");

        super.onCreate(savedInstanceState);
        setContentView(R.layout.obd_logger);

        mTimeStart = getTime();

        mLog.d("Application started.");

        /*
         * Initialize Bluetooth connection.
         */
       	mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

        if (mBluetoothAdapter == null) {
            Toast.makeText(this, "Bluetooth is not available",
                    Toast.LENGTH_LONG).show();

            finish();
            return;
        }

        /*
         * If Bluetooth is not enabled, pop up dialog asking user
         * to enable it.
         */
        if (!mBluetoothAdapter.isEnabled()) {
            Intent enableBtIntent = new Intent(
                    BluetoothAdapter.ACTION_REQUEST_ENABLE);

            startActivityForResult(enableBtIntent, REQUEST_ENABLE_BT);
        }  else {
            /*
             * Bluetooth enabled, so pass the baton to communication
             * service.
             */
            launchComService();
        }

        Log.d(TAG, "<- O.onCreate()");
    }

    /** Called when the activity is started. */
    @Override
    public void onStart() {
        super.onStart();

        Log.d(TAG, "-> O.onStart()");
        Log.d(TAG, "<- O.onStart()");
    }

    /** Called when the activity is paused. */
    @Override
    public void onPause() {
        super.onPause();

        Log.d(TAG, "-> O.onPause()");
        Log.d(TAG, "<- O.onPause()");
    }

    /** Called when the activity is resumed. */
    @Override
    public void onResume() {
        super.onResume();

        Log.d(TAG, "-> O.onResume()");
        Log.d(TAG, "<- O.onResume()");
    }

    /** Called when the activity is restarted. */
    @Override
    public void onRestart() {
        super.onRestart();

        Log.d(TAG, "-> O.onRestart()");
        Log.d(TAG, "<- O.onRestart()");
    }

    /** Called when the activity is stopped. */
    @Override
    public void onStop() {
        super.onStop();

        Log.d(TAG, "-> O.onStop()");
        Log.d(TAG, "<- O.onStop()");
    }

    /** Called when the activity is destroyed. */
    @Override
    public void onDestroy() {
        super.onDestroy();

        Log.d(TAG, "-> O.onDestroy()");

        /*
         * If communication thread was spawned, let it know
         * it should terminate its job.
         */
        if (mComService != null && commThread != null) {
            mComService.finish();

            /* Wait until the thread quits - set timeout to 10 seconds */
            try {
                commThread.join(10000);
            } catch (InterruptedException e) {
                /* Could not join communication thread */
                Log.e(TAG, "Interrupted while sleeping.", e);
            }

            Log.d(TAG, "Joining communication thread...");
            if (commThread.isAlive()) {
                /* Could not join communication thread */
                Log.e(TAG, "FAILED.");
            } else {
                Log.i(TAG, "JOINED.");
            }
        }

        mLog.i("Application finished.");

        /* Close log files */
        mLog.finish();

        Log.d(TAG, "<- O.onDestroy()");
    }

    /**
     * Called when the concurrently running activity
     * spawned by startActivityForResult() is done.
     */
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.d(TAG, "-> O.onActivityResult " + resultCode);

        switch (requestCode) {
        case REQUEST_ENABLE_BT:
            /* Request to enable Bluetooth returned */
            if (resultCode == Activity.RESULT_OK) {
                /*
                 * Bluetooth enabled, so pass the baton to communication
                 * service.
                 */
                launchComService();
            } else {
                /* User did not enable Bluetooth or an error occurred */
                Log.d(TAG, "BT not enabled");

                Toast.makeText(this, "Bluetooth not enabled",
                        Toast.LENGTH_LONG).show();

                finish();
            }
        }

        Log.d(TAG, "<- O.onActivityResult");
    }
}

