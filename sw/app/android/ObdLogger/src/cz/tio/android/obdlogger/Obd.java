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
    static final String PKT_GET_PID = "EOP";
    static final String PKT_GET_PID_RESP = "EP00";
    static final String PKT_GET_PID_AS_RESP = "ER";
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

    /* debug mode */
    private boolean mDebug = false;

    /* indicates if communication with ECU is established */
    private boolean mConnectionEstablished = false;

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
        if (mConnectionEstablished) {
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
                mPicCom.configObdAutoScan("", "");
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
     * Asks ECU for PID value.
     *
     * @param   pidId   the id of requested PID
     * @return          PID value in form of byte array
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws  PicConnectionLostException when lost connection with PIC
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
     * Asks ECU for supported PIDs.
     *
     * @return   list of supported PIDs; <code>null</code>in case of failure.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    public ArrayList<String> getSupportedPids()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        ArrayList<String> pidList = new ArrayList<String>();

        /* PIDs containing bitmap of supported PIDs */
        int[] supPidIds = {0x00, 0x20, 0x40, 0x60, 0x80};

        for (int i = 0; i < supPidIds.length; i++) {
            int[] pidBitmap = getPidValue(String.format("%02X", supPidIds[i]));

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
         * 'Turbo' indicator. Calculated as Intake pressure/Barometric pressure
         * ratio.
         */
        mProbePool.put("Turbo", new TurboProbe());
    }
}

