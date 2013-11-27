package cz.tio.android.obdlogger;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.util.Log;

import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.Exception;
import java.lang.IllegalAccessException;
import java.lang.NoSuchMethodException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

import java.util.ArrayList;
import java.util.Set;
import java.util.UUID;

import cz.tio.android.obdlogger.IPicComPhys;

/**
 * Implements FIFO which transports characters from "reader" thread (producer)
 * to main communication thread (consumer).
 * If FIFO is empty, consumer does not block on waiting for character
 * indefinitely - there is 1 second timeout.
 * Internally it stores characters as integers, since this is what
 * InputStream.read() returns.
 */
class PicRecvFifo {
    /** FIFO receive buffer */
    private ArrayList<Integer> mRecvFifo;

    /**
     * Flag set when consumer is blocked waiting for character. Cleared once
     * consumer is unblocked (either due to character received or timeout).
     */
    private boolean mWaiting;

    /**
     * Returns one character from receive buffer.
     * If no character is currently available, waits 1 second for one
     * to be received.
     *
     * @return  received character; -1 if no character is available.
     */
    synchronized public int get() {
        /* If there is a character in receive buffer, return it */
        if (mRecvFifo.size() > 0) {
            return mRecvFifo.remove(0).intValue();
        }

        /* Character currently not available, indicate we are going to block. */
        mWaiting = true;

        /* Wait 1 second for a character. */
        try {
            wait(1000);
        } catch (InterruptedException e) {
            /* Thread interrupted, return. */
            return -1;
        } finally {
            /* Thread unblocked. */
            mWaiting = false;
        }

        /* Character has been received, return it. */
        if (mRecvFifo.size() > 0) {
            return mRecvFifo.remove(0).intValue();
        } else {
            /* Timeout expired. */
            return -1;
        }
    }

    /**
     * Stores received character to the buffer.
     *
     * @param ch    the character to be stored
     */
    synchronized public void put(int ch) {
        /* Store the character into receive buffer. */
        mRecvFifo.add(ch);

        /* Consumer blocked on waiting for character, wake it up. */
        if (mWaiting) {
            notify();
        }
    }

    /**
     * Constructor
     */
    public PicRecvFifo() {
        mRecvFifo = new ArrayList<Integer>();

        mWaiting = false;
    }
}

/**
 * "reader" thread.
 * This thread serves as a producer of received characters. It obtaines data
 * from input stream via call to InputStream.read(). Received data are then
 * stored into FIFO receive buffer. The thread blocks in InputStream.read()
 * if there are no data available.
 */
class PicReaderThread extends Thread {
    /** FIFO passing received characters between producer and consumer */
    private PicRecvFifo mRecvBuf = null;

    /** Stream providing received characters. */
    private InputStream mInStream = null;

    /**
     * Thread body.
     */
    public void run() {
        int ch = 0;

        /* Loop until we are asked to exit or connection to PIC is lost. */
        while ((ch != -1) && (ch != -2)) {
            try {
                /* Block until character is received. */
                ch = mInStream.read();

                /*
                 * Thread has been interrupted which indicates that upper layer
                 * wants us to gracefully terminate the connection.
                 */
                if (interrupted()) {
                    Log.d(ObdLogger.TAG, "Reader thread interrupted.");

                    ch = -1;
                }
            } catch (IOException e) {
                /* Connection lost, terminate the thread. */
                Log.e(ObdLogger.TAG, "Bluetooth Connection with PIC module " +
                        "lost, destroying reader thread.");
                ch = -2;
            } finally {
                /* Store the character into FIFO. */
                mRecvBuf.put(ch);
            }
        }
    }

    /**
     * Constructor.
     *
     * @param recvChar  the receive FIFO
     * @param inStr     the input stream
     */
    public PicReaderThread(PicRecvFifo recvChar, InputStream inStr) {
        mRecvBuf = recvChar;

        mInStream = inStr;
    }
}

/**
 * Provides low level procedures for communication with PIC module
 * via Bluetooth. Implements IPicComPhys interface.
 */
public class PicComBt implements IPicComPhys {
    static final String TAG = "PicComBt";

    /** Name of Bluetooth module. */
    private String mModName;

    /* Bluetooth communication socket */
    private BluetoothSocket mBtSocket = null;

    /* Input and output streams used for communication with PIC module */
    private InputStream mPicIn = null;
    private OutputStream mPicOut = null;

    /* "reader" thread */
    private PicReaderThread mReaderThread;

    /* Object representing value received from PIC module. */
    private PicRecvFifo mRecvFifo = null;

    /* Connection established. */
    private boolean mConnectionEstablished;

    /**
     * Establishes communication channel with PIC module via Bluetooth.
     *
     * @return  <code>true</code> if communication has been established;
     *          <code>false</code> otherwise.
     */
    public boolean open() {
       	BluetoothAdapter mBluetoothAdapter;

        /* Connection already established, return. */
        if (mConnectionEstablished)
            return true;

        /* We already got a socket, use it when re-connecting. */
        if (mBtSocket == null) {
            mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

            /* Bluetooth not available. */
            if (mBluetoothAdapter == null) {
                return false;
            }

            /* Cancel in-progress discovery */
            if (mBluetoothAdapter.isDiscovering()) {
                Log.d(TAG, "Canceling Bluetooth discovery process...");

                if (mBluetoothAdapter.cancelDiscovery()) {
                    Log.d(TAG, "DONE.");
                } else {
                    Log.d(TAG, "FAILED.");
                }
            }

            /* Search for PIC module connected via Bluetooth */
            Set <BluetoothDevice> pDevs = mBluetoothAdapter.getBondedDevices();

            /* There are no paired devices, exit */
            if (pDevs.size() == 0) {
                Log.e(TAG, "No OBD Bluetooth paired devices found.");

                return false;
            }

            /* Loop through paired devices */
            for (BluetoothDevice sppDevice : pDevs) {
                /* PIC module found, create socket. */
                if (sppDevice.getName().equals(mModName)) {
                    try {
                        Method m = sppDevice.getClass().getMethod(
                                "createRfcommSocket", new Class[] {int.class});
                        mBtSocket = (BluetoothSocket) m.invoke(sppDevice, 1);
                    } catch (NoSuchMethodException e) {
                        Log.e(TAG, "Failed to connect to PIC module.", e);

                        return false;
                    } catch (IllegalAccessException e) {
                        Log.e(TAG, "Failed to connect to PIC module.", e);

                        return false;
                    } catch (InvocationTargetException e){
                        Log.e(TAG, "Failed to connect to PIC module.", e);

                        return false;
                    }
                }
            }
        }

        /* PIC module not found. */
        if (mBtSocket == null) {
            Log.e(TAG, "PIC module not found.");

            return false;
        }

        /*
         * Connect the device through the created socket. This will block
         * until it succeeds or throws an exception.
         */
        try {
            Log.d(TAG, "Connecting to Bluetooth socket...");

            mBtSocket.connect();
        } catch (IOException e) {
            /* Unable to connect. Clean up and exit. */

            Log.d(TAG, "FAILED.");
            Log.e(TAG, "Unable to connect to PIC module.", e);

            try {
                Log.d(TAG, "Closing Bluetooth communication socket...");
                mBtSocket.close();
            } catch (IOException ee) {
                Log.d(TAG, "FAILED.", ee);
                return false;
            } finally {
                mBtSocket = null;
            }

            Log.d(TAG, "CLOSED.");

            return false;
        }

        /* Connection successfully established. */
        Log.d(TAG, "CONNECTED.");

        /* Get the input and output streams */
        try {
            mPicIn = mBtSocket.getInputStream();
            mPicOut = mBtSocket.getOutputStream();
        } catch (IOException e) {
            Log.e(TAG, "Unable to create communication streams, aborting.");

            /* Close the socket. */
            try {
                Log.d(TAG, "Closing Bluetooth communication socket...");
                mBtSocket.close();
            } catch (IOException ee) {
                Log.d(TAG, "FAILED.", ee);
                return false;
            } finally {
                mBtSocket = null;
            }

            Log.d(TAG, "CLOSED.");

            return false;
        }

        /*
         * Create FIFO used for transferring characters between "reader"
         * thread (producer) and the main thread (consumer).
         */
        mRecvFifo = new PicRecvFifo();

        /*
         * Create "reader" thread processing data from input stream via
         * InputStream.read() call.
         * The purpose of spawning separate "reader" thread for
         * InputStream.read() calls is to provide main thread with possibility
         * to time out read operation, since InputStream.read() blocks
         * indefinitely if there are no data available.
         */
        mReaderThread = new PicReaderThread(mRecvFifo, mPicIn);

        /* Start reader thread */
        mReaderThread.start();

        mConnectionEstablished = true;

        return true;
    }

    /**
     * Closes communication channel.
     *
     * @return  <code>true</code> if communication channel successfully closed;
     *          <code>false</code> otherwise.
     */
    public boolean close() {
        if (mConnectionEstablished) {
            mConnectionEstablished = false;

            /* Signal reader thread that it should finish its job. */
            mReaderThread.interrupt();

            /*
             * In order to make the thread interruptible, it needs to leave
             * blocking I/O. To accomplish that, let it receive at least
             * one character from PIC module.
             */
            write("<QS>");

            try {
                mReaderThread.join(5000);
            } catch (InterruptedException e) {
                /* Could not join communication thread */
                Log.e(TAG, "Interrupted while sleeping.", e);
            }

            Log.d(TAG, "Joining PIC reader thread...");
            if (mReaderThread.isAlive()) {
                /* Could not join communication thread. */
                Log.d(TAG, "FAILED.");
            } else {
                /* Thread joined. */
                Log.d(TAG, "JOINED.");
            }
        }

        if (mBtSocket != null) {
            /* Close Bluetooth communication socket. */
            Log.d(TAG, "Closing Bluetooth communication socket...");
            try {
                mBtSocket.close();
                Log.d(TAG, "CLOSED.");
            } catch (IOException e) {
                Log.d(TAG, "FAILED.", e);

                return false;
            } finally {
                mBtSocket = null;
            }
        }

        return true;
    }

    /**
     * Returns one character received from PIC module.
     * Blocks until character is available or timeout expires.
     *
     * @return  received character;
     *          -1 if timeout expired;
     *          -2 if connection with PIC module was lost.
     */
    public int read() {
        int res;

        /* Connection not established. */
        if (!mConnectionEstablished)
            return -2;

        res = mRecvFifo.get();

        /* Connection lost. */
        if (res == -2)
            mConnectionEstablished = false;

        return res;
    }

    /**
     * Sends one byte to PIC module.
     *
     * @param   data    the byte to be sent
     * @return  0 if operation succeeded;
     *          -1 otherwise.
     */
    public int write(byte data) {
        byte[] d = {data};

        return writeCommon(d);
    }

    /**
     * Sends array of bytes to PIC module.
     *
     * @param   data    the array of bytes to be sent
     * @return  0 if operation succeeded;
     *          -1 otherwise.
     */
    public int write(byte[] data) {
        return writeCommon(data);
    }

    /**
     * Sends String to PIC module.
     *
     * @param   data    the string to be sent
     * @return  0 if operation succeeded;
     *          -1 otherwise.
     */
    public int write(String data) {
        return writeCommon(data.getBytes());
    }

    /**
     * Constructor
     */
    public PicComBt(String modName) {
        super();

        mModName = modName;

        mConnectionEstablished = false;
    }

    /**
     * Common write method.
     *
     * @return  0 if operation succeeded;
     *          -1 otherwise.
     */
    private int writeCommon(byte[] data) {
        try {
            mPicOut.write(data);
        } catch (IOException e) {
            mConnectionEstablished = false;

            Log.e(ObdLogger.TAG, "Connection to PIC module lost during write.",
                    e);

            return -1;
        }

        return 0;
    }
}

