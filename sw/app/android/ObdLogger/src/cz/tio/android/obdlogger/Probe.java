package cz.tio.android.obdlogger;

import cz.tio.android.obdlogger.Obd;
import cz.tio.android.obdlogger.PicCom;
import cz.tio.android.obdlogger.PicConnectionLostException;
import cz.tio.android.obdlogger.PicTimeoutExpiredException;

/**
 * Abstract model of measurement probe as a source of measured
 * or calculated physical quantity.
 *
 * The class carries following attributes:
 *
 *   mId - unique probe ID
 *   mLabel - short label (displayed on dashboard and in log file)
 *   mDesc - description string
 *   mFormatString - format string used to convert parameter value to text
 *                   which can be displayed
 *   mPhysToPbAdd, mPhysToPbMul   - additive and multiplicative constant used
 *                                  when converting physical value
 *                                  to progress bar fraction
 *   mUnits - string holding physical units
 *   mRawData - last acquired raw data
 *   mCurrY - last acquired value
 *   mCurrYValid - true if last acquired value is valid
 */
public abstract class Probe {
    /* Probe unique ID */
    String mId;

    /* last acquired raw data */
    int[] mRawData;

    /* last acquired value */
    double mCurrY = 0.0;

    /* indicates that mCurrY contains valid information */
    boolean mCurrYValid = false;

    /* private attributes */
    /* Label - displayed on "dashboard" and in log file - e.g. PRESSURE */
    private String mLabel = "";

    /* Description - e.g. "Atmospheric pressure" */
    private String mDesc = "";

    /* Physical units - e.g. MPa */
    private String mUnits = "-";

    /*
     * Format string used when physical value is converted to text string -
     * e.g. for purposes of displaying the text on dashboard or logging it.
     */
    private String mFormatString = "%f";

    /*
     * Additive and multiplicative constant used for conversion
     * of physical value to progress bar fraction.
     */
    private double mPhysToPbAdd = 0.0;
    private double mPhysToPbMul = 1.0;

    /**
     * Constructor
     */
    public Probe(String id, String units, String desc, String label, 
        String formatString, double pbAdd, double pbMul) {

        mId = id;
        mDesc = desc;
        mLabel = label;
        mFormatString = formatString;
        mUnits = units;

        mPhysToPbAdd = pbAdd;
        mPhysToPbMul = pbMul;
    }

    /**
     * Returns raw probe data.
     *
     * @return  raw data
     *
     */
    public int[] getRawData() {
        return mRawData;
    }

    /**
     * Updates probe value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     */
    public boolean update(int[] value) {
        mRawData = value;

        return true;
    }

    /**
     * Asks for probe value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                     waiting for data from PIC
     *          PicConnectionLostException when lost connection with PIC
     */
    public boolean acquire()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        return true;
    }

    /**
     * Returns probe ID.
     *
     * @return probe ID
     */
    public String getId() {
        return mId;
    }

    /**
     * Returns label.
     *
     * @return probe label
     */
    public String getLabel() {
        return mLabel;
    }

    /**
     * Returns description.
     *
     * @return probe description
     */
    public String getDescription() {
        return mDesc;
    }

    /**
     * Returns units.
     *
     * @return probe units
     */
    public String getUnits() {
        return mUnits;
    }

    /**
     * Checks if last acquired value is valid.
     *
     * @return <code>true</code> if last acquired value is valid;
     *         <code>false</code> otherwise.
     */
    public boolean currYIsValid() {
        return mCurrYValid;
    }

    /**
     * Returns last acquired value.
     *
     * @return last acquired value
     */
    public double getCurrY() {
        return mCurrY;
    }

    /**
     * Converts physical value to text which is to be displayed on "dashboard".
     * Appends physical units if there are any. Replaces decimal point
     * with decimal comma.
     *
     * @return text to be displayed on dashboard
     */
    public String physToText() {
        StringBuffer s;

        if (!mCurrYValid) {
            return "";
        }

        /* If format string assumes integer value, cast appropriately. */
        if (mFormatString.endsWith("X"))
            s = new StringBuffer(String.format(mFormatString,
                    ((long) mCurrY) & 0xffffffffL));
        else
            s = new StringBuffer(String.format(mFormatString, mCurrY));

        if (!mUnits.equals("-")) {
            s.append(" " + mUnits);
        }

        if (s.indexOf(".") != -1) {
            s.setCharAt(s.indexOf("."), ',');
        }

        return s.toString();
    }

    /**
     * Converts PID value to text which is to be logged to file.
     * Replaces decimal comma with decimal point.
     *
     * @return text to be logged into file
     */
    public String physToLog() {
        if (!mCurrYValid) {
            return "";
        }

        StringBuffer s = new StringBuffer(String.format(mFormatString, mCurrY));

        if (s.indexOf(",") != -1) {
            s.setCharAt(s.indexOf(","), '.');
        }

        return s.toString();
    }

    /**
     * Converts parameter value to fraction displayed via progress bar
     * on dashboard.
     *
     * @return value to be displayed on dashboard in form of progress bar
     */
    public double physToPbFraction() {
        if (!mCurrYValid) {
            return 0.0;
        }

        return (mCurrY + mPhysToPbAdd) * mPhysToPbMul;
    }
}

/**
 * Represents PIC probe acquired directly by PIC module. Extends Probe class.
 *
 * Adds following attributes:
 *
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 *   mPicCom - reference to PicCom object providing methods for communication
 *             with PIC module.
 */
class PicProbe extends Probe {
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;
    private PicCom mPicCom = null;

    /**
     * Constructor
     */
    public PicProbe(String pid, String units, String desc, String label,
            String formatString, double kAdd, double kMul,
            double pbAdd, double pbMul, PicCom picCom) {

        /* Initialize attributes of parent class */
        super(pid, units, desc, label, formatString, pbAdd, pbMul);

        mRawToPhysAdd = kAdd;
        mRawToPhysMul = kMul;

        mPicCom = picCom;
    }

    /**
     * Updates probe value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     */
    public boolean update(int[] value) {
        mCurrY = (value[0] + mRawToPhysAdd) * mRawToPhysMul;

        mCurrYValid = true;

        return true;
    }

    /**
     * Asks PIC for probe value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     * @throws  PicTimeoutExpiredException when timeout expires while
     *                                     waiting for data from PIC
     *          PicConnectionLostException when lost connection with PIC
     */
    public boolean acquire()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        int[] picAdValue = new int[1];

        try {
            picAdValue[0] = mPicCom.getAdValue("08");
        } catch (PicTimeoutExpiredException e) {
            mCurrYValid = false;

            throw e;
        } catch (PicConnectionLostException e) {
            mCurrYValid = false;

            throw e;
        } 

        return this.update(picAdValue);
    }
}

/**
 * Represents OBD probe measured by ECU and obtained via diagnostic
 * communication protocol. Extends Probe class.
 *
 * Adds following attributes:
 *
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 *   mObd - reference to Obd object providing methods for communication
 *          with ECU.
 */
class ObdProbe extends Probe {
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;
    private Obd mObd = null;

    /**
     * Constructor
     */
    public ObdProbe(String pid, String units, String desc, String label,
            String formatString, double kAdd, double kMul,
            double pbAdd, double pbMul, Obd obd) {

        /* Initialize attributes of parent class */
        super(pid, units, desc, label, formatString, pbAdd, pbMul);

        mRawToPhysAdd = kAdd;
        mRawToPhysMul = kMul;

        mObd = obd;
    }

    /**
     * Updates probe value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     */
    public boolean update(int[] value) {
        super.update(value);

        /**
         * Converts PID data returned by ECU in form of an array of bytes
         * to physical value using following formula:
         *
         *   mCurrY = (mRawY + mRawToPhysAdd) * mRawToPhysMul,
         *
         *   where mRawY is summary of weighted bytes returned from ECU.
         *   For instance, if ECU returned two bytes, mRawY would be
         *   calculated as:
         *
         *   mRawY = 256 * value[0] + value[1]
         */
        int mRawY = 0;
    
        for (int d : value) {
            mRawY *= 256;
            mRawY += d;
        }

        mCurrY = (mRawY + mRawToPhysAdd) * mRawToPhysMul;

        mCurrYValid = true;

        return true;
    }

    /**
     * Obtains current value of OBD parameter from ECU.
     * Updates probe with new value.
     *
     * @return  <code>true</code> in case of success;
     *          <code>false</code> otherwise.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public boolean acquire()
            throws PicTimeoutExpiredException, PicConnectionLostException {
        int[] ecuData;

        try {
            ecuData = mObd.getPidValue(mId);
        } catch (PicTimeoutExpiredException e) {
            mCurrYValid = false;

            throw e;
        } catch (PicConnectionLostException e) {
            mCurrYValid = false;

            throw e;
        } 

        return this.update(ecuData);
    }

    /**
     * Obtains 'freeze frame' value of OBD parameter from ECU.
     * Updates probe with new value.
     *
     * @param    ffNum freeze frame number
     * @return   <code>true</code> in case of success;
     *           <code>false</code> otherwise.
     * @throws   PicTimeoutExpiredException when timeout expires while
     *                                      waiting for data from PIC
     *           PicConnectionLostException when lost connection with PIC
     */
    public boolean acquire(int ffNum)
            throws PicTimeoutExpiredException, PicConnectionLostException {
        int[] ecuData;

        try {
            ecuData = mObd.getFreezeFramePidValue(mId, ffNum);
        } catch (PicTimeoutExpiredException e) {
            mCurrYValid = false;

            throw e;
        } catch (PicConnectionLostException e) {
            mCurrYValid = false;

            throw e;
        } 

        return this.update(ecuData);
    }

    /**
     * Applies correction to mRawToPhysMul multiplicative constant.
     *
     * @param   mulCorr the correction value
     */
    public void adjustRawToPhysMul(double mulCorr) {
        mRawToPhysMul *= mulCorr;
    }
}

/**
 * Represents Turbo Indicator, extends Probe class. It is calculated
 * as Intake Pressure/Barometric Pressure ratio. It adds following attributes:
 *
 *   mProbeList - list of probes used to calculate the value.
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 */
class TurboProbe extends Probe {
    private String[] mProbeList = {"0B", "33"};
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;

    /**
     * Constructor
     */
    public TurboProbe() {
        /* Initialize attributes of parent class */
        super("Turbo", "-", "Turbo Indicator", "TURBO", "%.2f",
                -0.7, 1.0 / (2.0 - 0.7));

        mRawToPhysAdd = 0.0;
        mRawToPhysMul = 1.0;
    }

    /**
     * Recalculates value if given PID contributes to the formula.
     *
     * @param   pid     OBD PID
     * @param   obd     reference to Obd object
     * @return          <code>true</code> if value has been recalculated;
     *                  <code>false</code> otherwise.
     */
    public boolean update(String pid, Obd obd) {
        for (String s : mProbeList) {
            if (pid.equals(s)) {
                if (obd.getProbe("0B").currYIsValid() &&
                        obd.getProbe("33").currYIsValid() &&
                        obd.getProbe("33").getCurrY() != 0) {
                    mCurrY = obd.getProbe("0B").getCurrY() /
                            obd.getProbe("33").getCurrY();

                    mCurrYValid = true;

                    return true;
                }

                return false;
            }
        }
        return false;
    }
}

/**
 * Represents EGT probe, extends Probe class.
 * Adds following attributes:
 *
 *   mObdPid - OBD pid carrying information about particular EGT sensor.
 *   mEgtNum - EGT sensor number (one OBD PID can carry information about
 *             up to 4 EGT sensors).
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 */
class EgtProbe extends Probe {
    private String mObdPid;
    private int mEgtNum;
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;

    /**
     * Constructor
     */
    public EgtProbe(String egtName, String obdPid, int egtNum) {
        /* Initialize attributes of parent class */
        super(egtName, "stC", egtName, egtName, "%.0f", 0.0, 1.0);

        mObdPid = obdPid;
        mEgtNum = egtNum;

        mRawToPhysAdd = -400.0;
        mRawToPhysMul = 0.1;
    }

    /**
     * Recalculates value if given PID contributes to the formula.
     *
     * @param   pid     OBD PID
     * @param   obd     reference to Obd object
     * @return          <code>true</code> if value has been recalculated;
     *                  <code>false</code> otherwise.
     */
    public boolean update(String pid, Obd obd) {
        if (pid.equals(mObdPid)) {
            if (obd.getProbe(pid).currYIsValid()) {
		int[] raw = obd.getProbe(pid).getRawData();

                /* Malformed data - do not check for now. */
                /*
                 * if (raw.length != 9) {
                 *   return false;
                 * }
                 */

                /* Specified EGT sensor not supported. */
                if ((raw[0] & (1 << mEgtNum)) == 0) {
                    return false;
                }

                mCurrY = 256.0 * raw[mEgtNum * 2 + 1] +
                        raw[(mEgtNum + 1) * 2];

                mCurrY += mRawToPhysAdd;
                mCurrY *= mRawToPhysMul;

                mCurrYValid = true;

                return true;
            }

            return false;
        }
        return false;
    }
}

/**
 * Represents 'Injector Feedback Value' Toyota  probe, extends Probe class.
 * Adds following attributes:
 *
 *   mObdPid   - OBD pid carrying information about particular Injector FB.
 *   mInjNum   - injector num (0..3)- PID carries information about 4 injectors.
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 */
class InjFbProbe extends Probe {
    private String mObdPid;
    private int mInjNum;
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;

    /**
     * Constructor
     */
    public InjFbProbe(String injName, String obdPid, int injNum) {
        /* Initialize attributes of parent class */
        super(injName, "-", injName, injName, "%.0f", 0.0, 1.0);

        mObdPid = obdPid;
        mInjNum = injNum;

        mRawToPhysAdd = -64.0;
        mRawToPhysMul = 1.0;
    }

    /**
     * Recalculates value if given PID contributes to the formula.
     *
     * @param   pid     OBD PID
     * @param   obd     reference to Obd object
     * @return          <code>true</code> if value has been recalculated;
     *                  <code>false</code> otherwise.
     */
    public boolean update(String pid, Obd obd) {
        if (pid.equals(mObdPid)) {
            if (obd.getProbe(pid).currYIsValid()) {
                int[] raw = obd.getProbe(pid).getRawData();

                /* Malformed data - do not check for now. */
                /*
                 * if (raw.length != 29) {
                 *   return false;
                 * }
                 */
                /* debug only */
                /* mCurrY = raw[0 + mInjNum]; */
                mCurrY = raw[17 + mInjNum];

                mCurrY += mRawToPhysAdd;
                mCurrY *= mRawToPhysMul;

                mCurrYValid = true;

                return true;
            }

            return false;
        }
        return false;
    }
}

/**
 * Represents 'compound probe', extends Probe class.
 * Compound probes handle cases when more logical probes are delivered
 * within one physical ECU probe.
 * The design is still work in progress.
 * Currently it is used to extract information about
 * DPF PM Accumulation Ratio from Toyota PID 'DD'
 *
 * CompoundProbe Adds following attributes:
 *
 *   mObdPid - OBD pid carrying information about compound probe.
 *   mPos    - beginning of compound probe
 *   mSize   - size of compound probe
 *   mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
 *                                  when converting raw value to physical one
 */
class CompoundProbe extends Probe {
    private String mObdPid;
    private int mPos, mSize;
    private double mRawToPhysAdd = 0.0;
    private double mRawToPhysMul = 1.0;

    /**
     * Constructor
     */
    public CompoundProbe(String probeName, String obdPid, int pos, int size) {
        /* Initialize attributes of parent class */
        super(probeName, "%", probeName, probeName, "%.0f", 0.0, 1.0);

        mObdPid = obdPid;
        mPos = pos;
        mSize = size;

        mRawToPhysAdd = 0.0;
        mRawToPhysMul = 2.0;
    }

    /**
     * Recalculates value if given PID contributes to the formula.
     *
     * @param   pid     OBD PID
     * @param   obd     reference to Obd object
     * @return          <code>true</code> if value has been recalculated;
     *                  <code>false</code> otherwise.
     */
    public boolean update(String pid, Obd obd) {
        if (pid.equals(mObdPid)) {
            if (obd.getProbe(pid).currYIsValid()) {
		int[] raw = obd.getProbe(pid).getRawData();

                /* Malformed data - do not check for now. */
                /*
                 * if (raw.length != 22) {
                 *   return false;
                 * }
                 */

                /*
                 * Re-calculate current value.
                 * Hardcoded for now for DPF PMar PID - last byte
                 * of ECU PID carries that information.
                 */
                mCurrY = raw[raw.length - 1] * 2.0;

                mCurrYValid = true;

                return true;
            }

            return false;
        }
        return false;
    }
}
