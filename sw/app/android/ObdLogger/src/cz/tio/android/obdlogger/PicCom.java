package cz.tio.android.obdlogger;

import android.util.Log;
import java.util.ArrayList;
import java.util.HashMap;

import cz.tio.android.obdlogger.IPicComPhys;

/**
 * Defines interface for callback routines.
 */
interface IPicComCallback {
    /**
     * Called when connection with PIC module is established.
     */
    public void onConnectionEstablished();

    /**
     * Called when connection with PIC module is lost.
     */
    public void onConnectionLost();
}

/**
 * Thrown when the device looses connection with PIC module.
 */
@SuppressWarnings("serial")
class PicConnectionLostException extends Exception {
    /**
     * Constructor
     */
    public PicConnectionLostException() {
        super("Connection with PIC module lost.");
    }
}

/**
 * Thrown when timeout expires while waiting for data from PIC module.
 */
@SuppressWarnings("serial")
class PicTimeoutExpiredException extends Exception {
    /**
     * Constructor
     */
    public PicTimeoutExpiredException() {
        super("Timeout expired when waiting for data from PIC module.");
    }
}

/**
 * Null callback object used as a default if one is not provided
 * when PicCom object is instantiated.
 */
class NullPicComCallback implements IPicComCallback {
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
    public NullPicComCallback() {
        super();
    }
}

/**
 * Provides methods for communicating with PIC module.
 */
public class PicCom {
    static final String TAG = "PicCom";

    /* ECU communication modes */
    static final String ECU_COMM_MODE_KWP2K_BASIC = "01";
    static final String ECU_COMM_MODE_KWP2K_ENHANCED = "02";
    static final String ECU_COMM_MODE_KWP2K_DEBUG = "03";

    /* states of receive state machine */
    private static final int ST_WAIT4SOF = 1;
    private static final int ST_RECV_DATA = 2;
    /* receive timeout in milliseconds */
    private static final long RECV_TIMEOUT = 1000;

    /* Reference to underlying communication module */
    private IPicComPhys mPicComPhys;

    /* Reference to callback routines */
    private IPicComCallback mPicComCb;

    /* Packet buffer implemented as list of String objects */
    private ArrayList<String> mPacketBuffer;

    /* enables debug mode */
    private boolean mDebug = false;
    /* timestamp of the last communication operation */
    private double mComTime;

    /* indicates if communication with PIC module is established */
    private boolean mConnectionEstablished = false;

    /**
     * Establishes communication with PIC module.
     *
     * @return  <code>true</code> if communication has been established;
     *          <code>false</code> otherwise.
     */
    public boolean open() {
        /* If communication channel is already established, return. */
        if (mConnectionEstablished) {
            return true;
        }

        /*
         * First make sure underlying communication module is connected
         * to the PIC.
         */
        if (!mPicComPhys.open())
            return false;

        /* Initiate communication. Re-try several times in case of timeout. */
        int retry = 3;
        do {
            if (retry < 3) {
                Log.d(TAG, "Timeout expired when connecting to PIC, " +
                        "retrying...");
            }

            try {
                /** Send packet to PIC and wait until response arrives. */
                setEcuCommMode(ECU_COMM_MODE_KWP2K_ENHANCED);

                /*
                 * Response to our query arrived, consider communication
                 * with PIC operating.
                 */
                mConnectionEstablished = true;

                /* Call appropriate callback method. */
                mPicComCb.onConnectionEstablished();

                return true;
            } catch (PicTimeoutExpiredException e) {
                /* Timeout expired. Re-try. */
            } catch (PicConnectionLostException e) {
                /* Connection with PIC module lost. */
                return false;
            }
        } while (--retry > 0);

        return false;
    }

    /**
     * Finishes communication.
     */
    public boolean close() {
        mConnectionEstablished = false;

        /* Call appropriate callback method. */
        mPicComCb.onConnectionLost();

        /* Ask underlying communication module to terminate connection. */
        return mPicComPhys.close();
    }

    /**
     * Wipes out packet buffer.
     */
    public void cleanBuffer() {
        mPacketBuffer.clear();
    }

    /**
     * Assembles and sends packet to PIC module.
     *
     * @return   <code>true</code> if packet successfully sent;
     *           <code>false</code> otherwise.
     * @throws   PicConnectionLostException when lost connection with PIC
     */
    public void sendPacket(String data) throws PicConnectionLostException {
        /* Encapsulate provided data payload with SOF and EOF characters. */
        String picPacket = "<" + data + ">";

        /* Send out resulting packet to PIC module. */
        if (mPicComPhys.write(picPacket.getBytes()) == -1) {
            /* Connection to PIC module lost. */
            mConnectionEstablished = false;

            /* Call appropriate callback method. */
            mPicComCb.onConnectionLost();

            throw new PicConnectionLostException();
        }

        /* If desired, print the packet for debugging purposes. */
        if (mDebug) {
            Log.d(TAG, String.format("[%.3f] <- %s",
                    (ObdLogger.getTime() - mComTime), picPacket));

            mComTime = ObdLogger.getTime();
        }
    }

    /**
     * Returns packet received from any PIC module.
     * If packet buffer contains packets, returns the first packet
     * int the buffer, otherwise wait for a packet from PIC either until packet
     * is received or error occures.
     *
     * @return   the packet
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public String getPacket()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        /* Buffer is empty, wait for packet from PIC module. */
        if (mPacketBuffer.isEmpty()) {
            return recv();
        }

        /* Return the first packet from buffer. */
        return mPacketBuffer.remove(0);
    }

    /**
     * Returns packet starting with specified string.
     * If packet buffer contains matching packet, returns the first such packet
     * from the buffer, otherwise wait for such packet from PIC.
     *
     * @return  the matching packet 
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public String getPacket(String startString)
            throws PicConnectionLostException, PicTimeoutExpiredException {
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
     *
     * @return   the value acquired from ADC.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int getAdValue(String channel)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        sendPacket("A" + channel);

        /*
         * Wait for response from PIC module until valid packet is received
         * or timeout expires.
         */
        String p = getPacket("A00");

        int[] picData = new int[2];

        for (int i = 0; i < picData.length; i++) {
            picData[i] = Integer.parseInt(p.substring(
                    3 + (i * 2), 5 + (i * 2)), 16);
        }

        return (picData[0] * 256 + picData[1]);
    }

    /**
     * Configures PIC acquisition engine.
     *
     * @param
     * @return
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int configAcqModule(String config)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        sendPacket("QC" + config);

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Configures PIC post-processing engine.
     *
     * @return
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int configPpModule(String config)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        sendPacket("QP" + config);

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Starts data acquisition.
     *
     * @return
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int runAcq()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        sendPacket("QR");

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Stops data acquisition.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int stopAcq()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        sendPacket("QS");

        /* Wait for ACK */
        return waitForAckNak("Q");
    }

    /**
     * Configures OBD auto-scan mode.
     *
     * @param
     * @return
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int configObdAutoScan(String pidList)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        sendPacket("EOR" + pidList);

        /* Wait just for ACK */
        return waitForAckNak("E", true, false);
    }

    /**
     * Changes ECU communication mode.
     *
     * @param
     * @return
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public int setEcuCommMode(String mode)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        sendPacket("M" + mode);

        /* Wait for ACK */
        return waitForAckNak("M");
    }

    /**
     * Constructor
     *
     * @param picComPhys    the low level communication service.
     */
    public PicCom(IPicComPhys picComPhys) {
        if (mDebug) {
            mComTime = ObdLogger.getTime();
        }

        /* Save reference to underlying communication object. */
        mPicComPhys = picComPhys;

        /* Callback routines not provided, use null object. */
        mPicComCb = new NullPicComCallback();

        /* Instantiate packet buffer. */
        mPacketBuffer = new ArrayList<String>();
    }

    /**
     * Constructor
     *
     * @param picComPhys    the low level communication service.
     * @param picComCb      the callback routines
     */
    public PicCom(IPicComPhys picComPhys, IPicComCallback picComCb) {
        if (mDebug) {
            mComTime = ObdLogger.getTime();
        }

        /* Save reference to underlying communication object. */
        mPicComPhys = picComPhys;

        /* Save reference to callback routines. */
        mPicComCb = picComCb;

        /* Instantiate packet buffer. */
        mPacketBuffer = new ArrayList<String>();
    }

    /**
     * Searches in packet buffer for packet starting with given string.
     * If packet matching given criteria is found, it's removed from buffer
     * and returned to caller, otherwise <code>null</code> is returned.
     *
     * @return  the matching packet; <code>null</code> if matching packet
     *          is not found.
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
            Log.w(TAG, String.format("[%.3f] Buffering packet <%s>.",
                    ObdLogger.getRunTime(), p));

            return mPacketBuffer.add(p);
    }

    /**
     * Waits either for ACK, NAK or both from specified module.
     *
     * @return   0   - ACK has been received.
     *           err - error code received in NAK response
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    private int waitForAckNak(String modId, boolean ack, boolean nak)
            throws PicTimeoutExpiredException, PicConnectionLostException {
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
     *
     * @return   0  - ACK has been received.
     *           err - error code received in NAK response
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    private int waitForAckNak(String modId)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        return waitForAckNak(modId, true, true);
    }

    /**
     * Receives packet from PIC module.
     * Returns null if timeout expired when waiting for packet, otherwise
     * returns received packet.
     */
    private String recv() throws PicConnectionLostException,
            PicTimeoutExpiredException {
        String p = new String("");

        /* State machine will wait for beginning of the packet. */
        int recvState = ST_WAIT4SOF;

        /*
         * Receive characters until whole packet is received or until
         * timeout expires.
         */
        while (true) {
            /* Wait for character from PIC module. */
            int b = mPicComPhys.read();

            if (b == -1) {
                /* Timeout expired. */
                throw new PicTimeoutExpiredException();
            } else if (b == -2) {
                /* Connection with PIC module lost. */
                mConnectionEstablished = false;

                /* Call appropriate callback method. */
                mPicComCb.onConnectionLost();

                throw new PicConnectionLostException();
            }

            char ch = (char) b;

            /* Wait for beginning of packet. */
            if (recvState == ST_WAIT4SOF && ch == '<') {
                p = new String("");

                recvState = ST_RECV_DATA;

                if (mDebug) {
                    Log.d(TAG, (String.format("[%.3f] -> '<'",
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
                        Log.d(TAG, (String.format("[%.3f] -> <",
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
                        Log.d(TAG, (String.format("[%.3f] -> %s",
                                ObdLogger.getTime() - mComTime, p)));

                        mComTime = ObdLogger.getTime();
                    }

                    return p;
                } else {
                    /* Otherwise store received character in buffer. */
                    p += ch;

                    if (mDebug) {
                        Log.d(TAG, (String.format("[%.3f] -> %s",
                                ObdLogger.getTime() - mComTime, p)));

                        mComTime = ObdLogger.getTime();
                    }
                }
            }
        }
    }
}

