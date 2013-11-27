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
import java.util.HashMap;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

/**
 * Implements OBD communication protocol.
 */
public class Obd {
    static final String PKT_ACK = "EA";
    static final String PKT_NAK = "EN";
    static final String PKT_GET_PID = "EOP";
    static final String PKT_GET_PID_RESP = "E00P";

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
     *
     * @param picCom    reference to underlying communication object
     * @param activity  reference to parent activity
     */
    public Obd(PicCom picCom, Activity activity) {
        /* Save reference to lower layer communication object. */
        mPicCom = picCom;

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

