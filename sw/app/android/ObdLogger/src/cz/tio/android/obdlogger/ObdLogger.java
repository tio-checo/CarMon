    package cz.tio.android.obdlogger;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
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
import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.HashMap;
import java.util.Set;
import java.util.UUID;

import cz.tio.android.obdlogger.EditPreferences;

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
     * Asks for probe value. Returns true in case of success. Otherwise
     * returns false.
     */
    public boolean acquire() {
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
     * Asks PIC for probe value. Returns true in case of success. Otherwise
     * returns false.
     */
    public boolean acquire() {
        int[] picAdValue = new int[1];

        picAdValue[0] = mPicCom.getAdValue("08");

        /* Failed to obtain data from ECU. Invalidate the value. */
        if (picAdValue[0] == -1) {
            mCurrYValid = false;

            return false;
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
     * Obtains value of OBD parameter from ECU. Updates probe with new value.
     * Returns true in case of success. Otherwise returns false.
     */
    public boolean acquire() {
        int[] ecuData = mObd.getPidValue(mId);

        /* Failed to obtain data from ECU. Invalidate the value. */
        if (ecuData == null) {
            mCurrYValid = false;

            return false;
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
 * Defines interface implemented by communication services (Obd, Kwp2000, Pic).
 */
interface ICommSvc {
    /**
     * Establishes communication channel.
     * Returns true if communication has been established, otherwise
     * returns false.
     */
    public boolean open();

    /**
     * Closes communication channel.
     * Returns true if communication channel has been sucessfully closed,
     * otherwise returns false.
     */
    public boolean close();

    /**
     * Sends data.
     * Return false if attempt to send data failed, otherwise return
     * true.
     */
    public boolean sendPacket(String data);

    /**
     * Returns packet received from any module.
     * Returns null if timeout expired when waiting for data,
     * otherwise returns received data.
     */
    public String getPacket();

    /**
     * Returns packet starting with specified string.
     * Returns null if timeout expired when waiting for data,
     * otherwise returns received data.
     */
    public String getPacket(String startString);
}

/**
 * Implements FIFO which transports characters from PIC module (producer)
 * to communication thread (consumer). If FIFO is empty, consumer does
 * not block on waiting for character indefinitely - there is 1 second
 * timeout implemented.
 */
class PicRecvChar {
    private int mChar = -1;
    private ArrayList<Integer> mRecvBuffer;
    private boolean mWaiting;

    /**
     * Constructor
     */
    public PicRecvChar() {
        mRecvBuffer = new ArrayList<Integer>();

        mWaiting = false;
    }

    /**
     * Synchronized getter
     */
    synchronized public int get() {
        /* If there is a character in receive buffer, return it */
        if (mRecvBuffer.size() > 0) {
            return mRecvBuffer.remove(0).intValue();
        }

        mWaiting = true;
        /* Buffer is empty, wait for a while for a character. */
        try {
            wait(1000);
        } catch (InterruptedException e) {
            /* Thread interrupted, return. */
            mWaiting = false;
            return -1;
        }
        mWaiting = false;

        /* Character has been received, return it */
        if (mRecvBuffer.size() > 0) {
            return mRecvBuffer.remove(0).intValue();
        } else {
            /* Timeout expired, signal it */
            return -1;
        }
    }

    /**
     * Synchronized setter
     */
    synchronized public void put(int ch) {
        /* Store character into receive buffer */
        mRecvBuffer.add(ch);

        /* consumer is blocked on waiting for character, so wake it up */
        if (mWaiting) {
            notify();
        }
    }
}

/**
 * PIC reader thread
 */
class PicReaderThread extends Thread {
    private PicRecvChar mRecvBuf = null;
    private InputStream mInStream = null;

    /**
     * Constructor
     */
    public PicReaderThread(PicRecvChar recvChar, InputStream inStr) {
        mRecvBuf = recvChar;

        mInStream = inStr;
    }

    /**
     * Thread body
     */
    public void run() {
        int ch = 0;

        /* Loop until input stream is closed */
        while (ch != -1) {
            try {
                ch = mInStream.read();

                /*
                 * Thread has been interrupted which indicates that upper layer
                 * wants us to gracefully terminate the thread.
                 */
                if (interrupted()) {
                    Log.d(ObdLogger.TAG, "Reader thread interrupted.");

                    ch = -1;
                }

                /* Store received character and notify waiting thread */
                mRecvBuf.put(ch);

//                Log.d(ObdLogger.TAG, String.format("Pic -> %c", ch));
            } catch (IOException e) {
                Log.e(ObdLogger.TAG, "Error when waiting for " +
                        "character from PIC module.");

                /* Connection lost, terminate the thread. */
                ch = -1;
                mRecvBuf.put(ch);
            }
        }
    }
}

/**
 * Provides methods for communicating with PIC module.
 */
class PicCom implements ICommSvc {
    /* ECU communication modes */
    static final String ECU_COMM_MODE_KWP2K_BASIC = "01";
    static final String ECU_COMM_MODE_KWP2K_ENHANCED = "02";
    static final String ECU_COMM_MODE_KWP2K_DEBUG = "03";

    /* states of receive state machine */
    private static final int ST_WAIT4SOF = 1;
    private static final int ST_RECV_DATA = 2;
    /* receive timeout in milliseconds */
    private static final long RECV_TIMEOUT = 1000;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Packet buffer implemented as list of String objects */
    private ArrayList<String> mPacketBuffer;

    /* enables debug mode */
    private boolean mDebug = false;
    /* timestamp of the last communication operation */
    private double mComTime;

    /* indicates if communication with PIC module is established */
    private boolean mComPicEstablished = false;

    /* input and output sockets used for communication with PIC module */
    private InputStream mPicIn = null;
    private OutputStream mPicOut = null;

    /* "reader" thread */
    private PicReaderThread mReaderThread;
    /*
     * Object representing value received from PIC module - it holds integer,
     * since InputStream.read() returns int.
     */
    private PicRecvChar mRecvChar;

    /**
     * Searches in packet buffer for packet starting with given string.
     * If packet matching given criteria is found, it's removed from buffer
     * and returned to caller, otherwise null is returned.
     */
    private String findPacket(String s) {
        int i;

        /* Buffer is empty, return */
        if (mPacketBuffer.isEmpty())
            return null;

        for (i = 0; i < mPacketBuffer.size(); i++) {
            /* Packet found, remove it from buffer and return it. */
            if (mPacketBuffer.get(i).startsWith(s))
                return mPacketBuffer.remove(i);
        }

        /* Packet not found in a buffer. */
        return null;
    }

    /**
     * Appends packet to the end of the buffer.
     */
    private boolean bufferPacket(String p) {
            Log.w(ObdLogger.TAG, String.format("[%.3f] Buffering packet <%s>.",
                    ObdLogger.getRunTime(), p));

            return mPacketBuffer.add(p);
    }

    /**
     * Waits either for ACK, NAK or both from specified module.
     * @return  0  - ACK has been received.
     *         err - error code received in NAK response
     *         -1  - timeout expired
     */
    private int waitForAckNak(String modId, boolean ack, boolean nak) {
        String ackResp = modId + "A";
        String nakResp = modId + "N";
        String p;

        /*
         * First inspect packet buffer, as the response may have already
         * been received. Search for ACK first, since that is what
         * we expect to receive most of the time.
         */
        if (ack) {
            p = findPacket(ackResp);
            /* ACK received */
            if (p != null)
                return 0;
        }

        if (nak) {
            p = findPacket(nakResp);
            /* NAK received */
            if (p != null)
                return Integer.parseInt(p.substring(2), 16);
        }

        /*
         * Neither ACK nor NAK found in the buffer, wait for packet from PIC
         * until timeout expires.
         */
        while (true) {
            p = recv();

            /* Timeout occurred */
            if (p == null) {
                Log.w(ObdLogger.TAG, String.format("[%.3f] " +
                        "Timeout when waiting for packet from" +
                        " PIC module", ObdLogger.getRunTime()));

                return -1;
            }

            if (ack) {
                /* ACK received. */
                if (p.equals(ackResp)) {
                    return 0;
                }
            }

            if (nak) {
                /* NAK received. */
                if (p.startsWith(nakResp)) {
                    return Integer.parseInt(p.substring(2), 16);
                }
            }

            /*
             * Neither NAK nor ACK packet, so save it to the buffer.
             */
            bufferPacket(p);
        }
    }

    /**
     * Waits for ACK or NAK from specified module.
     * @return  0  - ACK has been received.
     *         err - error code received in NAK response
     *         -1  - timeout expired
     */
    private int waitForAckNak(String modId) {
        return waitForAckNak(modId, true, true);
    }

    /**
     * Receives one byte from PIC module. Returns -1 if InputStream was closed
     * or timeout expired.
     */
    private int recvChar() {
        return mRecvChar.get();
    }

    /**
     * Receives packet from PIC module.
     * Returns null if timeout expired when waiting for packet, otherwise
     * returns received packet.
     */
    private String recv() {
        String p = new String("");

        /* State machine will wait for beginning of the packet. */
        int recvState = ST_WAIT4SOF;

        /*
         * Receive characters until whole packet is received or until
         * timeout expires.
         */
        while (true) {
            /* Wait for character from PIC module. */
            int b = recvChar();

            /*
             * If timeout expired and no character has been received,
             * abort the process and return null.
             */
            if (b == -1) {
                return null;
            }

            char ch = (char) b;

            /* Wait for beginning of packet. */
            if (recvState == ST_WAIT4SOF && ch == '<') {
                p = new String("");

                recvState = ST_RECV_DATA;

                if (mDebug) {
                    Log.d(ObdLogger.TAG, (String.format("[%.3f] -> '<'",
                            ObdLogger.getTime() - mComTime)));

                    mComTime = ObdLogger.getTime();
                }

                continue;
            }

            /* Receive the rest of packet. */
            if (recvState == ST_RECV_DATA) {
                if (ch == '<') {
                    /* SOF character received again, restart receive process. */
                    p = new String("");

                    if (mDebug) {
                        Log.d(ObdLogger.TAG, (String.format("[%.3f] -> <",
                                ObdLogger.getTime() - mComTime)));

                        mComTime = ObdLogger.getTime();
                    }
                } else if (ch == '>') {
                    /*
                     * EOF character, complete packet has been received.
                     * Pass data payload to the caller.
                     * If desired, print received packet for debugging
                     * purposes.
                     */
                    if (mDebug) {
                        Log.d(ObdLogger.TAG, (String.format("[%.3f] -> %s",
                                ObdLogger.getTime() - mComTime, p)));

                        mComTime = ObdLogger.getTime();
                    }

                    return p;
                } else {
                    /* Otherwise store received character in buffer. */
                    p += ch;

                    if (mDebug) {
                        Log.d(ObdLogger.TAG, (String.format("[%.3f] -> %s",
                                ObdLogger.getTime() - mComTime, p)));

                        mComTime = ObdLogger.getTime();
                    }
                }
            }
        }
    }

    /**
     * Constructor
     */
    public PicCom(InputStream In, OutputStream Out) {
        if (mDebug) {
            mComTime = ObdLogger.getTime();
        }

        /* Initialize input and output streams */
        mPicIn = In;
        mPicOut = Out;

        /*
         * Create object which will be used for character received from
         * PIC module between main and "reader" thread.
         */
        mRecvChar = new PicRecvChar();

        /* Instantiate packet buffer. */
        mPacketBuffer = new ArrayList<String>();

        /*
         * Create separate "reader" thread. It is needed,
         * since InputStream.read() does not support timeouts and blocks
         * if there are no data available. That would in turn block
         * main communication thread and lead to deadlock.
         */
        mReaderThread = new PicReaderThread(mRecvChar, In);

        /* Start reader thread */
        mReaderThread.start();
    }

    /**
     * Establishes communication with PIC module.
     * Returns true if communication has been established, otherwise
     * returns false.
     */
    public boolean open() {
        /* If communication channel is already established, return. */
        if (mComPicEstablished) {
            return true;
        }

        /* Initiate communication. */
        int res = setEcuCommMode(ECU_COMM_MODE_KWP2K_ENHANCED);

        if (res == -1) {
            mLog.w("Timeout when trying to initiate communication " +
                    "with PIC module.");

            return false;
        } else {
            /* Valid response received, communication established. */
            mLog.i("Communication with PIC module established.");

            mComPicEstablished = true;

            return true;
        }
    }

    /**
     * Finishes communication.
     */
    public boolean close() {
        /* Signal reader thread that it should finish its job. */
        mReaderThread.interrupt();

        /*
         * In order to make the thread interruptible, it needs to return
         * from blocking I/O. To accomplish that, let it receive at least
         * one character from PIC module.
         */
        sendPacket("QS");

        try {
            mReaderThread.join(5000);
        } catch (InterruptedException e) {
            /* Could not join communication thread */
            Log.e(ObdLogger.TAG, "Interrupted while sleeping.", e);
        }

        if (mReaderThread.isAlive()) {
            /* Could not join communication thread */
            Log.e(ObdLogger.TAG, "Failed to join PIC reader thread.");
        } else {
            Log.i(ObdLogger.TAG, "PIC reader thread joined.");
        }

        mComPicEstablished = false;

        return true;
    }

    /**
     * Wipes out packet buffer.
     */
    public void cleanBuffer() {
        mPacketBuffer.clear();
    }

    /**
     * Assembles and sends packet to PIC module.
     * Return false if attempt to send the packet failed, otherwise return
     * true.
     */
    public boolean sendPacket(String data) {
        /* Encapsulate provided data payload with SOF and EOF characters. */
        String picPacket = "<" + data + ">";

        /* Send out resulting packet to PIC module. */
        try {
            mPicOut.write(picPacket.getBytes());
        } catch (IOException e) {
            Log.e(ObdLogger.TAG, "sendPacket() - writing to OutputStream " +
                    "failed.", e);

            return false;
        }

        /* If desired, print the packet for debugging purposes. */
        if (mDebug) {
            Log.d(ObdLogger.TAG, String.format("[%.3f] <- %s",
                    (ObdLogger.getTime() - mComTime), picPacket));

            mComTime = ObdLogger.getTime();
        }

        return true;
    }

    /**
     * Returns packet received from any module. If packet buffer contains
     * packets, returns the first packet from the buffer, otherwise wait
     * for a packet from PIC either until packet is received or timeout
     * expires. Returns null if timeout expired.
     */
    public String getPacket() {
        /* Buffer is empty, wait for packet from PIC module. */
        if (mPacketBuffer.isEmpty()) {
            return recv();
        }

        /* Return the first packet from buffer. */
        return mPacketBuffer.remove(0);
    }

    /**
     * Returns packet starting with specified straing. If packet buffer contains
     * matching packet, returns the first packet found, otherwise wait for such
     * packet from PIC until timeout expires.
     * Returns null if timeout expired when waiting for a packet, otherwise
     * returns packet - either from buffer or just received one.
     */
    public String getPacket(String startString) {
        String p = findPacket(startString);

        /* Packet found in buffer, return it. */
        if (p != null)
            return p;

        /*
         * Packet from specified module not found in buffer, receive packet
         * from PIC until timeout expires.
         */
        while (true) {
            p = recv();

            /* Timer expired, return. */
            if (p == null)
                return null;

            /* Desired packet received, return it. */
            if (p.startsWith(startString))
                return p;

            /*
             * We are not interested in received packet, put it to the buffer.
             */
            bufferPacket(p);
        }
    }

    /**
     * Acquires value measured on given ADC channel.
     * Returns -1 in case of failure.
     */
    public int getAdValue(String channel) {
        if (!sendPacket("A" + channel)) {
            return -1;
        }

        /*
         * Wait for response from PIC module until valid packet is received
         * or timeout expires.
         */
        String p = getPacket("A00");

        /* Timeout expired */
        if (p == null) {
            Log.w(ObdLogger.TAG, String.format("[%.3f] " +
                    "Timeout when waiting for AD packet from" +
                    " PIC module", ObdLogger.getRunTime()));

            return -1;
        }

        int[] picData = new int[2];

        for (int i = 0; i < picData.length; i++) {
            picData[i] = Integer.parseInt(p.substring(
                    3 + (i * 2), 5 + (i * 2)), 16);
        }

        return (picData[0] * 256 + picData[1]);
    }

    /**
     * Configures PIC acquisition engine.
     * Returns -1 in case of failure.
     */
    public int configAcqModule(String config) {
        if (!sendPacket("QC" + config)) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Configures PIC post-processing engine.
     * Returns -1 in case of failure.
     */
    public int configPpModule(String config) {
        if (!sendPacket("QP" + config)) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Start data acquisition
     * Returns -1 in case of failure.
     */
    public int runAcq() {
        if (!sendPacket("QR")) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Stop data acquisition
     * Returns -1 in case of failure.
     */
    public int stopAcq() {
        if (!sendPacket("QS")) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Configures OBD auto-scan mode.
     * Returns -1 in case of failure.
     */
    public int configObdAutoScan(String pidList) {
        if (!sendPacket("EOR" + pidList)) {
            return -1;
        }

        /* Wait just for ACK */
        return waitForAckNak("E", true, false);
    }

    /**
     * Changes ECU communication mode. Returns 0 in case of success,
     * -1 otherwise.
     */
    public int setEcuCommMode(String mode) {
        if (!sendPacket("M" + mode)) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAckNak("M");
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
     * Assembles and sends out KWP2000 packet. Return false if attempt to
     * send the packet fails. Otherwise return true.
     */
    public boolean sendPacket(String p) {
        if (mDebug) {
            Log.d(ObdLogger.TAG, "<- " + p);
        }

        return mPicCom.sendPacket("S" + p);
    }

    /**
     * Waits for KWP2000 packet from PIC module.
     * Returns null if timeout expired.
     */
    public String recvPacket() {
        String p;

        while (true) {
            p = mPicCom.getPacket();

            /* Return null if timeout expired. */
            if (p == null) {
                return null;
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
     * Constructor
     */
    public Obd(Kwp2000 kwp2000) {
        /* Save reference to lower layer communication object. */
        mKwp2000 = kwp2000;

        mPicCom = kwp2000.mPicCom;
    }

    /**
     * Establishes communication with ECU.
     * Returns true if communication has been established, otherwise
     * returns false.
     */
    public boolean open() {
        /* If communication channel is already established, return. */
        if (mComEcuEstablished) {
            return true;
        }

        /*
         * To establish communication with ECU, send 'get pid 00' packet
         * to ECU. Low level part of that handshake is handled by PIC.
         * Let receive state machine process a response.
         */
        int[] p = getPidValue("00");

        /* Timeout expired. */
        if (p == null) {
            mLog.e("Timeout when trying to initiate communication with ECU.");

            return false;
        }

        /* Communication established, set appropriate flag and return. */
        mLog.i("Communication with ECU established.");

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
        int i;

        mComEcuEstablished = false;

        /* Turn off auto-scan mode. */
        if (mPicCom.configObdAutoScan("") == -1)
            ret = false;

        /*
         * Discard buffered packets, as these all belong to communication
         * session which is now being closed. Waits for finite number
         * of packets, so that the method does not hang here in case
         * auto-scan mode has not been successfully canceled.
         */
        for (i = 0; i < 100 && mPicCom.getPacket() != null; i++);

        /* Failed to clean up the buffer. */
        if (i == 100)
            ret = false;

        return ret;
    }

    /**
     * Asks ECU for PID value. Returns null if valid response has not
     * been received, otherwise returns PID value in form of byte array.
     */
    public int[] getPidValue(String pidId) {
        String sp = PKT_GET_PID + pidId;
        String rp = null;

        /* Ask ECU for PID value. Return if send failed. */
        if (!mPicCom.sendPacket(sp)) {
            return null;
        }

        /*
         * Loop until valid packet is received or
         * until receive buffer is emptied.
         */
        while (true) {
            /* Wait for response. */
            rp = mPicCom.getPacket("E");

            /*
             * Timeout expired. Communication needs to be established again.
             */
            if (rp == null) {
                mLog.w("Timeout when waiting for ECU response.");

                mComEcuEstablished = false;

                return null;
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
 * Represents communication service running as a background thread.
 */
class CommunicationService {
    /* Initiates communication with PIC module. */
    private static final int ST_COMM_INIT_PIC = 0;
    /* Set ECU communication mode. */
    private static final int ST_COMM_INIT_ECU_MODE = 1;
    /* Initiates communication with ECU. */
    private static final int ST_COMM_INIT_ECU = 2;
    /* Acquire probe value. */
    private static final int ST_ACQUIRE_PROBE_VALUE = 3;

    private boolean mDebug = false;
    private boolean mQuit = false;
    private int mState;

    /* Runs in debug mode talking to ECU simulator. */
    private boolean mEcuSim = false;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Reference to main Activity */
    private Activity mActivity;

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
     * Constructor
     */
    public CommunicationService(Activity activity, Obd obd) {
        super();

        /* Save reference to OBD communication object */
        mObd = obd;

        /*
         * Determine if we should run in debug mode talking to to ECU simulator.
         * Default to normal mode when talking to real ECU.
         */
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(activity);

        mEcuSim = prefs.getBoolean("pref_debug_mode", false);

        /* Create pool of available probes - TODO read data from external file */
        mProbePool = new HashMap<String, Probe>();

        /* OBD parameters measured and supplied by ECU. */
        mProbePool.put("04", new ObdProbe("04", "%",
                "Calculated Engine Load", "LOAD", "%.0f",
                0.0, 100.0 / 255.0, 0.0, 1.0 / 100.0, obd));

        mProbePool.put("05", new ObdProbe("05", "stC",
                "Engine coolant temperature", "WATER", "%.0f",
                -40, 1, 40.0, 1.0 / (40.0 + 110.0), obd));

        mProbePool.put("0B", new ObdProbe("0B", "kPa",
                "Intake Manifold Absolute Pressure", "AIR", "%.0f",
                0.0, 1.0, 0.0, 1.0 / 255.0, obd));

        mProbePool.put("0C", new ObdProbe("0C", "-",
                "Engine RPM", "RPM", "%.0f",
                0.0, 0.25, 0.0, 1.0 / 5000.0, obd));

        mProbePool.put("0D", new ObdProbe("0D", "km/h",
                "Vehicle Speed", "SPEED", "%.0f",
                0.0, 1.0, 0.0, 1.0 / 195.0, obd));

        mProbePool.put("0F", new ObdProbe("0F", "stC",
                "Intake Air Temperature", "AIR", "%.0f",
                -40, 1, 40.0, 1.0 / (40.0 + 50.0), obd));

        mProbePool.put("10", new ObdProbe("10", "g/s",
                "MAF Air Flow Rate", "MAF", "%.0f",
                0.0, 0.01, 0.0, 1.0 / 150.0, obd));

        mProbePool.put("11", new ObdProbe("11",  "%",
                "Throttle Position", "THROT", "%.0f",
                0.0, 100.0 / 255.0, 0.0, 1.0 / 100.0, obd));

        mProbePool.put("23", new ObdProbe("23", "MPa",
                "Fuel Rail Pressure", "RAIL", "%.0f",
                0.0, 0.01, 0.0, 1.0 / 120.0, obd));

        mProbePool.put("2C", new ObdProbe("2C", "%",
                "Commanded EGR", "EGR", "%.0f",
                0.0, 100.0 / 255.0, 0.0, 0.01, obd));

        mProbePool.put("2D", new ObdProbe("2D", "%",
                "EGR Error", "EGRe", "%.0f",
                -128.0, 100.0 / 128.0, 100.0, 0.01, obd));

        mProbePool.put("33", new ObdProbe("33", "kPa",
                "Barometric Pressure", "BAR", "%.0f",
                0.0, 1.0, 0.0, 1.0 / 255.0, obd));

        /*
         * Battery voltage - measured directly by PIC A/D converter.
         */
        mProbePool.put("Vst", new PicProbe("Vst", "V",
                "Battery Voltage", "Vst", "%.1f",
                0.0, 0.01837, 0.0, 1.0 / (0.01837 * 1024),
                obd.mKwp2000.mPicCom));
        mProbePool.put("Vrms", new PicProbe("Vrms", "-",
                "RMS battery Voltage", "Vrms", "%.0f",
                0.0, 1.0, 0.0, 1.0 / (1.0 * 1024),
                obd.mKwp2000.mPicCom));
        mProbePool.put("Vmin", new PicProbe("Vmin", "-",
                "Min battery Voltage", "Vmin", "%.0f",
                0.0, 1.0, 0.0, 1.0 / (1.0 * 1024),
                obd.mKwp2000.mPicCom));
        mProbePool.put("Vmax", new PicProbe("Vmax", "-",
                "Max battery Voltage", "Vmax", "%.0f",
                0.0, 1.0, 0.0, 1.0 / (1.0 * 1024),
                obd.mKwp2000.mPicCom));
        mProbePool.put("F1", new PicProbe("F1", "-",
                "Frequency 1", "F1", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("M1", new PicProbe("M1", "-",
                "Amplitude 1", "M1", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("F2", new PicProbe("F2", "-",
                "Frequency 2", "F2", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("M2", new PicProbe("M2", "-",
                "Amplitude 2", "M2", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("F3", new PicProbe("F3", "-",
                "Frequency 3", "F3", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("M3", new PicProbe("M3", "-",
                "Amplitude 3", "M3", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("F4", new PicProbe("F4", "-",
                "Frequency 4", "F4", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("M4", new PicProbe("M4", "-",
                "Amplitude 4", "M4", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("F5", new PicProbe("F5", "-",
                "Frequency 5", "F5", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));
        mProbePool.put("M5", new PicProbe("M5", "-",
                "Amplitude 5", "M5", "%.0f",
                0.0, 1.0, 0.0, 1.0, obd.mKwp2000.mPicCom));

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
        mStsPic = (Button) mActivity.findViewById(R.id.picState);
        mStsEcu = (Button) mActivity.findViewById(R.id.ecuState);
        mStsGps = (Button) mActivity.findViewById(R.id.gpsState);

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
     * Signals to communication thread that it should terminate.
     */
    public void finish() {
        mQuit = true;
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
     * Configure data acquisition module either for capturing battery voltage
     * during engine starting phase or for measuring charging voltage when
     * engine is running.
     *
     * @param  startingPhase true if configure for capturing voltage
     *                       during starting phase
     * @return               True if module was successfully configured,
     *                       false otherwise.
     */
    private boolean confAcqModule(boolean startingPhase) {
        PicCom pc = mObd.mKwp2000.mPicCom;

        /*
         * Make sure acquisition module is idle as well as turn off
         * post-processing module.
         */
        if (pc.stopAcq() == -1) {
            mLog.w("Failed to stop data acquisition.");

            return false;
        }

        if (pc.configPpModule("00") == -1) {
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
            if (pc.configAcqModule("0801010102DF00A1000000000096") == -1) {
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
            if (pc.configAcqModule("08020201000019001F4100800080") == -1) {
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
            if (pc.configPpModule("02A") == -1) {
                mLog.w("Failed to activate post-processing module.");

                return false;
            }
        }

        /* Start data acquisition */
        if (pc.runAcq() == -1) {
            mLog.w("Failed to start data acquisition.");

            return false;
        }

        return true;
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

            while (!mQuit) {
                /* Establish communication with PIC module */
                if (mState == ST_COMM_INIT_PIC) {
                    if (!pc.open()) {
                        /* Sleep for a while and retry. */
                        try {
                            Thread.sleep(1000);
                        } catch (InterruptedException e) {
                            Log.w(ObdLogger.TAG,
                                    "Thread CommunicationService " +
                                    "interrupted.", e);
                        }

                        continue;
                    }

                    /* Update status indicator */
                    updateStatusIndicator(mStsPic, true);

                    mState = ST_COMM_INIT_ECU_MODE;
                } else if (mState == ST_COMM_INIT_ECU_MODE) {
                    String mode;

                    /* Set ECU communication mode. */
                    if (mEcuSim) {
                        mode = PicCom.ECU_COMM_MODE_KWP2K_DEBUG;
                    } else {
                        mode = PicCom.ECU_COMM_MODE_KWP2K_ENHANCED;
                    }

                    int res = pc.setEcuCommMode(mode);

                    /* Command failed, try to re-connect to PIC module. */
                    if (res == -1) {
                        mLog.w("Failed to configure communication ECU mode.");

                        mState = ST_COMM_INIT_PIC;
                    } else {
                        mState = ST_COMM_INIT_ECU;
                    }
                } else if (mState == ST_COMM_INIT_ECU) {
                /* Establish communication with ECU */

                    /*
                     * Wait until communication is established. Any packets
                     * previously received from PIC module are discarded.
                     */
                    if (!mObd.open()) {
                        /* Sleep for a second and retry. */
                        try {
                            Thread.sleep(1000);
                        } catch (InterruptedException e) {
                            Log.w(ObdLogger.TAG,
                                    "Thread CommunicationService " +
                                    "interrupted.", e);
                        }

                        /*
                         * One of the reasons the communication with ECU
                         * can't be established is that the logger currently
                         * operates in different communication mode
                         * (real versus simulation).
                         * If user just changed the mode to address that
                         * problem, change the mode first to reflect current
                         * user's choice, then try to re-establish communication
                         * with ECU.
                         * That delivers slightly better user experience,
                         * since there is no need to restart application after
                         * correct communication mode was selected.
                         */

                        SharedPreferences prefs = PreferenceManager
                                .getDefaultSharedPreferences(mActivity);

                        boolean m = prefs.getBoolean("pref_debug_mode", false);

                        if (m != mEcuSim) {
                            mEcuSim = m;

                            mState = ST_COMM_INIT_ECU_MODE;
                        }

                        continue;
                    }

                    /* Let user know via indicator on status bar. */
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
                    v[0] = pc.getAdValue("08");
                    double batt = v[0] * 0.01837;

                    /* Failed to obtain battery voltage, skip this phase. */
                    if (v[0] == -1) {
                        mLog.w("Failed to obtain battery voltage.");

                        captureStartVoltage = false;
                    } else if (batt > 13.0) {
                        /* Engine running */
                        captureStartVoltage = false;

                        /*
                         * Configure and start OBD auto-scan mode.
                         * Re-try in case of failure.
                         */
                        if (pc.configObdAutoScan(mPidAutoScanList) == -1) {
                            mLog.w("Failed to configure OBD auto-scan.");

                            continue;
                        }
                    } else {
                        /* Engine idle, measure starting voltage. */
                        captureStartVoltage = true;
                    }

                    if (!confAcqModule(captureStartVoltage)) {
                        /* Failed to configure data acq module, re-try. */
                        continue;
                    }

                    /* Update battery voltage on dashboard. */
                    Probe battProbe = mProbePool.get("Vst");

                    /* Update appropriate probe */
                    if (battProbe.update(v)) {
                        /* Update indicator with new value */
                        mDashboard.updateIndicator(battProbe);
                    }

                    mState = ST_ACQUIRE_PROBE_VALUE;
                } else if (mState == ST_ACQUIRE_PROBE_VALUE) {
                    int tout;
                    String p;

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
                    do {
                        p = pc.getPacket();
                    } while (p == null && --tout > 0 && !mQuit);

                    /* Application finished. */
                    if (mQuit) {
                        continue;
                    }

                    /* Timeout occurred */
                    if (p == null) {
                        mLog.w("Timeout when waiting for packet from PIC.");

                        /*
                         * Communication with PIC module is broken,
                         * try to establish it again.
                         */
                        /* Let user know via indicator on status bar. */
                        updateStatusIndicator(mStsEcu, false);
                        updateStatusIndicator(mStsPic, false);

                        mLog.w("Communication with PIC module lost.");

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

                        mState = ST_COMM_INIT_PIC;

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
                        pc.stopAcq();

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

                            /* Configure and start OBD auto-scan mode */
                            if (pc.configObdAutoScan(mPidAutoScanList) == -1) {
                                /* Failed to configure auto-scan mode, retry. */
                                mState = ST_COMM_INIT_ECU;
                                continue;
                            }

                            /* Reconfigure acquisition module. */
                            confAcqModule(false);

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

            /* Stop acquisition */
            pc.stopAcq();

            /* Close ECU communication channel. */
            if (!mObd.close())
                mLog.w("Failed to close ECU communication channel.");

            /* Close lower layer communication service. */
            pc.close();
        }
    };
}

public class ObdLogger extends Activity {
    static final String TAG = "ObdLogger";
    static final int REQUEST_ENABLE_BT = 1;
    static final int SET_DEBUG_MODE_ID = 1;

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

    /* Bluetooth adapter */
    private BluetoothAdapter mBluetoothAdapter = null;
    /* Bluetooth communication socekt */
    private BluetoothSocket mBtSocket = null;
    /* Bluetooth input and output communication streams */
    private InputStream mBluetoothIn = null;
    private OutputStream mBluetoothOut = null;

    /**
     * Establishes connection to PIC module
     */
    private void connectToPic() {

        /* Cancel in-progress discovery */
        if (mBluetoothAdapter.isDiscovering()) {
            Log.d(TAG, "Canceling Bluetooth discovery process...");

            if (mBluetoothAdapter.cancelDiscovery()) {
                Log.d(TAG, "Done.");
            } else {
                Log.d(TAG, "Failed.");
            }
        }

        /* Search for PIC module connected via Bluetooth */
        Set <BluetoothDevice> pairedDevices = mBluetoothAdapter.getBondedDevices();

        /* There are no paired devices, exit */
        if (pairedDevices.size() == 0) {
            Toast.makeText(this, "No OBD Bluetooth paired devices found.",
                    Toast.LENGTH_LONG).show();

            finish();
            return;
        }

        /* Loop through paired devices */
        for (BluetoothDevice sppDevice : pairedDevices) {
            /* Display device name and address */
            Toast.makeText(this, sppDevice.getName() + " - " +
                    sppDevice.getAddress(), Toast.LENGTH_LONG).show();

            /* If it is PIC module, establish a connection */
            if (sppDevice.getName().equals("OBD converter")) {
                try {
                    mBtSocket = sppDevice.createRfcommSocketToServiceRecord(
                            UUID.fromString(
                            "00001101-0000-1000-8000-00805F9B34FB"));
                } catch (IOException e) {
                    Toast.makeText(this, "Failed to establish connection " +
                            "to PIC module.", Toast.LENGTH_LONG).show();

                    finish();
                    return;
                }
            }
        }

        /* PIC module not found */
        if (mBtSocket == null) {
            Toast.makeText(this, "PIC module not found",
                    Toast.LENGTH_LONG).show();

            finish();
            return;
        }

        /*
         * Connect the device through the socket. This will block
         * until it succeeds or throws an exception.
         */
        try {
            Log.d(TAG, "Connecting to Bluetooth socket...");

            mBtSocket.connect();
        } catch (IOException connectException) {
            Log.d(TAG, "Failed.");

            Toast.makeText(this, "Unable to connect " +
                    "to PIC module.", Toast.LENGTH_LONG).show();

            mLog.e("Unable to connect to PIC module, aborting.");

            /* Unable to connect; close the socket and get out */
            try {
                Log.d(TAG, "Closing Bluetooth communication socket...");
                mBtSocket.close();
            } catch (IOException e) {
                Log.e(TAG, "Failed to close Bluetooth socket.", e);
                Toast.makeText(this, "Unable to close the socket",
                        Toast.LENGTH_LONG).show();
            }

            Log.d(TAG, "Bluetooth socket closed.");

            finish();
            return;
        }

        /* Get the input and output streams */
        try {
            mBluetoothIn = mBtSocket.getInputStream();
            mBluetoothOut = mBtSocket.getOutputStream();
        } catch (IOException e) {
            Toast.makeText(this, "Unable to create communication streams",
                    Toast.LENGTH_LONG).show();

            finish();
            return;
        }

        /*
         * Instantiates OBD communication module along with all lower
         * layer communication modules.
         */
        mObd = new Obd(new Kwp2000(new PicCom(mBluetoothIn, mBluetoothOut)));

        /* Instantiates communication service */
        mComService = new CommunicationService(this, mObd);

        /* Creates background thread handling communication with PIC module. */
        commThread = new Thread(null, mComService.loop, "ComService");

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
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

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
            connectToPic();
        }
    }

    /** Called when Menu button is pressed. */
    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        menu.add(Menu.NONE, SET_DEBUG_MODE_ID, Menu.NONE, "Settings");

        return(super.onCreateOptionsMenu(menu));
    }

    /** Called when item from Menu is selected. */
    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case SET_DEBUG_MODE_ID:
                startActivity(new Intent(this, EditPreferences.class));
                return(true);
        }

        return(super.onOptionsItemSelected(item));
    }

    /** Called when the activity is started. */
    @Override
    public void onStart() {
        super.onStart();

        Log.d(TAG, "-> onStart()");
        Log.d(TAG, "<- onStart()");
    }

    /** Called when the activity is paused. */
    @Override
    public void onPause() {
        super.onPause();

        Log.d(TAG, "-> onPause()");
        Log.d(TAG, "<- onPause()");
    }

    /** Called when the activity is resumed. */
    @Override
    public void onResume() {
        super.onResume();

        Log.d(TAG, "-> onResume()");
        Log.d(TAG, "<- onResume()");
    }

    /** Called when the activity is restarted. */
    @Override
    public void onRestart() {
        super.onRestart();

        Log.d(TAG, "-> onRestart()");
        Log.d(TAG, "<- onRestart()");
    }

    /** Called when the activity is stopped. */
    @Override
    public void onStop() {
        super.onStop();

        Log.d(TAG, "-> onStop()");
        Log.d(TAG, "<- onStop()");
    }

    /** Called when the activity is destroyed. */
    @Override
    public void onDestroy() {
        super.onDestroy();

        Log.d(TAG, "-> onDestroy()");

        /*
         * If communication thread was spawned, let it know
         * it should terminate its job.
         */
        if (mComService != null && commThread != null) {
            mComService.finish();

            /* Wait until the thread quits - set timeout to 5 seconds */
            try {
                commThread.join(5000);
            } catch (InterruptedException e) {
                /* Could not join communication thread */
                Log.e(TAG, "Interrupted while sleeping.", e);
            }

            if (commThread.isAlive()) {
                /* Could not join communication thread */
                Log.e(TAG, "Failed to join communication thread.");
            } else {
                Log.i(TAG, "Communication thread joined.");
            }
        }

        if (mBtSocket != null) {
            /* Close Bluetooth communication socket */
            Log.d(TAG, "Closing Bluetooth communication socket...");
            try {
                mBtSocket.close();
                Log.d(TAG, "Bluetooth socket closed.");
            } catch (IOException e) {
                Log.e(TAG, "Failed to close Bluetooth socket.", e);
            }
        }

        mLog.i("Application finished.");

        /* Close log files */
        mLog.finish();

        Log.d(TAG, "<- onDestroy()");
    }

    /**
     * Called when the concurrently running activity
     * spawned by startActivityForResult() is done.
     */
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.d(TAG, "-> onActivityResult " + resultCode);

        switch (requestCode) {
        case REQUEST_ENABLE_BT:
            /* Request to enable Bluetooth returned */
            if (resultCode == Activity.RESULT_OK) {
                /* Bluetooth is now enabled, connect to PIC module */
                connectToPic();
            } else {
                /* User did not enable Bluetooth or an error occurred */
                Log.d(TAG, "BT not enabled");

                Toast.makeText(this, "Bluetooth not enabled",
                        Toast.LENGTH_LONG).show();

                finish();
            }
        }

        Log.d(TAG, "<- onActivityResult");
    }
}

