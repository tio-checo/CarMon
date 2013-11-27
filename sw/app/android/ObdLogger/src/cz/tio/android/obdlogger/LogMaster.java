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
            Probe probe = obd.getProbe(probeList.get(i));

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

