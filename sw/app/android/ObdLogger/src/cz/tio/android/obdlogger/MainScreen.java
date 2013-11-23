package cz.tio.android.obdlogger;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.util.Log;

public class MainScreen extends Activity {
    static final String TAG = "MainScreen";

    /* IDs identifying launched activities */
    static final int OBD_LOGGER_ACTIVITY_ID = 1;

    /* IDs of Preferences */
    static final int SET_DEBUG_MODE_ID = 1;

    /* Reference to common application logger */
//    private LogMaster mLog = LogMaster.getInstance();

    /** Called when Logger Start Button is clicked. */
    public void launchObdLogger(View view) {
        Intent i = new Intent(this, ObdLogger.class);

        startActivityForResult(i, OBD_LOGGER_ACTIVITY_ID);
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        Log.d(TAG, "-> M.onCreate()");

        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

        Log.d(TAG, "<- M.onCreate()");
    }

    /** Called when the activity is started. */
    @Override
    public void onStart() {
        super.onStart();

        Log.d(TAG, "-> M.onStart()");
        Log.d(TAG, "<- M.onStart()");
    }

    /** Called when the activity is paused. */
    @Override
    public void onPause() {
        super.onPause();

        Log.d(TAG, "-> M.onPause()");
        Log.d(TAG, "<- M.onPause()");
    }

    /** Called when the activity is resumed. */
    @Override
    public void onResume() {
        super.onResume();

        Log.d(TAG, "-> M.onResume()");
        Log.d(TAG, "<- M.onResume()");
    }

    /** Called when the activity is restarted. */
    @Override
    public void onRestart() {
        super.onRestart();

        Log.d(TAG, "-> M.onRestart()");
        Log.d(TAG, "<- M.onRestart()");
    }

    /** Called when the activity is stopped. */
    @Override
    public void onStop() {
        super.onStop();

        Log.d(TAG, "-> M.onStop()");
        Log.d(TAG, "<- M.onStop()");
    }

    /** Called when the activity is destroyed. */
    @Override
    public void onDestroy() {
        super.onDestroy();

        Log.d(TAG, "-> M.onDestroy()");
        Log.d(TAG, "<- M.onDestroy()");
    }

    /** Called when Menu button is pressed. */
    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        menu.add(Menu.NONE, SET_DEBUG_MODE_ID, Menu.NONE, "Settings");

        return(super.onCreateOptionsMenu(menu));
    }

    /** Called when item from Menu is selected. */
    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case SET_DEBUG_MODE_ID:
                startActivity(new Intent(this, EditPreferences.class));
                return(true);
        }

        return(super.onOptionsItemSelected(item));
    }

    /**
     * Called when previously spawned sub-activity finished its job.
     */
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.d(TAG, "-> M.onActivityResult " + resultCode);

        switch (requestCode) {
        case OBD_LOGGER_ACTIVITY_ID:
            if (resultCode == Activity.RESULT_OK) {
                /* Activity succeeded. */
            } else {
                /* Activity failed. */
            }
        }

        Log.d(TAG, "<- M.onActivityResult");
    }
}

