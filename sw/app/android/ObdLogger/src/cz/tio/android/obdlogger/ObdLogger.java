package cz.tio.android.obdlogger;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import cz.tio.android.obdlogger.CommunicationService;
import cz.tio.android.obdlogger.LogMaster;

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

