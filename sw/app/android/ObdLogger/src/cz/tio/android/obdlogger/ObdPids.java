package cz.tio.android.obdlogger;

import android.app.Activity;
import android.app.ListActivity;
import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import cz.tio.android.obdlogger.ComSvcMsg;
import cz.tio.android.obdlogger.CommunicationService;
import cz.tio.android.obdlogger.LogMaster;
import cz.tio.android.obdlogger.Obd;
import cz.tio.android.obdlogger.PicCom;
import cz.tio.android.obdlogger.PicComBt;

import java.util.ArrayList;

public class ObdPids extends ListActivity {
    static final String TAG = "ObdPids";
    static final int REQUEST_ENABLE_BT = 1;

    /* Reference to application context */
    private Context mContext;

    /* TextView displaying PID value. */
    private TextView mPidValue;

    /* Reference to common application logger */
    private LogMaster mLog = LogMaster.getInstance();

    /* OBD object. */
    private Obd mObd = null;

    /* Container delivering messages to background communication thread. */
    private ComSvcMsg mComSvcMsg;

    /* Communication service handling communication with PIC module and ECU */
    private CommunicationService mComService = null;

    /* Communication thread */
    private Thread commThread;

    /* List of supported PIDs. */
    private ArrayList<String> mPidList;

    /** Message handler. */
    Handler mHandler = new Handler() {
        @Override
        @SuppressWarnings("unchecked")
        public void handleMessage(Message msg) {
            mLog.d(TAG, String.format("Message <%d> received.", msg.what));

            switch (msg.what) {
            /*
             * Connection with ECU established.
             * Ask for list of supported PIDs.
             */
            case ComSvcMsg.RES_ECU_CONNECTED:
                    getSupportedPids();
                    break;

            /*
             * Connection with ECU could not be established.
             * Inform user and return.
             */
            case ComSvcMsg.RES_ECU_NOT_AVAILABLE:
                    finish();
                    break;

            /* List of supported PIDs arrived. */
            case ComSvcMsg.RES_GET_SUPPORTED_PIDS:
                mPidList = (ArrayList<String>)msg.obj;
                mLog.d(TAG, "Supported PIDs: " + mPidList);

                /* Populate the list. */
                String[] items = new String[mPidList.size()];

                for (int i = 0; i < mPidList.size(); i++) {
                    String pid = mPidList.get(i);

                    items[i] = pid + " " + mObd.getDescription(pid);
                }

                setListAdapter(new ArrayAdapter<String>(mContext,
                        android.R.layout.simple_list_item_1, items));

                break;

            /* PID value arrived. */
            case ComSvcMsg.RES_GET_PID_VALUE:
                String pidValue = (String)msg.obj;
                mLog.d(TAG, "PID value: " + pidValue);

                /* Populate the text view. */
                mPidValue.setText(pidValue);

                break;

            /* Unknown message. */
            default:
                mLog.w(TAG, "handleMessage: Unknown message received.");
                break;
            }
        }
    };

    /** Called when PID is selected from the list. */
    @Override
    public void onListItemClick(ListView parent, View v, int position,
            long id) {
        Log.d(TAG, String.format("Selected item <%d>, PID <%s>.",
                position, mPidList.get(position)));

        /* Ask for value of selected PID. */
        getPidValue(mPidList.get(position));
    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.d(TAG, "-> Op.onCreate()");

        /* Save the current context. */
        mContext = getApplicationContext();

        /* Bluetooth adapter */
        BluetoothAdapter mBluetoothAdapter = null;

        setContentView(R.layout.obd_pids);

        mPidValue = (TextView)findViewById(R.id.pid);

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

        Log.d(TAG, "<- Op.onCreate()");
    }

    /** Called when the activity is started. */
    @Override
    public void onStart() {
        super.onStart();

        Log.d(TAG, "-> Op.onStart()");
        Log.d(TAG, "<- Op.onStart()");
    }

    /** Called when the activity is paused. */
    @Override
    public void onPause() {
        super.onPause();

        Log.d(TAG, "-> Op.onPause()");
        Log.d(TAG, "<- Op.onPause()");
    }

    /** Called when the activity is resumed. */
    @Override
    public void onResume() {
        super.onResume();

        Log.d(TAG, "-> Op.onResume()");
        Log.d(TAG, "<- Op.onResume()");
    }

    /** Called when the activity is restarted. */
    @Override
    public void onRestart() {
        super.onRestart();

        Log.d(TAG, "-> Op.onRestart()");
        Log.d(TAG, "<- Op.onRestart()");
    }

    /** Called when the activity is stopped. */
    @Override
    public void onStop() {
        super.onStop();

        Log.d(TAG, "-> Op.onStop()");
        Log.d(TAG, "<- Op.onStop()");
    }

    /** Called when the activity is destroyed. */
    @Override
    public void onDestroy() {
        super.onDestroy();

        Log.d(TAG, "-> Op.onDestroy()");

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

        /* Close log files */
        mLog.finish();

        Log.d(TAG, "<- Op.onDestroy()");
    }

    /**
     * Called when the concurrently running activity
     * spawned by startActivityForResult() is done.
     */
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.d(TAG, "-> Op.onActivityResult " + resultCode);

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

        Log.d(TAG, "<- Op.onActivityResult");
    }

    /**
     * Launches communication service.
     */
    private void launchComService() {
        /* Create container for delivering messages to communication thread. */
        mComSvcMsg = new ComSvcMsg();

        /*
         * Instantiates OBD communication module along with all lower
         * layer communication modules.
         */
        mObd = new Obd(new PicCom(new PicComBt("OBD converter")), this);

        /* Instantiate communication service. */
        mComService = new CommunicationService(this, mHandler, mComSvcMsg,
                CommunicationService.MODE_DIAG, mObd);

        /* Create background thread handling communication with PIC module. */
        commThread = new Thread(null, mComService.loop, "ComService");

        /* Launch the service. */
        commThread.start();
    }

    /** Asks for list of OBD PIDs supported by the ECU. */
    private void getSupportedPids() {
        Bundle b = new Bundle();

        b.putInt(ComSvcMsg.KEY_CMD, ComSvcMsg.CMD_GET_SUPPORTED_PIDS);

        mComSvcMsg.put(b);
    }

    /**
     * Asks for OBD PID value.
     *
     * @param pid   the PID
     */
    private void getPidValue(String pid) {
        Bundle b = new Bundle();

        b.putInt(ComSvcMsg.KEY_CMD, ComSvcMsg.CMD_GET_PID_VALUE);
        b.putString(ComSvcMsg.KEY_ARG1, pid);

        mComSvcMsg.put(b);
    }
}

