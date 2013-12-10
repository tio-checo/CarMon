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
    static final int OBD_PIDS_ACTIVITY_ID = 1;

    /* Reference to application context */
//    private Context mContext;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /** Called when Live data button is clicked. */
    public void launchObdPids(View view) {
        Intent i = new Intent(this, ObdPids.class);

        startActivityForResult(i, OBD_PIDS_ACTIVITY_ID);
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        /* Bluetooth adapter */
        BluetoothAdapter mBluetoothAdapter = null;

        Log.d(TAG, "-> Od.onCreate()");

        super.onCreate(savedInstanceState);
        setContentView(R.layout.obd_diag);

        Log.d(TAG, "<- Od.onCreate()");
    }

    /** Called when the activity is started. */
    @Override
    public void onStart() {
        super.onStart();

        Log.d(TAG, "-> Od.onStart()");
        Log.d(TAG, "<- Od.onStart()");
    }

    /** Called when the activity is paused. */
    @Override
    public void onPause() {
        super.onPause();

        Log.d(TAG, "-> Od.onPause()");
        Log.d(TAG, "<- Od.onPause()");
    }

    /** Called when the activity is resumed. */
    @Override
    public void onResume() {
        super.onResume();

        Log.d(TAG, "-> Od.onResume()");
        Log.d(TAG, "<- Od.onResume()");
    }

    /** Called when the activity is restarted. */
    @Override
    public void onRestart() {
        super.onRestart();

        Log.d(TAG, "-> Od.onRestart()");
        Log.d(TAG, "<- Od.onRestart()");
    }

    /** Called when the activity is stopped. */
    @Override
    public void onStop() {
        super.onStop();

        Log.d(TAG, "-> Od.onStop()");
        Log.d(TAG, "<- Od.onStop()");
    }

    /** Called when the activity is destroyed. */
    @Override
    public void onDestroy() {
        super.onDestroy();

        Log.d(TAG, "-> Od.onDestroy()");

        /* Close log files */
        mLog.finish();

        Log.d(TAG, "<- Od.onDestroy()");
    }

    /**
     * Called when the concurrently running activity
     * spawned by startActivityForResult() is done.
     */
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.d(TAG, "-> Od.onActivityResult " + resultCode);

        switch (requestCode) {
        /* OBD PIDs */
        case OBD_PIDS_ACTIVITY_ID:
            if (resultCode == Activity.RESULT_OK) {
                /* Activity succeeded. */
            } else {
                /* Activity failed. */
            }
            break;
        }

        Log.d(TAG, "<- Od.onActivityResult");
    }
}

