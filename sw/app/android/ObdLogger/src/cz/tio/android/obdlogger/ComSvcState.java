package cz.tio.android.obdlogger;

import android.app.Activity;
import android.os.Handler;

import cz.tio.android.obdlogger.ComSvcMsg;
import cz.tio.android.obdlogger.Obd;

/**
 * Interface utilized by state machine to manipulate states
 * and transition between them.
 */
interface IComSvcStateHandler {
    /**
     * Called when state is entered.
     */
    public void enter();

    /**
     * Called when state is left.
     */
    public void leave();

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException;
}

/**
 * Represents generic state. Classes representing real states
 * extend this abstract class.
 */
public abstract class ComSvcState implements IComSvcStateHandler {
    /*
     * Reference to common application logger. Defined as 'protected',
     * so that subclasses can access logger directly.
     */
    protected LogMaster mLog = LogMaster.getInstance();

    /* Reference to state machine. */
    private ComSvcStateMachine mStateMachine;

    /**
     * Gets reference to the state machine.
     *
     * @return the reference to the state machine.
     */
    public ComSvcStateMachine getStateMachine() {
        return mStateMachine;
    }

    /**
     * Gets boolean state data object associated with the given key.
     *
     * @param  key the key used to search the data object
     * @return the state data object
     */
    public boolean getStateDataBoolean(String key) {
        return ((Boolean)mStateMachine.getStateData(key)).booleanValue();
    }

    /**
     * Sets boolean state data object which is associated with the given key.
     *
     * @param  key the key used to search the data object
     * @param  b   the boolean value to be stored
     */
    public void setStateDataBoolean(String key, boolean b) {
        mStateMachine.setStateData(key, new Boolean(b));
    }


    /**
     * Gets state data object associated with the given key.
     *
     * @param  key the key used to search the data object
     * @return the state data object
     */
    public Object getStateData(String key) {
        return mStateMachine.getStateData(key);
    }

    /**
     * Sets state data object which is associated with the given key.
     *
     * @param  key the key used to search the data object
     * @param  o   the data object to be stored
     */
    public void setStateData(String key, Object o) {
        mStateMachine.setStateData(key, o);
    }

    /**
     * Gets reference to the current activity.
     */
    public Activity getActivity() {
        return (Activity)mStateMachine.getStateData("activity");
    }

    /**
     * Gets reference to the message handler delivering messages
     * from communication to UI thread.
     */
    public Handler getComMsgHandler() {
        return (Handler)mStateMachine.getStateData("comMsgHandler");
    }

    /**
     * Gets reference to the message handler delivering UI messages
     * to communication service.
     */
    public ComSvcMsg getUiMsgHandler() {
        return (ComSvcMsg)mStateMachine.getStateData("uiMsgHandler");
    }

    /**
     * Gets reference to the Obd communication object.
     */
    public Obd getObd() {
        return (Obd)mStateMachine.getStateData("obd");
    }

    /**
     * Called when state is entered.
     */
    abstract public void enter();

    /**
     * Called when state is left.
     */
    abstract public void leave();

    /**
     * Called to let state do its job and determine what action
     * is to be taken in the next step.
     *
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    abstract public ComSvcStateAction next()
            throws PicTimeoutExpiredException, PicConnectionLostException;

    /**
     * Constructor.
     */
    public ComSvcState(ComSvcStateMachine stateMachine) {
        mStateMachine = stateMachine;
    }
}
