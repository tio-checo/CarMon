package cz.tio.android.obdlogger;

import android.os.Environment;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Calendar;

import cz.tio.android.obdlogger.Obd;
import cz.tio.android.obdlogger.Probe;

/**
 * Provides mechanism for
 *  - capturing probe values in file
 *  - capturing state of application in file
 *
 * Designed as singleton.
 */
public class LogMaster {
    static final String TAG = "LogMaster";

    /* Log file capturing values of probes. */
    private static String mFilenameProbe;
    private static File mFileProbe;
    private static FileWriter mFileWriterProbe;

    /* Log file capturing starting voltage. */
    private static String mFilenameSv;
    private static File mFileSv;
    private static FileWriter mFileWriterSv;

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
        String dateTime = String.format("%1$tY%1$tm%1$td-%1$tH%1$tM%1$tS",
                Calendar.getInstance());

        /* Probe filename is in form of 'obd-YYYYMMDD-HHMMSS.txt' */
        mFilenameProbe = String.format("obd-%s.txt", dateTime);

        /* Starting voltage filename is in form of 'sv-YYYYMMDD-HHMMSS.txt' */
        mFilenameSv = String.format("sv-%s.txt", dateTime);

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
                Log.e(TAG, "Failed to create file " + mFilenameProbe, e);

                return -1;
            }
        }

        if (!mFileSts.exists()) {
            try {
                mFileSts.createNewFile();
            } catch (IOException e) {
                Log.e(TAG, "Failed to create file " + mFilenameSts, e);

                return -1;
            }
        }

        /* Open files in append mode. */
        try {
            mFileWriterProbe = new FileWriter(mFileProbe, true);
        } catch (IOException e) {
                Log.e(TAG, "Failed to create FileWriter.", e);

                return -1;
        }

        try {
            mFileWriterSts = new FileWriter(mFileSts, true);
        } catch (IOException e) {
                Log.e(TAG, "Failed to create FileWriter.", e);

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
            Log.d(TAG, "Creating LogMaster singleton.");
            mInstance = new LogMaster();
        }

        /*
         * Init log files.
         */
        if (!mLogFilesInitialized) {
            if (init() == -1) {
                Log.e(TAG, "Failed to initialize log files.");
            } else {
                mLogFilesInitialized = true;
                Log.d(TAG, "Log files initialized.");
            }
        }

        return mInstance;
    }

    /**
     * Logs data captured when measuring starting voltage.
     *
     * @param  p captured data
     * @param  sampleTime sample time in seconds
     * @return True if data was successfully logged, false otherwise.
     */
    public boolean logStartingVoltage(String p, double sampleTime) {
        /* Create and open the log file */
        mFileSv = new File(Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DOWNLOADS), mFilenameSv);

        if (!mFileSv.exists()) {
            try {
                mFileSv.createNewFile();
            } catch (IOException e) {
                Log.e(TAG, "Failed to create file " + mFilenameSv, e);

                return false;
            }
        }

        try {
            mFileWriterSv = new FileWriter(mFileSv, true);
        } catch (IOException e) {
                Log.e(TAG, "Failed to create FileWriter.", e);

                return false;
        }


        /* Capture only if log file was successfully initialized. */
        if (!mLogFilesReady) {
            return false;
        }

        /* Write acquired data to the log file. */
        try {
            /* Header */
            mFileWriterSv.write("# Starting phase battery voltage\n");
            mFileWriterSv.write("Time[s]\tVbatt[V]\n");

            /*
             * Data - packet is in form of
             *  aaaabbbbccccdddd...zzzz
             *
             * Replace decimal comma with decimal point.
             */
            for (int i = 0; i < (p.length() - 3) / 4; i++) {
                mFileWriterSv.write(String.format("%.3f\t%.1f\n",
                        sampleTime * i,
                        0.01837 * Integer.parseInt(p.substring(4 * i,
                        4 * (i + 1)), 16)).replace(',', '.'));
            }

            /* Footer */
            mFileWriterSv.write(
                    "==============================================\n");
        } catch (IOException e) {
            Log.e(TAG, "Failed to write into log file.", e);

            return false;
        }

        /* Close the file. */
        try {
            mFileWriterSv.flush();
            mFileWriterSv.close();
        } catch (IOException e) {
            Log.e(TAG, "Failed to finalize Starting voltage log file.", e);
        }

        return true;
    }

    /**
     * Captures values of given list of probes in file.
     *
     * @param probeList     the list of probes to be logged
     * @param obd           reference to Obd object containing pool of probes.
     */
    public void captureProbes(ArrayList<String> probeList, Obd obd) {
        /* Capture only if log file was successfully initialized. */
        if (!mLogFilesReady) {
            return;
        }

        /* Create header if entered for the first time */
        if (!mLogHeaderWritten) {
            mLogHeaderWritten = true;

            String logHeader = "Time[s]";
            for (int i = 0; i < probeList.size(); i++) {
                Probe probe = obd.getProbe(probeList.get(i));
                logHeader += String.format("\t%s[%s]",
                        probe.getLabel(), probe.getUnits());
            }

            /* Write header to log file */
            try {
                mFileWriterProbe.write(logHeader + "\n");
            } catch (IOException e) {
                Log.e(TAG, "Failed to writer header into log file.", e);

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
            Probe probe = obd.getProbe(probeList.get(i));

            logStr += String.format("\t%s", probe.physToLog());
        }

        try {
            mFileWriterProbe.write(logStr + "\n");
        } catch (IOException e) {
            Log.e(TAG, "Writing to log file failed.", e);
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
     * Logs error message.
     *
     * @param msg   the message to be logged
     */
    synchronized public void e(String msg) {
        common("ERROR", msg);

        Log.e(ObdLogger.TAG, prependRelTimestamp(msg));
    }

    /**
     * Logs tagged error message.
     *
     * @param tag   the tag identifying the message
     * @param msg   the message to be logged
     */
    synchronized public void e(String tag, String msg) {
        common("ERROR", msg);

        Log.e(tag, prependRelTimestamp(msg));
    }

    /**
     * Logs warning message.
     *
     * @param msg   the message to be logged
     */
    synchronized public void w(String msg) {
        common("WARNING", msg);

        Log.w(ObdLogger.TAG, prependRelTimestamp(msg));
    }

    /**
     * Logs tagged warning message.
     *
     * @param tag   the tag identifying the message
     * @param msg   the message to be logged
     */
    synchronized public void w(String tag, String msg) {
        common("WARNING", msg);

        Log.w(tag, prependRelTimestamp(msg));
    }

    /**
     * Logs info message.
     *
     * @param msg   the message to be logged
     */
    synchronized public void i(String msg) {
        common("INFO", msg);

        Log.i(ObdLogger.TAG, prependRelTimestamp(msg));
    }

    /**
     * Logs tagged info message.
     *
     * @param tag   the tag identifying the message
     * @param msg   the message to be logged
     */
    synchronized public void i(String tag, String msg) {
        common("INFO", msg);

        Log.i(tag, prependRelTimestamp(msg));
    }

    /**
     * Logs debug message.
     *
     * @param msg   the message to be logged
     */
    synchronized public void d(String msg) {
        common("DEBUG", msg);

        Log.d(ObdLogger.TAG, prependRelTimestamp(msg));
    }
    
    /**
     * Logs tagged debug message.
     *
     * @param tag   the tag identifying the message
     * @param msg   the message to be logged
     */
    synchronized public void d(String tag, String msg) {
        common("DEBUG", msg);

        Log.d(tag, prependRelTimestamp(msg));
    }
    
    /**
     * Closes logging files.
     */
    public void finish() {
        try {
            mFileWriterProbe.write("==============================================\n");
            mFileWriterProbe.write(String.format("Final time: %.3f\n",
                    (ObdLogger.getTime() - mLogStartTime)));

            mFileWriterProbe.write("==============================================\n");
            mFileWriterProbe.flush();
            mFileWriterProbe.close();
        } catch (IOException e) {
            Log.e(TAG, "Failed to finalize probe log file.", e);
        }

        try {
            mFileWriterSts.flush();
            mFileWriterSts.close();
        } catch (IOException e) {
            Log.e(TAG, "Failed to finalize status log file.", e);
        }

        Log.d(TAG, "Log files closed.");
        mLogFilesInitialized = false;
        mLogHeaderWritten = false;
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
     *
     * @param level     the logging level
     * @param msg       the message to be logged
     */
    private void common(String level, String msg) {
        try {
            mFileWriterSts.write(String.format("[%s] %s: %s\n",
                     getTimestamp(), level, msg));
        } catch (IOException e) {
            Log.e(TAG, "Writing to status log file failed.", e);
        }
    }
}
