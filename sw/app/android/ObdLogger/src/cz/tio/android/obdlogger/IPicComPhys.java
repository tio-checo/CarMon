package cz.tio.android.obdlogger;

/**
 * Defines interface which low level PIC communication modules
 * (Bluetooth, USB, ...) must implement.
 */
public interface IPicComPhys {
    /**
     * Opens communication channel to PIC module. Two failure modes are
     * recognized - recoverable and non-recoverable. It makes sense to re-try
     * open() call in case of recoverable error, as the error may have
     * been intermittent and subsequent call may succeed. PIC module coming
     * up (after power on) is an example of recoverable error, since it is
     * likely the open succeeds once PIC module is up and starts accepting
     * connections.
     * On the other hand, non-recoverable failure indicates, that the error
     * is permanent and must be addressed first before open() is called again.
     * Disabled Bluetooth is an example of non-recoverable error.
     *
     * @return  0 if communication established;
     *          -1 in case of recoverable error;
     *          -2 in case of non-recoverable error.
     */
    public int open();

    /**
     * Closes communication channel.
     *
     * @return  0 if communication channel successfully closed;
     *          -1 otherwise.
     */
    public int close();

    /**
     * Returns one character received from PIC module.
     * Blocks until character is available or timeout expires.
     *
     * @return  received character;
     *          -1 if timeout expired;
     *          -2 if connection with PIC module was lost.
     */
    public int read();

    /**
     * Sends one byte to PIC module.
     *
     * @param   data    the byte to be sent
     * @return  0 if operation succeeded;
     *          -1 otherwise.
     */
    public int write(byte data);

    /**
     * Sends array of bytes to PIC module.
     *
     * @param   data    the array of bytes to be sent
     * @return  0 if operation succeeded;
     *          -1 otherwise.
     */
    public int write(byte[] data);

    /**
     * Sends String to PIC module.
     *
     * @param   data    the string to be sent
     * @return  0 if operation succeeded;
     *          -1 otherwise.
     */
    public int write(String data);
}

