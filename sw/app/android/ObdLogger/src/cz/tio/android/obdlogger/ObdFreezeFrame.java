package cz.tio.android.obdlogger;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ListActivity;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.ListView;

import cz.tio.android.obdlogger.ComSvcMsg;
import cz.tio.android.obdlogger.CommunicationService;
import cz.tio.android.obdlogger.LogMaster;
import cz.tio.android.obdlogger.Obd;
import cz.tio.android.obdlogger.PicCom;
import cz.tio.android.obdlogger.PicComBt;

import java.util.ArrayList;

public class ObdFreezeFrame extends ListActivity {
    static final String TAG = "ObdFF";

    /* Reference to application context */
    private Context mContext;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* Container delivering messages to background communication thread. */
    private ComSvcMsg mComSvcMsg;

    /* Communication service handling communication with PIC module and ECU */
    private CommunicationService mComService = null;

    /* Communication thread */
    private Thread commThread;

    /* True if currently displaying freeze frame details. */
    private boolean mInFreezeFrameDetailScreen = false;

    /* List of logged freeze frames. */
    private ArrayList<String> mFreezeFrameList;

    /** Message handler. */
    Handler mHandler = new Handler() {
        @Override
        @SuppressWarnings("unchecked")
        public void handleMessage(Message msg) {
            String[] items;

            mLog.d(TAG, String.format("Message <%d> received.", msg.what));

            switch (msg.what) {
            /*
             * Connection with ECU established.
             * Ask for list of logged freeze frames.
             */
            case ComSvcMsg.RES_ECU_CONNECTED:
                    mComSvcMsg.getFreezeFrameList();
                    break;

            /*
             * Connection with ECU could not be established.
             * Inform user and return.
             */
            case ComSvcMsg.RES_ECU_NOT_AVAILABLE:
                    finish();
                    break;

            /* List of logged freeze frames arrived. */
            case ComSvcMsg.RES_GET_FF_LIST:
                mFreezeFrameList = (ArrayList<String>)msg.obj;
                mLog.d(TAG, "Logged freeze frames: " + mFreezeFrameList);

                /*
                 * If there are no freeze frames logged, just inform the user
                 * and quit the activity.
                 */
                if (mFreezeFrameList.size() == 0) {
                    showDialogNoFreezeFrames();

                    break;
                }

                /* Populate the list. */
                items = new String[mFreezeFrameList.size()];

                for (int i = 0; i < mFreezeFrameList.size(); i++) {
                    items[i] = mFreezeFrameList.get(i);
                }

                setListAdapter(new ArrayAdapter<String>(mContext,
                        android.R.layout.simple_list_item_1, items));

                break;

            /* PIDs for selected freeze frames arrived. */
            case ComSvcMsg.RES_GET_FF_PIDS:
                /*
                 * Populate the list with PID values for selected freeze frame.
                 */
                ArrayList<String> pidList = (ArrayList<String>)msg.obj;
                mLog.d(TAG, "Logged PIDs: " + pidList);

                /* Populate the list. */
                items = new String[pidList.size()];

                for (int i = 0; i < pidList.size(); i++) {
                    items[i] = pidList.get(i);
                }

                setListAdapter(new ArrayAdapter<String>(mContext,
                        android.R.layout.simple_list_item_1, items));

                mInFreezeFrameDetailScreen = true;

                break;

            /* Unknown message. */
            default:
                mLog.w(TAG, "handleMessage: Unknown message received.");
                break;
            }
        }
    };

    /** Called when freeze frame is selected from the list. */
    @Override
    public void onListItemClick(ListView parent, View v, int position,
            long id) {

        /*
         * Used only to select freeze frame from freeze frame list.
         * No-op in 'freeze frame details' screen.
         */
        if (mInFreezeFrameDetailScreen) {
            return;
        }

        Log.d(TAG, String.format("Selected freeze frame <%d>.", position));

        /* Ask for PID values for selected freeze frame. */
        mComSvcMsg.getFreezeFramePids(position);
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.d(TAG, "-> onCreate()");

        /* Save the current context. */
        mContext = getApplicationContext();

        setContentView(R.layout.obd_freeze_frame);

        /* Launch communication service. */
        launchComService();

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

        /*
         * If communication thread was spawned, let it know
         * it should terminate its job.
         */
        if (mComService != null && commThread != null) {
            /* Let communication thread know that we are quitting. */
            mComService.finish();

            /* Send communication thread 'Quit' message. */
            mComSvcMsg.sendQuitMsg();

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
        }

        Log.d(TAG, "<- onActivityResult");
    }

    /**
     * Called when 'Back' button is pressed.
     */
    @Override
    public void onBackPressed() {
        mLog.d(TAG, "-> onBackPressed");

        /*
         * If the activity is currently displaying freeze frame details,
         * then do not leave the activity. Instead, return to the list
         * of available freeze frames.
         */
        if (mInFreezeFrameDetailScreen) {
            mLog.d(TAG, "Re-creating freeze frame screen.");

            mInFreezeFrameDetailScreen = false;

            /* Re-create screen displaying freeze frame list. */
            String[] items = new String[mFreezeFrameList.size()];

            for (int i = 0; i < mFreezeFrameList.size(); i++) {
                items[i] = mFreezeFrameList.get(i);
            }

            setListAdapter(new ArrayAdapter<String>(mContext,
                    android.R.layout.simple_list_item_1, items));

            mLog.d(TAG, "<- onBackPressed");

            return;
        }

        mLog.d(TAG, "<- onBackPressed");

        /* Return to parent activity. */
        super.onBackPressed();
    }

    /**
     * Launches communication service.
     */
    private void launchComService() {
        /* Create container for delivering messages to communication thread. */
        mComSvcMsg = new ComSvcMsg();

        /*
         * Instantiate communication service.
         * Aa part of that, instantiates OBD communication module along
         * with all lower layer communication modules.
         */
        mComService = new CommunicationService(this, mHandler, mComSvcMsg,
                CommunicationService.MODE_FREEZE_FRAME,
                new Obd(new PicCom(new PicComBt("OBD converter")), this));

        /* Create background thread handling communication with PIC module. */
        commThread = new Thread(null, mComService.loop, "ComService");

        /* Launch the service. */
        commThread.start();
    }

    /** Displays dialog informing that there are no freeze frames logged. */
    private void showDialogNoFreezeFrames() {
        new AlertDialog.Builder(this)
                .setTitle("No freeze frame logged.")
                .setMessage("Press Ok to return.")
                .setNeutralButton("Ok", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface na1, int na2) {
                        /* Return back to the parent activity. */
                        finish();
                    }
                })
                .show();
    }
}
