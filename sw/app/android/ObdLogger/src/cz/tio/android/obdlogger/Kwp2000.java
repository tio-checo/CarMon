package cz.tio.android.obdlogger;

import android.util.Log;

import cz.tio.android.obdlogger.PicCom;

/**
 * Implements KWP2000 communication protocol.
 */
class Kwp2000 {
    static final String TAG = "Kwp2000";

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

