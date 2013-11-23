package cz.tio.android.obdlogger;

/**
 * Defines interface which low level PIC communication modules
 * (Bluetooth, USB, ...) must implement.
 */
public interface IPicComPhys {
    /**
     * Establishes communication channel.
     *
     * @*return     <code>true</code> if communication has been established;
     *              <code>false</code> otherwise.
     */
    public boolean open();

    /**
     * Closes communication channel.
     *
     * @return  <code>true</code> if communication channel successfully closed;
     *          <code>false</code> otherwise.
     */
    public boolean close();

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

