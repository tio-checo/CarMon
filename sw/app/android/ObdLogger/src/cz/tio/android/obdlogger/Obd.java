package cz.tio.android.obdlogger;

import android.app.Activity;
import android.util.Log;

import cz.tio.android.obdlogger.LogMaster;
import cz.tio.android.obdlogger.PicCom;
import cz.tio.android.obdlogger.PicConnectionLostException;
import cz.tio.android.obdlogger.PicTimeoutExpiredException;
import cz.tio.android.obdlogger.ObdProbe;
import cz.tio.android.obdlogger.Probe;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/**
 * Represents callback routines which are called when
 * connection to ECU is established or lost.
 */
interface IEcuComCallback {
    /**
     * Called when connection with ECU is established.
     */
    abstract public void onConnectionEstablished();

    /**
     * Called when connection with ECU is lost.
     */
    abstract public void onConnectionLost();
}

/**
 * Null callback object used as a default if one is not provided
 * when Obd object is instantiated.
 */
class NullEcuComCallback implements IEcuComCallback {
    /**
     * Called when connection with PIC module is established.
     */
    public void onConnectionEstablished() {
    }

    /**
     * Called when connection with PIC module is lost.
     */
    public void onConnectionLost() {
    }

    /**
     * Constructor
     */
    public NullEcuComCallback() {
    }
}

/**
 * Implements OBD communication protocol.
 */
public class Obd {
    /* ECU communication modes */
    static final String ECU_COMM_MODE_KWP2K_AUTO = "00";
    static final String ECU_COMM_MODE_KWP2K_NORMAL = "01";
    static final String ECU_COMM_MODE_KWP2K_DEBUG = "02";
    static final String ECU_COMM_MODE_KWP2K_MIXED = "03";
    static final String ECU_COMM_MODE_CAN_SID_250 = "04";
    static final String ECU_COMM_MODE_CAN_SID_500 = "05";
    static final String ECU_COMM_MODE_CAN_EID_250 = "06";
    static final String ECU_COMM_MODE_CAN_EID_500 = "07";

    static final String PKT_ACK = "EA";
    static final String PKT_NAK = "EN";
    static final String PKT_GET_CURR_PID = "EOP01";
    static final String PKT_GET_CURR_PID_RESP = "EP00";
    static final String PKT_GET_CURR_PID_AS = "EOp";
    static final String PKT_GET_CURR_PID_AS_RESP = "Ep";
    static final String PKT_GET_FF_PID = "EOF";
    static final String PKT_GET_FF_PID_RESP = "EF00";
    static final String PKT_GET_PERM_DTC = "EOD";
    static final String PKT_GET_PERM_DTC_RESP = "ED00";
    static final String PKT_CLEAR_DTC = "EOR";
    static final String PKT_SET_COMM_MODE = "EOC";

    /* private attributes */
    private static final int ST_COMM_INIT = 0;
    private static final int ST_GET_PID = 1;

    /* Reference to lower communication layer */
    private PicCom mPicCom;

    /**
     * Pool of all available probes in form of dictionary.
     * Probe IDs (String) is used as a key.
     */
    public HashMap<String, Probe> mProbePool;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Reference to callback routines */
    private IEcuComCallback mEcuComCb;

    /* Debug mode */
    private boolean mDebug = false;

    /* Indicates if communication with ECU is established */
    private boolean mConnectionEstablished = false;

    /* Caches list of supported PIDs */
    private ArrayList<String> mSupportedPids = null;

    /**
     * Establishes communication with ECU.
     *
     * @return  <code>true</code> if communication has been established;
     *          <code>false</code> otherwise.
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    public boolean open() throws PicConnectionLostException {
        /* If communication channel is already established, return. */
        if (mConnectionEstablished) {
            return true;
        }

        /*
         * To establish communication with ECU, send 'get pid 00' packet
         * to ECU. Low level part of that handshake is handled by PIC.
         * Let receive state machine process a response.
         */
        try {
            if (getPidValue("00") == null) {
                mLog.e("obd.open(): ECU not responded.");

                return false;
            }

            /*
             * Scaling per bit for PID 0B (Intake Manifold Absolute Pressure)
             * can be adjusted by 4.byte of PID 4F. Handle that here.
             * Should it turn out in future that there are more PIDs which need
             * to be handled, more generic approach should be introduced.
             */
            if (isPidSupported("4F")) {
                int v[] = getPidValue("4F");

                if ((v != null) && (v.length == 4) && (v[3] != 0)) {
                    ObdProbe p = (ObdProbe) getProbe("0B");

                    p.adjustRawToPhysMul(10.0 * v[3] / 255.0);
                }
            }

        } catch (PicTimeoutExpiredException e) {
            mLog.e("Timeout when trying to initiate communication with ECU.");

            return false;
        } catch (PicConnectionLostException e) {
            mLog.e("Lost connection with PIC when trying to initiate " +
                    "communication with ECU.");

            throw e;
        }

        mConnectionEstablished = true;

        /* Call related callback routine. */
        mEcuComCb.onConnectionEstablished();

        return true;
    }

    /**
     * Finishes communication with ECU.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     */
    public boolean close() {
        /* Assume success. */
        boolean ret = true;
        int i = 0;

        /* Turn off auto-scan mode. */
        if (mConnectionEstablished) {
            try {
                configObdAutoScan("", "");
            } catch (PicTimeoutExpiredException e) {
                ret = false;
            } catch (PicConnectionLostException e) {
                ret = false;
            }
        }

        mConnectionEstablished = false;

        /* Call related callback routine. */
        mEcuComCb.onConnectionLost();

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
     * Checks if connected to ECU.
     *
     * @return   <code>true</code> if connected to ECU;
     *           <code>false</code> otherwise.
     */
    public boolean connectionEstablished() {
        return mConnectionEstablished;
    }

    /**
     * Returns description for given PID.
     *
     * @param   pid the PID
     * @return      String describing given PID
     */
    public String getDescription(String pid) {
        /* Return empty string for unknown PIDs. */
        Probe probe = getProbe(pid);

        if (probe == null)
            return "";

        return probe.getDescription();
    }

    /**
     * Returns reference to probe stored in probe pool.
     *
     * @param   id  the probe ID
     * @return      reference to probe; <code>null</code> if probe with given
     *              ID does not exist.
     */
    public Probe getProbe(String probeId) {
        return mProbePool.get(probeId);
    }

    /**
     * Returns reference to underlying PicCom communication object.
     *
     * @return      reference to PicCom communication object
     */
    public PicCom getPicCom() {
        return mPicCom;
    }

    /**
     * Selects ECU communication mode.
     *
     * @param    mode the communication mode
     * @return   <code>true</code> in case of success;
     *           <code>false</code> otherwise.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public boolean setEcuCommMode(String mode)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        String sp = PKT_SET_COMM_MODE + mode;
        String rp = null;

        /* Ask ECU for PID value. */
        mPicCom.sendPacket(sp);

        /* Wait until ACK or NAK is received. */
        try {
            /* Wait for response from ECU module. */
            rp = mPicCom.getPacket("E");
        } catch (PicTimeoutExpiredException e) {
            /*
             * Timeout expired. Communication needs to be established again.
             */
            mLog.w("Timeout when waiting for ECU response.");

            mConnectionEstablished = false;

            /* Call related callback routine. */
            mEcuComCb.onConnectionLost();

            throw e;
        } catch (PicConnectionLostException e) {
            mConnectionEstablished = false;

            /* Call related callback routine. */
            mEcuComCb.onConnectionLost();

            throw e;
        }

        /*
         * ACK received.
         */
        if (rp.startsWith(PKT_ACK)) {
            return true;
        } else if (rp.startsWith(PKT_NAK)) {
            /*
             * NAK received, log it.
             */
            mLog.w(String.format(
                    "ECU responded with NAK <%s> to <%s> packet.", rp, sp));
        } else {
            /*
             * Unknown response received, log it.
             */
            mLog.w(String.format(
                    "ECU responded with invalid <%s> to <%s> packet.", rp, sp));
        }

        return false;
    }

    /**
     * Asks ECU for current PID value.
     *
     * @param   pidId   the id of requested PID
     * @return          PID value in form of byte array
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    public int[] getPidValue(String pidId)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        return getPidValueImpl(pidId, -1);
    }

    /**
     * Configures OBD PID auto-scan mode.
     *
     * @param    pidList                    the list of PIDs to be scanned
     * @param    sampleRate                 4 HEX ASCII characters specifying
     *                                      sample rate in [ms]
     * @return
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int configObdAutoScan(String pidList, String sampleRate)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        mPicCom.sendPacket(PKT_GET_CURR_PID_AS + sampleRate + pidList);

        /* Wait just for ACK */
        return mPicCom.waitForAckNak("E", true, false);
    }

    /**
     * Asks ECU for freeze frame PID value.
     *
     * @param   pidId           the id of requested PID
     * @param   freezeFrameNum  the freeze frame number
     * @return                  PID value in form of byte array
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    public int[] getFreezeFramePidValue(String pidId, int freezeFrameNum)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        return getPidValueImpl(pidId, freezeFrameNum);
    }

    /**
     * Asks ECU for list of PIDs supported in 'live data' mode.
     * Result is cached.
     *
     * @return   list of supported PIDs; <code>null</code>in case of failure.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    public ArrayList<String> getSupportedPids()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        if (mSupportedPids == null) {
            mSupportedPids = getListOfAvailablePids(-1);
        }

        return mSupportedPids;
    }

    /**
     * Checks if ECU supports particular PID.
     *
     * @param    pidId the id of checked PID
     * @return   <code>true</code> if PID is supported;
     *           <code>false</code> otherwise.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    public boolean isPidSupported(String pidId)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        ArrayList<String> suppPids = getSupportedPids();

        return suppPids.contains(pidId);
    }

    /**
     * Asks ECU for list PIDs captured for particular freeze frame.
     *
     * @param   freezeFrameNum  The freeze frame number. -1 indicates that
     *                          list of PIDs supported in 'live data' mode
     *                          should be obtained instead.
     * @return   list of freeze frame PIDs; <code>null</code>in case of failure.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    public ArrayList<String> getFreezeFramePids(int freezeFrameNum)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        return getListOfAvailablePids(freezeFrameNum);
    }

    /**
     * Constructs list of logged freeze frames.
     *
     * @return   list of DTCs identifying logged freeze frames
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    public ArrayList<String> getFreezeFrameList()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        ArrayList<String> freezeFrameList = new ArrayList<String>();
        int freezeFrameNum;

        /*
         * Try to determine DTC associated with particular freeze frame.
         * Stop once we got invalid response.
         * No more than 256 freeze frames are to be processed.
         */
        for (int i = 0; i < 256; i++) {
            int dtc;
            int [] v = getFreezeFramePidValue("02", i);

            /* ECU did not respond - assume that freeze frame does not exist. */
            if (v == null) {
                break;
            }

            /* Invalid data length. */
            if (v.length != 2) {
                break;
            }

            dtc = v[0] * 256 + v[1];

            /* Zero means "no DTC". */
            if (dtc == 0) {
                break;
            }

            /* Convert DTC number to String and add it to the list. */
            freezeFrameList.add(convertDtcRawToString(dtc));
        }

        return freezeFrameList;
    }

    /**
     * Asks ECU for total number of logged permanent DTCs.
     *
     * @return  number of logged permanent DTCs
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                     waiting for data from PIC
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    public int getPermDtcNum()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        /*
         * Number of logged permanent DTC is obtained from the first byte
         * of PID 01.
         */
        int v[] = getPidValue("01");

        /* Failed to obtain value of PID "01". */
        if ((v == null) || (v.length != 4)) {
            mLog.w("Failed to obtain number of DTCs logged.");

            return 0;
        }

        /* Filter our bit 7 - it contains status of MIL indicator. */
        return v[0] & 0x7f;
    }

    /**
     * Asks ECU for permanent DTC.
     *
     * @param   dtcNum  the number of permanent DTC to be obtained
     * @return  number of logged permanent DTCs
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                     waiting for data from PIC
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    public String getPermDtc(int dn)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        String sp = PKT_GET_PERM_DTC + String.format("%02X", dn);
        String rph = PKT_GET_PERM_DTC_RESP;
        String rp = null;

        /* Ask ECU for DTC value. */
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
                /* Timeout expired, return. */
                mLog.w("Timeout when waiting for ECU response.");

                mConnectionEstablished = false;

                /* Call related callback routine. */
                mEcuComCb.onConnectionLost();

                throw e;
            } catch (PicConnectionLostException e) {
                mConnectionEstablished = false;

                /* Call related callback routine. */
                mEcuComCb.onConnectionLost();

                throw e;
            }

            /* NAK received, log it and return. */
            if (rp.startsWith(PKT_NAK)) {
                mLog.w(String.format(
                        "ECU responded with NAK <%s> to <%s> packet.", rp, sp));

                return null;
            }

            /* Valid packet received. Escape from the loop. */
            if (rp.startsWith(rph)) {
                break;
            }

            /* Invalid packet received. Log this and wait for another one. */
            mLog.w(String.format(
                    "Invalid ECU response, awaiting <%s>, got <%s>.", rph, rp));
        }

        /* Validate packet length. */
        if (rp.length() != 8) {
            mLog.w(String.format(
                    "Invalid packet length, awaiting 8, got <%d>.",
                    rp.length()));

            return null;
        }

        int dtcRaw = 0;

        for (int i = 0; i < 2; i++) {
            dtcRaw *= 256;
            dtcRaw += Integer.parseInt(rp.substring(4 + (i * 2),
                    4 + ((i + 1) * 2)), 16);
        }

        return convertDtcRawToString(dtcRaw);
    }

    /**
     * Asks ECU to clear DTCs, freeze frame data, ...
     *
     * @return  <code>true</code> if operation succeeded;
     *          <code>false</code> otherwise.
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                     waiting for data from PIC
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    public boolean clearDtc()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        String sp = PKT_CLEAR_DTC;
        String rp = null;

        /* Instructs ECU to clear DTCs. */
        mPicCom.sendPacket(sp);

        /* Wait until ACK or NAK is received. */
        try {
            /* Wait for response from ECU module. */
            rp = mPicCom.getPacket("E");
        } catch (PicTimeoutExpiredException e) {
            /*
             * Timeout expired. Communication needs to be established again.
             */
            mLog.w("Timeout when waiting for ECU response.");

            mConnectionEstablished = false;

            /* Call related callback routine. */
            mEcuComCb.onConnectionLost();

            throw e;
        } catch (PicConnectionLostException e) {
            mConnectionEstablished = false;

            /* Call related callback routine. */
            mEcuComCb.onConnectionLost();

            throw e;
        }

        /*
         * ACK received.
         */
        if (rp.startsWith(PKT_ACK)) {
            return true;
        } else if (rp.startsWith(PKT_NAK)) {
            /*
             * NAK received, log it.
             */
            mLog.w(String.format(
                    "ECU responded with NAK <%s> to <%s> packet.", rp, sp));
        } else {
            /*
             * Unknown response received, log it.
             */
            mLog.w(String.format(
                    "ECU responded with invalid <%s> to <%s> packet.", rp, sp));
        }

        return false;
    }

    /**
     * Converts DTC raw data to String.
     *
     * @param dtc    the DTC raw data
     * @return       string describing the DTC
     */
    public String convertDtcRawToString(int dtc) {
        String dtcType;

        /* Decode DTC type. */
        switch ((dtc >> 14) & 0x03) {
        /* Powertrain */
        case 0:
            dtcType = "P";
            break;

        /* Chassis */
        case 1:
            dtcType = "C";
            break;

        /* Body */
        case 2:
            dtcType = "B";
            break;

        /* Network */
        case 3:
            dtcType = "U";
            break;

        /* Just to satisfy the compiler. */
        default:
            dtcType = "x";
            break;
        }

        /* Decode DTC number. */
        return dtcType + String.format("%04X", dtc & 0x3fff);
    }

    /**
     * Constructor
     *
     * @param picCom    the reference to underlying communication object
     * @param activity  the reference to parent activity
     */
    public Obd(PicCom picCom, Activity activity) {
        this(picCom, activity, new NullEcuComCallback());
    }

    /**
     * Constructor
     *
     * @param picCom    the reference to underlying communication object
     * @param activity  the reference to parent activity
     * @param ecuComCb  the callback routines
     */
    public Obd(PicCom picCom, Activity activity, IEcuComCallback ecuComCb) {
        /* Save reference to lower layer communication object. */
        mPicCom = picCom;

        /* Save reference to callback routines. */
        mEcuComCb = ecuComCb;

        /* Create pool of available probes. */
        mProbePool = new HashMap<String, Probe>();

        /* Populate list of known OBD probes from probes.xml file. */
        XmlPullParser xpp = activity.getResources().getXml(R.xml.probes);

        /*
         * True indicates we are processing OBD probes. False indicates
         * PIC probes.
         */
        boolean processingObdProbes = false;

        try {
            while (xpp.getEventType() != XmlPullParser.END_DOCUMENT) {
                String id = "";
                String description = "";
                String label = "";
                String units = "";
                double raw2PhysAdd = 0.0;
                double raw2PhysMul = 0.0;
                double phys2PbAdd = 0.0;
                double phys2PbMul = 0.0;
                String phys2String = "";

                /* Search for start tag. */
                if (xpp.getEventType() != XmlPullParser.START_TAG) {
                    xpp.next();
                    continue;
                }

                if (xpp.getName().equals("ObdProbes")) {
                /* Processing list of OBD probes. */
                    processingObdProbes = true;

                    xpp.next();
                    continue;
                } else if (xpp.getName().equals("PicProbes")) {
                /* Processing list of PIC probes. */
                    processingObdProbes = false;

                    xpp.next();
                    continue;
                } else if (!xpp.getName().equals("Pid")) {
                /* Search for new probe to process. */
                    xpp.next();
                    continue;
                }

                /* Process probe. */
                String currentTag = null;
                boolean processingDone = false;
                do {
                    xpp.next();

                    if (xpp.getEventType() == XmlPullParser.START_TAG) {
                        currentTag = xpp.getName();
                    } else if (xpp.getEventType() == XmlPullParser.TEXT) {
                        if (currentTag.equals("Id")) {
                            id = xpp.getText();
                        } else if (currentTag.equals("Description")) {
                            description = xpp.getText();
                        } else if (currentTag.equals("Label")) {
                            label = xpp.getText();
                        } else if (currentTag.equals("Units")) {
                            units = xpp.getText();
                        } else if (currentTag.equals("RawToPhysAdd")) {
                            raw2PhysAdd = Double.valueOf(xpp.getText());
                        } else if (currentTag.equals("RawToPhysMul")) {
                            raw2PhysMul = Double.valueOf(xpp.getText());
                        } else if (currentTag.equals("PhysToPbAdd")) {
                            phys2PbAdd = Double.valueOf(xpp.getText());
                        } else if (currentTag.equals("PhysToPbMul")) {
                            phys2PbAdd = Double.valueOf(xpp.getText());
                        } else if (currentTag.equals("PhysToString")) {
                            phys2String = xpp.getText();
                        }
                    } else if (xpp.getEventType() == XmlPullParser.END_TAG) {
                        if (xpp.getName().equals("Pid")) {
                            Log.d("CommSvc", String.format("Pid: I=%s, U=%s, " +
                                    "D=%s, L=%s, S=%s, d1=%f, d2=%f, d3=%f, " +
                                    "d4=%f",
                                    id, units,
                                    description, label, phys2String,
                                    raw2PhysAdd, raw2PhysMul,
                                    phys2PbAdd, phys2PbMul));

                            /* "Pid" end tag reached, store the probe. */
                            Probe newProbe;
                            if (processingObdProbes) {
                                /* OBD probe */
                                newProbe = new ObdProbe(id, units,
                                    description, label, phys2String,
                                    raw2PhysAdd, raw2PhysMul,
                                    phys2PbAdd, phys2PbMul,
                                    this);
                            } else {
                                /* PIC probe */
                                newProbe = new PicProbe(id, units,
                                    description, label, phys2String,
                                    raw2PhysAdd, raw2PhysMul,
                                    phys2PbAdd, phys2PbMul,
                                    this.getPicCom());
                            }
                            mProbePool.put(id, newProbe);

                            processingDone = true;
                        }
                    }
                } while (!processingDone);

                xpp.next();
            }
        } catch (IOException e) {
            Log.e("CommSvc", "IOException exception thrown when " +
                    "processing XML OBD PID file.");
        } catch (XmlPullParserException e) {
            Log.e("CommSvc", "XmlPullParserException exception thrown when " +
                    "processing XML OBD PID file.");
        }

        /*
         * 'Turbo' probe. Calculated as Intake pressure/Barometric pressure
         * ratio.
         */
        mProbePool.put("Turbo", new TurboProbe());

        /* EGT probes. */
        mProbePool.put("EGT11", new EgtProbe("EGT11", "78", 0));
        mProbePool.put("EGT12", new EgtProbe("EGT12", "78", 1));

        /* DPF PM Accumulation Ratio probe. */
        mProbePool.put("DPFar", new CompoundProbe("DPFar", "DD", 21, 1));
    }

    /**
     * Determines if freeze frame data are to be obtained.
     *
     * @param   freezeFrameNum  The freeze frame number. -1 indicates that
     *                          current data should be obtained instead.
     * @return  <code>true</code> if freeze frame data are requested;
     *          <code>false</code> otherwise.
     */
    private boolean isFreezeFrame(int freezeFrameNum) {
        return (freezeFrameNum != -1);
    }

    /**
     * Asks ECU for PID value - either Freeze Frame PID value or current PID
     * value.
     *
     * @param   pidId           the id of requested PID
     * @param   freezeFrameNum  The freeze frame number. -1 indicates that
     *                          current PID value should be obtained instead.
     *
     * @return                  PID value in form of byte array
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                     waiting for data from PIC
     * @throws  PicConnectionLostException when lost connection with PIC
     */
    private int[] getPidValueImpl(String pidId, int freezeFrameNum)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        String sp = isFreezeFrame(freezeFrameNum) ? PKT_GET_FF_PID :
                PKT_GET_CURR_PID;
        String rph = isFreezeFrame(freezeFrameNum) ? PKT_GET_FF_PID_RESP :
                PKT_GET_CURR_PID_RESP;
        String rp = null;

        /*
         * If appropriate, add freeze frame number to both command
         * as well as response.
         */
        if (isFreezeFrame(freezeFrameNum)) {
            sp += String.format("%02X", freezeFrameNum);
            rph += String.format("%02X", freezeFrameNum);
        }

        sp += pidId;
        rph += pidId;

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
                /* Timeout expired, return. */
                mLog.w("Timeout when waiting for ECU response.");

                mConnectionEstablished = false;

                /* Call related callback routine. */
                mEcuComCb.onConnectionLost();

                throw e;
            } catch (PicConnectionLostException e) {
                mConnectionEstablished = false;

                /* Call related callback routine. */
                mEcuComCb.onConnectionLost();

                throw e;
            }

            /* NAK received, log it and return. */
            if (rp.startsWith(PKT_NAK)) {
                mLog.w(String.format(
                        "ECU responded with NAK <%s> to <%s> packet.", rp, sp));

                return null;
            }

            /* Valid packet received. Escape from the loop. */
            if (rp.startsWith(rph)) {
                break;
            }

            /* Invalid packet received. Log this and wait for another one. */
            mLog.w(String.format(
                    "Invalid ECU response, awaiting <%s>, got <%s>.", rph, rp));
        }

        int offset = isFreezeFrame(freezeFrameNum) ? 8 : 6;
        int[] pidData = new int[(rp.length() - offset) / 2];

        for (int i = 0; i < pidData.length; i++) {
            pidData[i] = Integer.parseInt(rp.substring(
                    offset + (i * 2), offset + ((i + 1) * 2)), 16);
        }

        return pidData;
    }

    /**
     * Asks ECU for list of PIDs supported in 'live data' mode
     * or list of PIDs captured for particular freeze frame.
     *
     * @param   freezeFrameNum  The freeze frame number. -1 indicates that
     *                          list of PIDs supported in 'live data' mode
     *                          should be obtained instead.
     *
     * @return   list of PIDs; <code>null</code>in case of failure.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    private ArrayList<String> getListOfAvailablePids(int freezeFrameNum)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        /* PIDs containing bitmap of supported PIDs */
        int[] supPidIds = {0x00, 0x20, 0x40, 0x60, 0x80};

        ArrayList<String> pidList = new ArrayList<String>();

        for (int i = 0; i < supPidIds.length; i++) {
            int[] pidBitmap;

            if (isFreezeFrame(freezeFrameNum)) {
                pidBitmap = getFreezeFramePidValue(
                        String.format("%02X", supPidIds[i]), freezeFrameNum);
            } else {
                pidBitmap = getPidValue(String.format("%02X", supPidIds[i]));
            }

            /* Returned array has to have exactly 4 entries. */
            if (pidBitmap.length != 4) {
                mLog.e(String.format("getSupportedPids: <%d> is invalid " +
                        "length of bitmap, should be <4>", pidBitmap.length));

                return null;
            }

            for (int j = 0; j < 4; j++) {
                for (int k = 0; k < 8; k++) {
                    /* If PID is supported, add it to the list. */
                    if ((pidBitmap[j] & (0x80 >> k)) != 0) {
                        pidList.add(String.format("%02X",
                                supPidIds[i] + j * 8 + k + 1));
                    }
                }
            }

            /* If next PID bitmap is not available, we are done. */
            if ((pidBitmap[3] & 0x01) == 0) {
                break;
            }
        }

        return pidList;
    }
}
