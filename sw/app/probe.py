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
            if not key in self.mProbeValues:
                return False

        self.mCurrY = self.mProbeValues['0B'] / self.mProbeValues['33']
        self.mCurrYValid = True

        return True


class CompoundProbe(Probe):
    """
    Represents 'compound probe', extends Probe class.
    Compound probes handle cases when more logical probes are delivered
    within one physical ECU probe.
    The design is still work in progress.

    CompoundProbe Adds following attributes:

    mObdPid - OBD pid carrying information about compound probe.
    mPos    - beginning of compound probe
    mSize   - size of compound probe
    mRawToPhysAdd, mRawToPhysMul - additive and multiplicative constant used
                                   when converting raw value to physical one
    """
    def __init__(self, probeName, units, desc, label, formatString, kAdd,
                 kMul, obdPid, pos, size):
        super(CompoundProbe, self).__init__(probeName, units, desc, label,
                                            formatString)

        self.mObdPid = obdPid
        self.mPos = int(pos)
        self.mSize = int(size)

        self.mRawToPhysAdd = kAdd
        self.mRawToPhysMul = kMul

    def get_obd_pid(self):
        """ Returns obd PID used to calculate value of this probe. """
        return self.mObdPid

    def update(self, raw):
        """ Recalculates value from given raw data.
        Returns True if value has been recalculated; False otherwise.
        """
        #
        # Re-calculate current value.
        #
        self.mCurrY = 0.0

        for i in range(self.mSize):
            self.mCurrY *= 256.0
            self.mCurrY += raw[self.mPos + i]

        self.mCurrY += self.mRawToPhysAdd
        self.mCurrY *= self.mRawToPhysMul

        self.mCurrYValid = True

        return True

    def adjust_raw_to_phys_mul(self, mulCorr):
        """ Applies correction to mRawToPhysMul multiplicative constant. """
        self.mRawToPhysMul *= mulCorr


class ProbePool(object):
    """ Pool of all available Probes.
    """
    # Path to directory with XML files describing available probes
    PROBE_XML_DIR = "./android/ObdLogger/res/xml/"

    # True once pool of probes is initialized
    mPoolInitialized = False
    mProbePool = {}

    # Dictionary mapping OBD PIDs to compound probes
    mObdPid2Compound = {}

    def __init__(self, probe_def='2adftv'):
        # Initialize only once.
        if ProbePool.mPoolInitialized:
            return

        probe_fname = 'probes_' + probe_def + '.xml'

        # Commence parsing of XML file describing probes
        tree = etree.parse(ProbePool.PROBE_XML_DIR + probe_fname)

        # Iterate over children of <ProbeList> root tag
        for probe_list_el in tree.getroot():
            if probe_list_el.tag == "ObdProbes":
                # <ObdProbes> list
                probe_type = "obd"
            elif probe_list_el.tag == "PicProbes":
                # <PicProbes> list
                probe_type = "pic"
            elif probe_list_el.tag == "CompoundProbes":
                # <CompoundProbes> list
                probe_type = "compound"
            else:
                # Unknown probes, discard them
                continue

            # Iterate over particular probes and add them to the probe pool
            for probe_el in probe_list_el.iterchildren():
                # Discard comments and elements not describing probes
                if probe_el.tag != "Pid" and probe_el.tag != "CompoundPid" :
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
                    elif probe_prop_el.tag == "Pos":
                        compound_pos = probe_prop_el.text
                    elif probe_prop_el.tag == "Length":
                        compound_ln = probe_prop_el.text
                    elif probe_prop_el.tag == "ObdPid":
                        compound_obd_pid = probe_prop_el.text

                if probe_type == "obd":
                    new_probe = ObdProbe(pid, units, description, label,
                                         phys_to_string, raw_to_phys_add,
                                         raw_to_phys_mul)
                elif probe_type == "pic":
                    new_probe = PicProbe(pid, units, description, label,
                                         phys_to_string, raw_to_phys_add,
                                         raw_to_phys_mul)
                else:
                    new_probe = CompoundProbe(pid, units, description, label,
                                         phys_to_string, raw_to_phys_add,
                                         raw_to_phys_mul, compound_obd_pid,
                                         compound_pos, compound_ln)

                    # Save relationship between compound probe and OBD PID
                    if (compound_obd_pid in ProbePool.mObdPid2Compound):
                        ProbePool.mObdPid2Compound[compound_obd_pid].append(pid)
                    else:
                        ProbePool.mObdPid2Compound[compound_obd_pid] = [pid]

                # Add probe to the probe pool
                ProbePool.mProbePool[pid] = new_probe

        #
        # Adjust multiplication constant for O2 equivalence probes.
        # Hardcoded for now - should be obtained from the 1st byte of PID '4F'.
        #
        ProbePool.mProbePool['O2S11Ae'].adjust_raw_to_phys_mul(8.0 / 2.0)
        ProbePool.mProbePool['O2S11Be'].adjust_raw_to_phys_mul(8.0 / 2.0)

        #
        # Adjust multiplication constant for '0B' probe - hardcoded for now.
        # Hardcoded for now - should be obtained from the 4th byte of PID '4F'.
        #
        ProbePool.mProbePool['0B'].adjust_raw_to_phys_mul(10.0 * 32 / 255.0)

        # Add "synthetic" probes to the pool

        #
        # 'Turbo' probe. Calculated as Intake pressure/Barometric pressure
        # ratio.
        #
        ProbePool.mProbePool['Turbo'] = TurboProbe()

        ProbePool.mPoolInitialized = True

    def get_probe(self, pid):
        """Returns probe for specified PID."""
        return ProbePool.mProbePool[pid]

    def is_compound_probe(self, pid):
        """Returns True if given OBD PID carries compound probe(s)"""
        return pid in ProbePool.mObdPid2Compound

    def get_compound_list(self, pid):
        """Returns list of compound probes for given OBD PID."""
        return ProbePool.mObdPid2Compound[pid]
