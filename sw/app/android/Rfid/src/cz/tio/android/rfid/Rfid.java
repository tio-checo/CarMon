package cz.tio.android.rfid;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
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

                /* Store received character and notify waiting thread */
                mRecvBuf.put(ch);

//                Log.d(Rfid.TAG, String.format("Pic -> %c", ch));
            } catch (IOException e) {
                Log.e(Rfid.TAG, "Error when waiting for " +
                        "character from PIC module.", e);

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
class PicCom {
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
     * Constructor
     */
    public PicCom(InputStream In, OutputStream Out) {
        if (mDebug) {
            mComTime = Rfid.getTime();
        }

        /* Initialize input and output streams */
        mPicIn = In;
        mPicOut = Out;

        /*
         * Crate object which will be used for character received from
         * PIC module between main and "reader" thread.
         */
        mRecvChar = new PicRecvChar();

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
     * Receives one byte from PIC module. Returns -1 if InputStream was closed
     * or timeout expired.
     */
    private int recvChar() {
        return mRecvChar.get();
    }

    /**
     * Establishes communication with PIC module.
     * Returns true if communication has been established, otherwise
     *  returns false.
     */
    public boolean comInit() {
        /* If communication channel is already established, return. */
        if (mComPicEstablished) {
            return true;
        }

        /* Initiate communication with PIC module. */
        /* Set date and time in PIC module */
        int res = setDateTime(String.format("%1$tY%1$tm%1$td%1$tH%1$tM%1$tS",
                Calendar.getInstance()));

        if (res == -1) {
            String logStr = String.format("[%.3f] Timeout when trying " +
                    "to initiate communication with PIC module",
                    Rfid.getRunTime());

            Log.w(Rfid.TAG, logStr);
            mLog.w(logStr);

            return false;
        } else {
            /* Valid response received, communication established. */
            mComPicEstablished = true;

            String logStr = "Communication with PIC module established";
            Log.i(Rfid.TAG, logStr);
            mLog.i(logStr);

            return true;
        }
    }

    /**
     * Finishes communication.
     */
    public void commFinish() {
        /* Try to join reader thread. */
        try {
            mReaderThread.join(5000);
        } catch (InterruptedException e) {
            /* Could not join communication thread */
            Log.e(Rfid.TAG, "Interrupted while sleeping.", e);
        }

        if (mReaderThread.isAlive()) {
            /* Could not join communication thread */
            Log.e(Rfid.TAG, "Failed to join PIC reader thread.");
        } else {
            Log.i(Rfid.TAG, "PIC reader thread joined.");
        }
    }

    /**
     * Assembles and sends packet to PIC module.
     * Return false if attempt to send the packet failed, otherwise return
     * true.
     */
    public boolean sendPacket(String cmd, String data) {
        /* Encapsulate provided data payload with SOF and EOF characters. */
        String picPacket = "<" + cmd + data + ">";

        /* Send out resulting packet to PIC module. */
        try {
            mPicOut.write(picPacket.getBytes());
        } catch (IOException e) {
            Log.e(Rfid.TAG, "sendPacket() - writing to OutputStream " +
                    "failed.", e);

            return false;
        }

        /* If desired, print the packet for debugging purposes. */
        if (mDebug) {
            Log.d(Rfid.TAG, String.format("[%.3f] <- %s",
                    (Rfid.getTime() - mComTime), picPacket));

            mComTime = Rfid.getTime();
        }

        return true;
    }

    /**
     * Receives packet from PIC module.
     * Returns null if timeout expired when waiting for packet, otherwise
     * returns received packet.
     */
    public String recvPacket() {
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
                    Log.d(Rfid.TAG, (String.format("[%.3f] -> '<'",
                            Rfid.getTime() - mComTime)));

                    mComTime = Rfid.getTime();
                }

                continue;
            }

            /* Receive the rest of packet. */
            if (recvState == ST_RECV_DATA) {
                if (ch == '<') {
                    /* SOF character received again, restart receive process. */
                    p = new String("");

                    if (mDebug) {
                        Log.d(Rfid.TAG, (String.format("[%.3f] -> <",
                                Rfid.getTime() - mComTime)));

                        mComTime = Rfid.getTime();
                    }
                } else if (ch == '>') {
                    /*
                     * EOF character, complete packet has been received.
                     * Pass data payload to the caller.
                     * If desired, print received packet for debugging
                     * purposes.
                     */
                    if (mDebug) {
                        Log.d(Rfid.TAG, (String.format("[%.3f] -> %s",
                                Rfid.getTime() - mComTime, p)));

                        mComTime = Rfid.getTime();
                    }

                    return p;
                } else {
                    /* Otherwise store received character in buffer. */
                    p += ch;

                    if (mDebug) {
                        Log.d(Rfid.TAG, (String.format("[%.3f] -> %s",
                                Rfid.getTime() - mComTime, p)));

                        mComTime = Rfid.getTime();
                    }
                }
            }
        }
    }

    /**
     * Waits for ACK response. Returns -1 in case of failure.
     */
    public int waitForAck(String modId) {
        String ack = modId + "A";

        /* Wait for packet from PIC module. */
        String p = recvPacket();

        /* Timeout occurred */
        if (p == null) {
            Log.w(Rfid.TAG, String.format("[%.3f] " +
                    "Timeout when waiting for packet from" +
                    " PIC module", Rfid.getRunTime()));

            return -1;
        }

        /* Valid packet received. */
        if (p.equals(ack)) {
            return 0;
        }

        /*
         * Invalid response from PIC module received. Log this
         * and process next packet.
         */
        Log.w(Rfid.TAG, "Invalid PIC response, awaiting \"" +
                ack + "\", received \"" + p + "\"");

        return -1;
    }

    /**
     * Acquires value measured on given ADC channel. Returns -1
     * in case of failure.
     */
    public int getAdValue(String channel) {
        if (!sendPacket("A", channel)) {
            return -1;
        }

        /*
         * Process response from PIC module.
         * Loop until valid packet is received or timeout
         * occurs (buffer is empty).
         */
        String p;
        while (true) {
            p = recvPacket();

            /* Timeout occurred */
            if (p == null) {
                Log.w(Rfid.TAG, String.format("[%.3f] " +
                        "Timeout when waiting for packet from" +
                        " PIC module", Rfid.getRunTime()));

                return -1;
            }

            /* Valid packet received. Escape from the loop. */
            if (p.startsWith("A00")) {
                break;
            }

            /*
             * Invalid response from PIC module received. Log this
             * and process next packet.
             */
            Log.w(Rfid.TAG, "Invalid PIC response, awaiting " +
                    "\"A00...\", received \"" + p + "\"");
        }

        int[] picData = new int[2];

        for (int i = 0; i < picData.length; i++) {
            picData[i] = Integer.parseInt(p.substring(
                    3 + (i * 2), 3 + (i + 1) * 2), 16);
        }

        return (picData[0] * 256 + picData[1]);
    }

    /**
     * Configures PIC acquisition engine.
     * Returns -1 in case of failure.
     */
    public int configAcqModule(String config) {
        if (!sendPacket("QC", config)) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAck("Q");
    }

    /**
     * Configures PIC post-processing engine.
     * Returns -1 in case of failure.
     */
    public int configPpModule(String config) {
        if (!sendPacket("QP", config)) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAck("Q");
    }

    /**
     * Start data acquisition
     * Returns -1 in case of failure.
     */
    public int runAcq() {
        if (!sendPacket("QR", "")) {
            return -1;
        }

        /* Wait for ACK */
//        return waitForAck("Q");
        return 0;
    }

    /**
     * Stop data acquisition
     * Returns -1 in case of failure.
     */
    public int stopAcq() {
        if (!sendPacket("QS", "")) {
            return -1;
        }

        /* Wait for ACK */
//        return waitForAck("Q");
        return 0;
    }

    /**
     * Configures OBD auto-scan mode.
     * Returns -1 in case of failure.
     */
    public int configObdAutoScan(String pidList) {
        if (!sendPacket("EOR", pidList)) {
            return -1;
        }

        /* Wait for ACK */
//        return waitForAck("E");
        return 0;
    }

    /**
     * Changes ECU communication mode. Returns 0 in case of success,
     * -1 otherwise.
     */
    public int setEcuCommMode(String mode) {
        if (!sendPacket("M", mode)) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAck("M");
    }

    /**
     * Sets date and time in PIC module. Returns 0 in case of success,
     * -1 otherwise.
     */
    public int setDateTime(String dt) {
        if (!sendPacket("PT", dt)) {
            return -1;
        }

        /* Wait for ACK */
        return waitForAck("P");
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
        /* Probe filename is in format of 'rfid-YYYYMMDD-HHMMSS.txt' */
        mFilenameProbe = String.format("rfid-%1$tY%1$tm%1$td-%1$tH%1$tM%1$tS.txt",
                Calendar.getInstance());

        /*
         * There is just one file used for capturing status messages.
         * It operates in append mode.
         */
        mFilenameSts = "rfid-sts.txt";

        /* Initialize log timer. */
        mLogStartTime = Rfid.getTime();

        /* Create log files if they don't exist . */
        mFileProbe = new File(Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DOWNLOADS), mFilenameProbe);

        mFileSts = new File(Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DOWNLOADS), mFilenameSts);

        if (!mFileProbe.exists()) {
            try {
                mFileProbe.createNewFile();
            } catch (IOException e) {
                Log.e(Rfid.TAG, "Failed to create file " + mFilenameProbe, e);

                return -1;
            }
        }

        if (!mFileSts.exists()) {
            try {
                mFileSts.createNewFile();
            } catch (IOException e) {
                Log.e(Rfid.TAG, "Failed to create file " + mFilenameSts, e);

                return -1;
            }
        }

        /* Open files in append mode. */
        try {
            mFileWriterProbe = new FileWriter(mFileProbe, true);
        } catch (IOException e) {
            Log.e(Rfid.TAG, "Failed to create FileWriter.", e);

            return -1;
        }

        try {
            mFileWriterSts = new FileWriter(mFileSts, true);
        } catch (IOException e) {
            Log.e(Rfid.TAG, "Failed to create FileWriter.", e);

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
            Log.d(Rfid.TAG, "Creating LogMaster singleton.");
            mInstance = new LogMaster();
        }

        /*
         * Init log files.
         */
        if (!mLogFilesInitialized) {
            if (init() == -1) {
                Log.e(Rfid.TAG, "Failed to initialize log files.");
            } else {
                mLogFilesInitialized = true;
                Log.d(Rfid.TAG, "Log files initialized.");
            }
        }

        return mInstance;
    }

    /**
     * Posts message into probe log file.
     */
    public void post(String msg) {
        try {
            mFileWriterProbe.write(msg);
        } catch (IOException e) {
            Log.e(Rfid.TAG, "Writing to log file failed.", e);
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
     * Implements common logging code.
     */
    private void common(String level, String msg) {
        try {
            mFileWriterSts.write(String.format("[%s] %s: %s\n",
                     getTimestamp(), level, msg));
        } catch (IOException e) {
            Log.e(Rfid.TAG, "Writing to status log file failed.", e);
        }
    }

    /**
     * Logs error messages.
     */
    synchronized public void e(String msg) {
        common("ERROR", msg);
    }

    /**
     * Logs warning messages.
     */
    synchronized public void w(String msg) {
        common("WARNING", msg);
    }

    /**
     * Logs info messages.
     */
    synchronized public void i(String msg) {
        common("INFO", msg);
    }

    /**
     * Logs debug messages.
     */
    synchronized public void d(String msg) {
        common("DEBUG", msg);
    }
    
    /**
     * Finishes open files.
     */
    public static void finish() {
        try {
            mFileWriterProbe.write("==============================================\n");
            mFileWriterProbe.write(String.format("Final time: %.3f\n",
                    (Rfid.getTime() - mLogStartTime)));

            mFileWriterProbe.write("==============================================\n");
            mFileWriterProbe.flush();
            mFileWriterProbe.close();
        } catch (IOException e) {
            Log.e(Rfid.TAG, "Failed to finalize probe log file.", e);
        }

        try {
            mFileWriterSts.flush();
            mFileWriterSts.close();
        } catch (IOException e) {
            Log.e(Rfid.TAG, "Failed to finalize status log file.", e);
        }

        Log.d(Rfid.TAG, "Log files closed.");
        mInstance = null;
        mLogFilesInitialized = false;
    }
}

/**
 * Represents communication service running as a background thread.
 */
class CommunicationService {
    /* Initiates communication with PIC module. */
    private static final int ST_COMM_INIT_PIC = 0;
    /* Waits for packet from RFID reader. */
    private static final int ST_COMM_WAIT4RFID = 1;

    private boolean mDebug = false;
    private boolean mQuit = false;
    private int mState;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Reference to main Activity */
    private Activity mActivity;

    /* PIC communication module. */
    private PicCom mPicCom;

    /* Status of PIC module communication */
    private Button mStsPic;

    /**
     * Constructor
     */
    public CommunicationService(Activity activity, PicCom pic) {
        super();

        /* Save reference to OBD communication object */
        mPicCom = pic;

        /* Save reference to main Activity */
        mActivity = activity;

        /* Initialize references to status indicators */
        mStsPic = (Button) mActivity.findViewById(R.id.picState);
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
     * Implements communication thread running in background.
     */
    public Runnable loop = new Runnable() {
        public void run() {
            /* Currently processed PID */
            String currPid = "";
            PicCom pc = mPicCom;

            while (!mQuit) {
                /* Establish communication with PIC module */
                if (mState == ST_COMM_INIT_PIC) {
                    /* Wait until communication with PIC module is established. */
                    if (!pc.comInit()) {
                        /* Sleep for a while and retry. */
                        try {
                            Thread.sleep(2000);
                        } catch (InterruptedException e) {
                            Log.w(Rfid.TAG,
                                    "Thread CommunicationService " +
                                    "interrupted.", e);
                        }

                        continue;
                    }

                    /* Update status indicator */
                    updateStatusIndicator(mStsPic, true);

                    mState = ST_COMM_WAIT4RFID;
		        } else if (mState == ST_COMM_WAIT4RFID) {
                    String p;

                    /* Ask PIC module for battery voltage */
                    pc.sendPacket("A", "08");

                    /* Process packets received from PIC module. */
                    while ((p = pc.recvPacket()) != null) {

                        /* Packet from AD module */
                        if (p.startsWith("A00")) {
                            final int adv;

                            /* AD value obtained, refresh indicator */
                            int[] picData = new int[2];

                            for (int i = 0; i < picData.length; i++) {
                                picData[i] = Integer.parseInt(p.substring(
                                        3 + (i * 2), 3 + (i + 1) * 2), 16);
                            }

                            adv = picData[0] * 256 + picData[1];

                            final TextView lbl = (TextView) mActivity.findViewById(
                                R.id.indicator4Value);

                            mActivity.runOnUiThread(new Runnable() {
                                public void run() {
                                    lbl.setText(String.format("%.1f V",
                                            0.01837 * adv));
                                }
                            });
                        } else if (p.startsWith("R00")) {
                            final String pr = p;

                            /* Packet received, process it. */
                            Log.i(Rfid.TAG, pr);

                            /* Log packet to the file. */
                            mLog.post(pr + "\n");

                            /* Update labels with new data. */
                            /* RFID id */
                            final TextView lblId =
                                    (TextView) mActivity.findViewById(
                                    R.id.indicator1Value);

                            mActivity.runOnUiThread(new Runnable() {
                                public void run() {
                                    lblId.setText(pr.substring(17));
                                }
                            });

                            /* Date */
                            final TextView lblD =
                                    (TextView) mActivity.findViewById(
                                    R.id.indicator2Value);

                            mActivity.runOnUiThread(new Runnable() {
                                public void run() {
                                    lblD.setText(pr.substring(3, 7) + "/" +
                                            pr.substring(7, 9) + "/" +
                                            pr.substring(9, 11));
                                }
                            });

                            /* Time */
                            final TextView lblT =
                                    (TextView) mActivity.findViewById(
                                    R.id.indicator3Value);

                            mActivity.runOnUiThread(new Runnable() {
                                public void run() {
                                    lblT.setText(pr.substring(11, 13) + ":" +
                                            pr.substring(13, 15) + ":" +
                                            pr.substring(15, 17));
                                }
                            });
                        } else {
                            /*
                             * Invalid response from PIC module received.
                             */
                            Log.w(Rfid.TAG, "Invalid PIC response, awaiting " +
                                    "\"A00...\" or \"R00...\", received \"" +
                                    p + "\"");
                        }
                    }
                } /* if */
            } /* while */
        }
    };
}

public class Rfid extends Activity {
    static final String TAG = "Rfid";
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

    /* PIC communication module */
    private PicCom mPicCom;

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
            Toast.makeText(this, "No Bluetooth paired devices found",
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
            if (sppDevice.getName().equals("Rfid")) {
                try {
                    mBtSocket = sppDevice.createRfcommSocketToServiceRecord(
                            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB"));
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
            Toast.makeText(this, "Unable to crate communication streams",
                    Toast.LENGTH_LONG).show();

            finish();
            return;
        }

        /*
         * Instantiates OBD communication module along with all lower
         * layer communication modules.
         */
        mPicCom = new PicCom(mBluetoothIn, mBluetoothOut);

        /* Instantiates communication service */
        mComService = new CommunicationService(this, mPicCom);

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
        return Rfid.getTime() - mTimeStart;
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

        mLog.d("Application started.");

        mTimeStart = getTime();

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

    /** Called when the activity is started. */
    @Override
    public void onStart() {
        super.onStart();

        Log.d(TAG, "-> onStart()");
        Log.d(TAG, "<- onStart()");
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
