from lxml import etree

class Probe(object):
    """
    Abstract model of measurement probe as a source of measured
    or calculated physical quantity.

    The class carries following attributes:

    mId - unique probe ID
    mLabel - short label (displayed on dashboard and in log file)
    mDesc - description string
    mFormatString - format string used to convert parameter value to text
                    which can be displayed
    mUnits - string holding physical units
    mRawData - last acquired raw data
    mCurrY - last acquired value
    mCurrYValid - true if last acquired value is valid
    """

    def __init__(self, pid, units="-", desc="", label="", formatString="%f"):
        # Probe unique ID
        self.mId = pid

        # Description - e.g. "Atmospheric pressure"
        self.mDesc = desc

        # Label - displayed on "dashboard" and in log file - e.g. PRESSURE
        self.mLabel = label

        #
        # Format string used when physical value is converted to text string -
        # e.g. for purposes of displaying the text on dashboard or logging it.
        #
        self.mFormatString = formatString

        # Physical units - e.g. MPa
        self.mUnits = units

        # last acquired raw data
        self.mRawData = []

        # last calculated value
        self.mCurrY = 0.0

        # indicates that mCurrY contains valid information
        self.mCurrYValid = False

    def get_raw_data(self):
        """ Returns raw probe data. """
        return self.mRawData

    def update(self, value):
        """ Updates probe value.
        Returns True in case of success; False otherwise.
        """
        self.mRawData = value

        return True

    def get_id(self):
        """ Returns probe ID. """
        return self.mId

    def get_label(self):
        """ Returns label. """
        return self.mLabel

    def get_description(self):
        """ Returns description. """
        return self.mDesc

    def get_units(self):
        """ Returns label. """
        return self.mUnits

    def curr_y_is_valid(self):
        """ Checks if last acquired value is valid.
        Returns True if last acquired value is valid; False otherwise.
        """
        return self.mCurrYValid

    def get_curr_y(self):
        """ Returns last calculated value. """
        return self.mCurrY

    def phys_to_text(self):
        """ Converts probe value to text which can be displayed on "dashboard".
        Appends physical units if there are any. Replaces decimal point
        with decimal comma.
        """
        if not self.mCurrYValid:
            return ""

        s = self.mFormatString % self.mCurrY

        if self.mUnits != "-":
            s += " " + self.mUnits

        s.replace(".", ",")

        return s

    def phys_to_log(self):
        """ Converts PID value to text which can be logged to file.
        Replaces decimal comma with decimal point.
        """
        if not self.mCurrYValid:
            return ""

        s = self.mFormatString % self.mCurrY
        s.replace(",", ".")

        return s


class PicProbe(Probe):
    """
    Represents PIC probe acquired directly by PIC module. Extends Probe class.

    Adds following attributes:

    mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
                                   when converting raw value to physical one
    """
    def __init__(self, pid, units, desc, label, formatString, kAdd, kMul):
        Probe.__init__(self, pid, units, desc, label, formatString)

        self.mRawToPhysAdd = kAdd
        self.mRawToPhysMul = kMul

    def update(self, value):
        """ Updates probe value.
        Returns True in case of success; False otherwise.
        """
        self.mCurrY = (value[0] + self.mRawToPhysAdd) * self.mRawToPhysMul

        self.mCurrYValid = True

        return True


class ObdProbe(Probe):
    """
    Represents OBD probe measured by ECU and obtained via diagnostic
    communication protocol. Extends Probe class.

    Adds following attributes:

    mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
                                   when converting raw value to physical one
    """
    def __init__(self, pid, units, desc, label, formatString, kAdd, kMul):
        super(ObdProbe, self).__init__(pid, units, desc, label, formatString)

        self.mRawToPhysAdd = kAdd
        self.mRawToPhysMul = kMul

    def update(self, value):
        """ Updates probe value.
        Returns True in case of success; False otherwise.
        """
        Probe.update(self, value)

        #
        # Converts PID data returned by ECU in form of an array of bytes
        # to physical value using following formula:
        #
        #   mCurrY = (mRawY + mRawToPhysAdd) * mRawToPhysMul,
        #
        #   where mRawY is summary of weighted bytes returned from ECU.
        #   For instance, if ECU returned two bytes, mRawY would be
        #   calculated as:
        #
        #   mRawY = 256 * value[0] + value[1]
        #
        self.mRawY = 0
    
        for d in value:
            self.mRawY *= 256
            self.mRawY += d

        self.mCurrY = (self.mRawY + self.mRawToPhysAdd) * self.mRawToPhysMul

        self.mCurrYValid = True

        return True

    def adjust_raw_to_phys_mul(self, mulCorr):
        """ Applies correction to mRawToPhysMul multiplicative constant. """
        self.mRawToPhysMul *= mulCorr


class TurboProbe(Probe):
    '''
    Represents Turbo Indicator, extends Probe class. It is calculated
    as Intake Pressure/Barometric Pressure ratio. It adds following
    attributes:

    mProbeList   - list of probes used to calculate the value.
    mProbeValues - dictionary of probe values used to calculate
                   value of turbo probe
    '''
    def __init__(self):
        super(TurboProbe, self).__init__('Turbo', '-', 'Turbo Indicator',
                                         'TURBO', '%.2f')
        self.mProbeList = ['0B', '33']
        self.mProbeValues = {}

    def update(self, probe):
        '''
        Recalculates value if given probe contributes to the formula.
        Returns True if value has been recalculated;
        False otherwise.
        '''
        pid = probe.get_id()
        if pid not in self.mProbeList:
            return False

        if probe.curr_y_is_valid():
            self.mProbeValues[pid] = probe.get_curr_y()

        # If all necessary data available, recalculate the probe.
        for key in self.mProbeList:
            if not self.mProbeValues.has_key(key):
                return False

        self.mCurrY = self.mProbeValues['0B'] / self.mProbeValues['33']
        self.mCurrYValid = True

        return True


class EgtProbe(Probe):
    """
    Represents EGT probe, extends Probe class.
    Adds following attributes:

    mObdPid - OBD pid carrying information about particular EGT sensor.
    mEgtNum - EGT sensor number (one OBD PID can carry information about
              up to 4 EGT sensors).
    mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
                                   when converting raw value to physical one
    """
    def __init__(self, egtName, obdPid, egtNum):
        super(EgtProbe, self).__init__(egtName, "stC", egtName, egtName,
                                       "%.0f")

        self.mObdPid = obdPid
        self.mEgtNum = egtNum

        self.mRawToPhysAdd = -400.0
        self.mRawToPhysMul = 0.1

    def get_obd_pid(self):
        """ Returns obd PID used to calculate value of this probe. """
        return self.mObdPid

    def update(self, raw):
        """ Recalculates value from given raw data.
        Returns True if value has been recalculated; False otherwise.
        """
        #
        # Malformed data - do not check for now./
        #
        # if len(raw) != 9:
        #     return False
        #

        # Specified EGT sensor not supported.
        if raw[0] & (1 << self.mEgtNum) == 0:
            return False

        self.mCurrY = 256.0 * raw[self.mEgtNum * 2 + 1] + \
                      raw[(self.mEgtNum + 1) * 2]

        self.mCurrY += self.mRawToPhysAdd
        self.mCurrY *= self.mRawToPhysMul

        self.mCurrYValid = True

        return True


class InjFbProbe(Probe):
    """
    Represents 'Injector Feedback Value' Toyota  probe, extends Probe class.
    Adds following attributes:

    mObdPid   - OBD pid carrying information about particular Injector FB.
    mInjNum   - injector num (0..3)- PID carries information about 4 injectors.
    mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
                                   when converting raw value to physical one
    """
    def __init__(self, injName, obdPid, injNum):
        super(InjFbProbe, self).__init__(injName, "-", injName, injName, "%.0f")

        self.mObdPid = obdPid
        self.mInjNum = injNum

        self.mRawToPhysAdd = -64.0
        self.mRawToPhysMul = 1.0

    def get_obd_pid(self):
        """ Returns obd PID used to calculate value of this probe. """
        return self.mObdPid

    def update(self, raw):
        """ Recalculates value from given raw value.
        Returns True if value has been recalculated; False otherwise.
        """
        #
        # Malformed data - do not check for now.
        #
        # if len(raw) != 29:
        #     return False
        #

        #
        # If packet has less than 17 bytes,
        # then we are operating in debug mode.
        #
        if len(raw) < 17:
            self.mCurrY = raw[0 + self.mInjNum]
        else:
            self.mCurrY = raw[17 + self.mInjNum]

        self.mCurrY += self.mRawToPhysAdd
        self.mCurrY *= self.mRawToPhysMul

        self.mCurrYValid = True


class CompoundProbe(Probe):
    """
    Represents 'compound probe', extends Probe class.
    Compound probes handle cases when more logical probes are delivered
    within one physical ECU probe.
    The design is still work in progress.
    Currently it is used to extract information about
    DPF PM Accumulation Ratio from Toyota PID 'DD'

    CompoundProbe Adds following attributes:

    mObdPid - OBD pid carrying information about compound probe.
    mPos    - beginning of compound probe
    mSize   - size of compound probe
    mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
                                   when converting raw value to physical one
    """
    def __init__(self, probeName, obdPid, pos, size):
        super(CompoundProbe, self).__init__(probeName, "%", probeName,
                                            probeName, "%.0f")

        self.mObdPid = obdPid
        self.mPos = pos
        self.mSize = size

        self.mRawToPhysAdd = 0.0
        self.mRawToPhysMul = 2.0

    def get_obd_pid(self):
        """ Returns obd PID used to calculate value of this probe. """
        return self.mObdPid

    def update(self, raw):
        """ Recalculates value from given raw data.
        Returns True if value has been recalculated; False otherwise.
        """
        #
        # Malformed data - do not check for now.
        #
        # if len(raw) != 22:
        #     return False
        #

        #
        # Re-calculate current value.
        # Hardcoded for now for DPF PMar PID - last byte
        # of ECU PID carries that information.
        #
        self.mCurrY = raw[-1] * 2.0

        self.mCurrYValid = True

        return True


class ProbePool(object):
    """ Pool of all available Probes.
    """
    # Path to XML file describing available probes
    PROBE_XML_PATH = "./android/ObdLogger/res/xml/probes.xml"

    # True once pool of probes is initialized
    mPoolInitialized = False
    mProbePool = {}

    def __init__(self):
        # Initialize only once.
        if ProbePool.mPoolInitialized:
            return

        # Commence parsing of XML file describing probes
        tree = etree.parse(ProbePool.PROBE_XML_PATH)

        # Iterate over children of <ProbeList> root tag
        for probe_list_el in tree.getroot():
            if probe_list_el.tag == "ObdProbes":
                # <ObdProbes> list
                is_obd_probe = True
            elif probe_list_el.tag == "PicProbes":
                # <PicProbes> list
                is_obd_probe = False
            else:
                # Unkonown probes, discard them
                continue

            # Iterate over particular probes and add them to the probe pool
            for probe_el in probe_list_el.iterchildren():
                # Discard comments and elements not describing probes
                if probe_el.tag != "Pid":
                    continue

                # Obtain properties for particular probe
                for probe_prop_el in probe_el.iterchildren():
                    if probe_prop_el.tag == "Id":
                        pid = probe_prop_el.text
                    elif probe_prop_el.tag == "Description":
                        description = probe_prop_el.text
                    elif probe_prop_el.tag == "Label":
                        label = probe_prop_el.text
                    elif probe_prop_el.tag == "Units":
                        units = probe_prop_el.text
                    elif probe_prop_el.tag == "RawToPhysAdd":
                        raw_to_phys_add = float(probe_prop_el.text)
                    elif probe_prop_el.tag == "RawToPhysMul":
                        raw_to_phys_mul = float(probe_prop_el.text)
                    elif probe_prop_el.tag == "PhysToString":
                        phys_to_string = probe_prop_el.text

                if is_obd_probe:
                    new_probe = ObdProbe(pid, units, description, label,
                                         phys_to_string, raw_to_phys_add,
                                         raw_to_phys_mul)
                else:
                    new_probe = PicProbe(pid, units, description, label,
                                         phys_to_string, raw_to_phys_add,
                                         raw_to_phys_mul)

                # Add probe to the probe pool
                ProbePool.mProbePool[pid] = new_probe

        #
        # Adjust multiplication constant for '0B' probe - hardcoded for now.
        # Adjustment should be obtained from PID '4F'.
        #
        ProbePool.mProbePool['0B'].adjust_raw_to_phys_mul(10.0 * 32 / 255.0)

        # Add "synthetic" probes to the pool

        #
        # 'Turbo' probe. Calculated as Intake pressure/Barometric pressure
        # ratio.
        #
        ProbePool.mProbePool['Turbo'] = TurboProbe()

        # EGT probes
        ProbePool.mProbePool['EGT11'] = EgtProbe('EGT11', '78', 0)
        ProbePool.mProbePool['EGT12'] = EgtProbe('EGT12', '78', 1)

        # Injector Feedback Values
        ProbePool.mProbePool['Inj1'] = InjFbProbe('Inj1', 'AD', 0)
        ProbePool.mProbePool['Inj2'] = InjFbProbe('Inj2', 'AD', 1)
        ProbePool.mProbePool['Inj3'] = InjFbProbe('Inj3', 'AD', 2)
        ProbePool.mProbePool['Inj4'] = InjFbProbe('Inj4', 'AD', 3)

        # DPF PM Accumulation Ratio probe.
        ProbePool.mProbePool['DPFar'] = CompoundProbe('DPFar', 'DD', 21, 1)

        ProbePool.mPoolInitialized = True

    def get_probe(self, pid):
        """Returns probe for specified PID."""
        return ProbePool.mProbePool[pid]
