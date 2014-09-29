package cz.tio.android.obdlogger;

import android.app.Activity;
import android.os.Handler;

import cz.tio.android.obdlogger.ComSvcMsg;
import cz.tio.android.obdlogger.Obd;

import java.lang.Boolean;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * Implements generic state machine - real state machines are derived
 * from this class.
 */
public abstract class ComSvcStateMachine {
    /*
     * State diagram. It has form of list of ComSvcState objects.
     * Each ComSvcState object represents one state.
     * Order of objects in the list defines transitions between the states.
     * Initial state is the first one in the list, next state is obtained
     * by grabbing state with following index.
     */
    private ArrayList<ComSvcState> mStateDiagram;

    /* Encapsulates data which needs to be carried across states. */
    private HashMap<String, Object> mStateData;

    /* Current state. */
    private int mCurrState;

    /**
     * Gets state data object associated with the given key.
     *
     * @param  key the key used to search the data object
     * @return the state data object
     */
    public Object getStateData(String key) {
        return mStateData.get(key);
    }

    /**
     * Sets state data object which is associated with the given key.
     *
     * @param  key the key used to search the data object
     * @param  o   the data object to be stored
     */
    public void setStateData(String key, Object o) {
        mStateData.put(key, o);
    }

    /**
     * Gets current state.
     *
     * @return  the current state
     */
    public int getCurrState() {
        return mCurrState;
    }

    /**
     * Adds new state at the end of state diagram.
     *
     * @param state  the new state
     */
    public void addState(ComSvcState state) {
        mStateDiagram.add(state);
    }

    /**
     * Moves to the given state.
     *
     * @param state  the new state
     */
    public void goToState(int state) {
        /*
         * Leave current state, if there is any. -1 indicates that
         * the state machine has not been running yet, thus there is
         * no state to be left.
         */
        if (mCurrState != -1) {
            getCurrStateHandler().leave();
        }

        /* Enter new state. */
        mCurrState = state;
        getCurrStateHandler().enter();
    }

    /**
     * Moves to the first state.
     */
    public void goToFirstState() {
        goToState(0);
    }

    /**
     * Moves to the next state.
     */
    public void goToNextState() {
        /* Calculate next state. */
        int nextState = mCurrState + 1;

        /* If this is the last state, stay there - do not re-enter. */
        if (nextState >= mStateDiagram.size()) {

            return;
        }

        /* Transition to the next state. */
        goToState(nextState);
    }

    /**
     * Gets current state handler.
     *
     * @return  the current state handler
     */
    public ComSvcState getCurrStateHandler() {
        return mStateDiagram.get(mCurrState);
    }

    /**
     * Runs state machine.
     *
     * @return  <code>true</code>  state machine is running
     *          <code>false</code> state machine reached its final state
     */
    public boolean runStateMachine() {
        /* Stay in current state by default. */
        ComSvcStateAction nextAction = ComSvcStateAction.STAY_IN_CURRENT;

        /*
         * Handle current state and calculate what to do next.
         * Possibilities:
         *   - stay in current state
         *   - move to the next state
         *   - quit the state machine
         *
         * Raised exceptions have following impact on state transition:
         *   - PicTimeoutExpiredException - stay in current state (retry)
         *   - PicConnectionLostException - lost connection with PIC,
         *                                  try to re-connect
         */
        try {
            nextAction = getCurrStateHandler().next();
        } catch (PicTimeoutExpiredException e) {
            /* Timeout expired, re-try. */

            return true;
        } catch (PicConnectionLostException e) {
            /* Lost connection with PIC, re-connect. */
            goToFirstState();

            return true;
        }

        /* Quitting. */
        if (nextAction == ComSvcStateAction.QUIT) {
            /* Leave the current state. */
            getCurrStateHandler().leave();

            return false;
        }

        /* Moving to the next state. */
        if (nextAction == ComSvcStateAction.GOTO_NEXT) {
            goToNextState();
        }

        return true;
    }

    /**
     * Constructor
     *
     * @param    activity          the reference to our activity
     * @param    obd               the reference to Obd communication object
     * @param    comToUiMsgHandler the reference to message handler used
     *                             to deliver messages from communication thread
     *                             to UI thread
     * @param    uiToComMsgHandler the reference to message handler used
     *                             to deliver messages from UI thread
     *                             to communication thread
     */
    public ComSvcStateMachine(Activity activity, Obd obd,
            Handler comToUiMsgHandler, ComSvcMsg uiToComMsgHandler) {

        /* Create container carrying data passed between states. */
        mStateData = new HashMap<String, Object>();

        /* Create state diagram. */
        mStateDiagram = new ArrayList<ComSvcState>();

        /* Save reference to current activity. */
        setStateData("activity", activity);

        /* Save reference to Obd communication object. */
        setStateData("obd", obd);

        /*
         * If provided, save reference to the transport mechanism delivering
         * messages from communication service thread to UI thread.
         */
        setStateData("comMsgHandler", comToUiMsgHandler);

        /*
         * If provided, save reference to the transport mechanism delivering
         * messages from UI thread to communication service thread.
         */
        setStateData("uiMsgHandler", uiToComMsgHandler);

        /* Initialize state machine. */
        mCurrState = -1;
    }

    /**
     * Constructor
     *
     * @param    activity          the reference to our activity
     * @param    obd               the reference to Obd communication object
     */
    public ComSvcStateMachine(Activity activity, Obd obd) {
        this(activity, obd, null, null);
    }
}
