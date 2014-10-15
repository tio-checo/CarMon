package cz.tio.android.obdlogger;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.view.View;
import android.widget.Toast;

import cz.tio.android.obdlogger.ComSvcMsg;
import cz.tio.android.obdlogger.CommunicationService;
import cz.tio.android.obdlogger.LogMaster;

import java.util.ArrayList;

public class ObdDiag extends Activity {
    static final String TAG = "ObdDiag";

    /* IDs identifying launched activities */
    static final int REQUEST_ENABLE_BT = 1;
    static final int OBD_PIDS_ACTIVITY_ID = 2;
    static final int OBD_FF_ACTIVITY_ID = 3;

    /* Reference to application context */
//    private Context mContext;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /** Called to read Live data. */
    public void launchObdPids(View view) {
        Intent i = new Intent(this, ObdPids.class);

        startActivityForResult(i, OBD_PIDS_ACTIVITY_ID);
    }

    /** Called to read Freeze frame data. */
    public void launchObdFreezeFrame(View view) {
        Intent i = new Intent(this, ObdFreezeFrame.class);

        startActivityForResult(i, OBD_FF_ACTIVITY_ID);
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        /* Bluetooth adapter */
        BluetoothAdapter mBluetoothAdapter = null;

        Log.d(TAG, "-> onCreate()");

        super.onCreate(savedInstanceState);
        setContentView(R.layout.obd_diag);

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
        }

        Log.d(TAG, "<- onCreate()");
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
            /* Failed to enable Bluetooth, return. */
            if (resultCode != Activity.RESULT_OK) {
                /* User did not enable Bluetooth or an error occurred */
                Log.d(TAG, "BT not enabled");

                Toast.makeText(this, "Bluetooth not enabled",
                        Toast.LENGTH_LONG).show();

                finish();
            }
            break;

        /* Read current PIDs. */
        case OBD_PIDS_ACTIVITY_ID:
            if (resultCode == Activity.RESULT_OK) {
                /* Activity succeeded. */
            } else {
                /* Activity failed. */
            }
            break;

        /* Display list of logged freeze frames. */
        case OBD_FF_ACTIVITY_ID:
            if (resultCode == Activity.RESULT_OK) {
                /* Activity succeeded. */
            } else {
                /* Activity failed. */
            }
            break;
        }

        Log.d(TAG, "<- onActivityResult");
    }
}
