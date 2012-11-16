#!/usr/bin/python

import gobject
import gtk
import pango
import pygtk
pygtk.require('2.0')

import serial
import sys
import time
import threading

class OBDParameter(object):
    '''Class representing OBD Parameter. OBD parameter is specified via
    set of following attributes:

        pid - Parameter ID
        desc - description string
        label - short label (displayed on dashboard and in log file)
        data_length - number of bytes ECU sends when asked for parameter value
        k_add, k_mul - additive and multiplicative constant used when
                       converting raw value to physical one
        f_add, f_mul - additive and multiplicative constant used when
                       converting physical value to progress bar fraction
        format_string - format string used to convert parameter value to text
                        which can be displayed
        units - string holding physical units
    '''

    def __init__(self, pid, desc='', label='', data_length=1,
                 k_add=0.0, k_mul=1.0, f_add=0.0, f_mul=1.0,
                 format_string='%.1f', units='-'):
        self.pid = pid
        self.desc = desc
        self.label = label
        self.data_length = data_length
        self.k_add = k_add
        self.k_mul = k_mul
        self.f_add = f_add
        self.f_mul = f_mul
        self.format_string = format_string
        self.units = units
        self.y_raw = 0
        self.y_prev = self.y_curr = None

        #
        # Initialize list which holds bytes returned from ECU representing
        # parameter value.
        #
        self.ecu_data = [0] * self.data_length


    def raw2phys(self, ecu_data):
        '''Converts value returned by ECU as a list of bytes to physical one
        using following formula:

        phys = (raw + k_add) * k_mul,

        where raw is summary of weighted bytes returned from ECU. For instance,
        for two ECU bytes, raw would be calculated as:

        raw = 256 * ecu_data[0] + ecu_data[1]
        '''

        #
        # TODO: Double check that ecu_data list contains expected number of
        # bytes.
        #
        self.ecu_data = ecu_data

        self.y_raw = 0
        for i in range(len(self.ecu_data)):
            self.y_raw *= 256
            self.y_raw += self.ecu_data[i]

        self.y_curr = (self.y_raw + self.k_add) * self.k_mul


    def phys2text(self):
        '''Converts parameter value to text which can be displayed. Replace
        decimal point with decimal comma.
        '''

        txt = self.format_string % self.y_curr
        if self.units != '-':
            txt += ' ' + self.units
        return txt.replace('.', ',')


    def phys2log(self):
        '''Converts parameter value to text which will be logged to file.
        '''

        if self.y_curr is not None:
            return (self.format_string % self.y_curr)
        else:
            return ''


    def phys2pb_fraction(self):
        '''Converts parameter value to fraction displayed via progress bar.
        '''

        return (self.y_curr + self.f_add) * self.f_mul


    def value_changed(self):
        '''Return True if parameter value changed comparing with previous
        sample. Otherwise return False. Copy new value as previous one.
        '''

        if self.y_curr != self.y_prev:
            self.y_prev = self.y_curr
            return True
        else:
            return False

class TurboIndicator(OBDParameter):
    ''' Class representing Turbo Indicator. It's calculated as
        Intake Pressure/Barometric Pressure ratio.

        pid - Parameter ID
        desc - description string
        label - short label (displayed on dashboard and in log file)
        k_add, k_mul - additive and multiplicative constant used when
                       converting raw value to physical one
        f_add, f_mul - additive and multiplicative constant used when
                       converting physical value to progress bar fraction
        format_string - format string used to convert parameter value to text
                        which can be displayed
        units - string holding physical units

        pid_list - list of PIDs this one is calculated from
        

        overloads __init() and raw2phys() method of its parent class.
    '''

    def __init__(self, pid='Turbo', desc='Turbo Indicator', label='TURBO',
                 format_string='%.2f', units='-',
                 # progressbar range: <0.7; 2.0>
                 f_add = -0.7, f_mul = 1.0 / (2.0 - 0.7),
                 pid_list = ['0B', '33']):

        super(TurboIndicator, self).__init__(pid, desc, label, f_add=f_add,
              format_string=format_string, units=units)

        self.pid_list = pid_list


    def raw2phys(self, obd_pids):
        '''Calculates parameter value as
        Intake Pressure/Barometric Pressure ratio.
        '''
        if obd_pids['0B'].y_curr is not None and \
           obd_pids['33'].y_curr is not None and \
           obd_pids['33'].y_curr != 0:
            self.y_curr = obd_pids['0B'].y_curr / obd_pids['33'].y_curr

    def recalculate(self, pid):
        '''Returns True if parameter should be recalculated as a result
        of change of given PID.
        '''
        return (pid in self.pid_list)


def get_time():
    ''' Returns current time. '''
    return time.time()


def get_run_time():
    ''' Returns run time of the application. '''
    return (get_time() - app_start_time)


def time_expired(captured_time, expiration_time):
    ''' Returns True if timer expired. Otherwise returns False.'''
    if (captured_time + expiration_time) < get_time():
        return True
    else:
        return False


class PICConv(object):
    ''' PICConv class
    '''

    # states of receive state machine
    ST_WAIT4SOF = "st_wait4sof"
    ST_RECV_DATA = "st_recv_data"

    def __init__(self):
        self.debug = False
        if self.debug:
            self.comm_time = get_time()

        # Communication with PIC module not established yet.
        self.com_pic_established = False

        # open serial port for communication with PIC module
#        self.ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.5)
#        self.ser = serial.Serial('/dev/ttyUSB0', 115200)
#        self.ser = serial.Serial('/dev/ttyS0', 115200, timeout=1.0)
        self.ser = serial.Serial('/dev/rfcomm0', 115200, timeout=1.0)

    def comm_init(self):
        ''' Establish communication with PIC module.
        Return True if communication has been established, otherwise
        return False.
        '''
        # If communication channel is already established, return.
        if self.com_pic_established:
            return True

        # Make sure receive buffer is empty
	while self.ser.inWaiting() != 0:
            ch = self.ser.read(self.ser.inWaiting())

        # Initiate communication by reading value from A/D converter.
        self.send_packet('A', '08')
        p = self.recv_packet()
        if p == None:
            print "[%0.3f] Timeout when trying to initiate " \
                   "communication with PIC module" % get_run_time()

            return False

        elif p[0] == 'a':
            # Valid response received, communication established.
            self.com_pic_established = True
            print "Communication with PIC module established"
            return True

        else:
            print "Invalid PIC response, awaiting 'a', received %s" % p
            return False

    def send_packet(self, cmd, data):
        ''' Assemble and send packet to PIC module.
        '''
        pic_packet = '<' + cmd + data + '>'
        self.ser.write(pic_packet)

        if self.debug:
            print "[%0.3f] pic: <- %s" % (get_time() - self.comm_time,
                                           pic_packet)
            self.comm_time = get_time()

    def recv_packet(self):
        ''' Receives packet from PIC module.
        Returns None if timeout expired when waiting for packet, otherwise
        returns received packet.
        '''

        # State machine will wait for beginning of the packet.
        recv_state = PICConv.ST_WAIT4SOF

        #
        # Receive characters until whole packet is received or until
        # timeout expires.
        #
        while True:
            # Wait for character from PIC module.
            ch = self.ser.read(1)

            #
            # If timeout expired and no character has been received,
            # abort the process and return None.
            #
            if len(ch) == 0:
                return None

            # Waiting for beginning of packet.
            if recv_state == PICConv.ST_WAIT4SOF and ch == '<':
                p = ''
                recv_state = PICConv.ST_RECV_DATA
                continue

            # Receive packet.
            if recv_state == PICConv.ST_RECV_DATA:
                if ch == '<':
                    # SOF character received again, restart receive process.
                    p = ''
                elif ch == '>':
                    #
                    # EOF character, complete packet has been received.
                    # Pass data payload to the caller.
                    #
                    if self.debug:
                        print "[%0.3f] pic: -> %s" % (get_time() -
                                                       self.comm_time,
                                                       p)
                        self.comm_time = get_time()

                    return p
                else:
                    # Otherwise store received character in buffer.
                    p += ch


class KWP2000(object):
    ''' KWP2000 communication class
    '''

    def __init__(self):
        # turn debug mode off
        self.debug = False

        # Instantiate lower layer communication object.
        self.pic_conv = PICConv()

    def calc_chsum(self, p):
        chsum = 0
        for i in range(len(p) / 2):
            chsum += int(p[2 * i:2 * (i + 1)], 16)
        return '%(ch)02X' % {'ch': chsum & 255}

    def send_packet(self, p):
        ''' Assemble and send out KWP2000 packet.
        '''
        self.pic_conv.send_packet('S', p)

    def recv_packet(self):
        ''' Wait for KWP2000 packet from PIC module.
        Returns None if timeout expired.
        '''

        while True:
            p = self.pic_conv.recv_packet()
            # Return None if timeout expired.
            if p is None:
                return None

            #
            # Wait for either valid KWP packet or NAK response.
            # Discard other packets.
            #
            if p[0] == 'r':
                return p

            if p[0] == 'N':
                print "[%0.3f] NAK received: %s" % (get_run_time(), p)
                return p


class OBD(object):
    ''' OBD communication class.
    '''

    ST_COMM_INIT = "st_comm_init"
    ST_GET_PID = "st_get_pid"

    PKT_GET_PID = "01"
    PKT_GET_PID_RESP = "41"

    def __init__(self):
        # turn debug mode off
        self.debug = False

        # Instantiate lower layer communication object.
        self.kwp2000 = KWP2000()

        # Communication with ECU is not established yet.
        self.com_ecu_established = False

    def comm_init(self):
        ''' Establish communication with ECU.
        Return True if communication has been established, otherwise
        return False.
        '''
        # If communication channel is already established, return.
        if self.com_ecu_established:
            return True

        # Make sure receive buffer is empty
        pc = self.kwp2000.pic_conv
	while pc.ser.inWaiting() != 0:
            ch = pc.ser.read(pc.ser.inWaiting())

        #
        # To establish communication with ECU, send 'get pid 00' packet
        # to ECU. Low level part of that handshake is handled by PIC.
        # Let receive state machine process a response.
        #
        p = self.get_pid_value('00')

        # Timeout expired.
        if p is None:
            print "[%0.3f] Timeout when trying to initiate " \
                   "communication with ECU" % get_run_time()

            return False

        # Communication established, set appropriate flag and return.
        self.com_ecu_established = True
        return True

    def get_pid_value(self, pid_id):
        ''' Ask ECU for PID value. Return None if valid response has not
        been received, otherwise return PID value in form of byte array.
        '''
        self.kwp2000.send_packet(OBD.PKT_GET_PID + pid_id)

        # Wait for response.
        p = self.kwp2000.recv_packet()

        #
        # Timeout expired or invalid packet received. Communication needs
        # to be established again.
        #
        if p is None or not p.startswith('r' + OBD.PKT_GET_PID_RESP + pid_id):
            if p is None:
                print "[%0.3f] Timeout when waiting for ECU " \
                       "response" % get_run_time()
            else:
                print "[%0.3f] Invalid ECU response, awaiting %s, got %s." \
                       % (get_run_time(), 'r' + OBD.PKT_GET_PID_RESP + pid_id,
                          p[0:5])

            self.com_ecu_established = False
            return None

        pid_bytes = []
        # Get number of bytes representing PID
        num = ((len(p) - 5) / 2)

        for i in range(num):
            pid_bytes.append(int(p[5 + 2 * i: 5 + 2 * (i + 1)], 16))

        return pid_bytes


class MainLoop(threading.Thread):
    ''' MainLoop class
    '''

    # Initiates communication with PIC module.
    ST_COMM_INIT_PIC = "st_comm_init_pic"
    # Initiates communication with ECU.
    ST_COMM_INIT_ECU = "st_comm_init_ecu"
    # Get PID value from ECU.
    ST_GET_PIDS = "st_get_pids"
    # Get battery voltage.
    ST_GET_VBB = "st_get_vbb"

    # Dashboard indicators.
    DB_DESC_LBL_NAME = 'desc_lbl_name'
    DB_IND_LBL_NAME = 'ind_lbl_name'
    DB_IND_PB_NAME = 'ind_pb_name'

    def __init__(self):
        super(MainLoop, self).__init__()

        self.debug = False

        self.quit = False

        self.builder = gtk.Builder()
        self.builder.add_from_file("./obd_logger.glade")
        self._window = self.builder.get_object("mainwindow")
#        self._window.set_default_size(600, 450)
        self._window.set_default_size(450, 600)
        self._window.connect("destroy", self.quit_app)
        self.state = MainLoop.ST_COMM_INIT_PIC

        # Initialize data structure measuring battery voltage.
        self.battery_voltage = {'rdy':False, 'value':[0, 0]}

        #
        # Initialize dictionary of known OBD Parameters. Use Parameter ID
        # as a key.
        # TODO: Read the data from external source (e.g. XML file).
        #
        self.obd_pids = {}

        # 'Real' parameters - OBD parameters obtained from ECU.
        self.obd_pids['04'] = OBDParameter('04', 'Calculated Engine Load',
                                           'LOAD',
                                           1, 0.0, 100.0 / 255.0,
                                           0.0, 1.0 / 100.0,
                                           '%.0f', '%')
        self.obd_pids['05'] = OBDParameter('05', 'Engine coolant temperature',
                                           'WATER', 1,
                                           -40, 1,
                                           40.0, 1.0 / (40.0 + 110.0),
                                           '%d',
#                                           unichr(0x1d52) + 'C')
                                           'stC')
        self.obd_pids['0B'] = OBDParameter('0B',
                                           'Intake Manifold Absolute Pressure',
                                           'AIR', 1,
                                           0.0, 1.0, 0.0, 1.0 / 255.0,
                                           '%.0f', 'kPa')
        self.obd_pids['0C'] = OBDParameter('0C', 'Engine RPM',
                                           'RPM', 2,
                                           0.0, 0.25, 0.0, 1.0 / 5000.0,
                                           '%.0f', 'min-1')
        self.obd_pids['0D'] = OBDParameter('0D', 'Vehicle Speed',
                                           'SPEED',  1,
                                           0.0, 1.0, 0.0, 1.0 / 195.0,
                                           '%.0f', 'km/h')
        self.obd_pids['0F'] = OBDParameter('0F', 'Intake Air Temperature',
                                           'AIR', 1,
                                           -40, 1, 40.0, 1.0 / (40.0 + 50.0),
                                          '%d',
#                                         unichr(0x1d52) + 'C')
                                           'stC')
        self.obd_pids['10'] = OBDParameter('10', 'MAF Air Flow Rate',
                                           'MAF', 2,
                                           0.0, 0.01, 0.0, 1.0 / 150.0,
                                           '%.0f', 'g/s')
        self.obd_pids['11'] = OBDParameter('11', 'Throttle Position',
                                           'GAS', 1,
                                           0.0, 100.0 / 255.0,
                                           0.0, 1.0 / 100.0,
                                           '%.0f', '%')
        self.obd_pids['23'] = OBDParameter('23', 'Fuel Rail Pressure',
                                           'RAIL', 2,
                                           0.0, 0.01, 0.0, 1.0 / 120.0,
                                           '%.0f', 'MPa')
        self.obd_pids['2C'] = OBDParameter('2C', 'Commanded EGR',
                                           'EGR', 1,
                                           0.0, 100.0 / 255.0, 0.0, 0.01,
                                           '%.0f', '%')
        self.obd_pids['2D'] = OBDParameter('2D', 'EGR Error',
                                           'EGRe', 1,
                                           -128.0, 100.0 / 128.0, 100.0, 0.01,
                                           '%.0f', '%')
        self.obd_pids['33'] = OBDParameter('33', 'Barometric Pressure',
                                           'BAR', 1,
                                           0.0, 1.0, 0.0, 1.0 / 255.0,
                                           '%.0f', 'kPa')

        # Battery voltage - measured by PIC module on one of its analog inputs.
        self.obd_pids['Batt'] = OBDParameter('Batt', 'Battery Voltage',
                                             'BATT', 2,
                                             # protection diode - only HW v1
                                             #40.74,
                                             #(5.0 * (2.7 + 6.8) / 2.7) / 1024,
                                             #0.0,
                                             #1.0 / (5.0 * (2.7 + 6.8) / 2.7),
                                             # HW v2
                                             0.0,
                                             0.03562,
                                             0.0,
                                             1.0 / (0.03562 * 1024),
                                             '%.1f', 'V')

        #
        # 'Turbo' indicator. Calculated as Intake pressure/Barometric pressure
        # ratio.
        #
        self.obd_pids['Turbo'] = TurboIndicator()
        
        # Initialize list of indicators - max 7 on the screen.
        self.db_indicators = []
        # 1st line
        self.db_indicators.append({MainLoop.DB_DESC_LBL_NAME:'label14',
                                   MainLoop.DB_IND_LBL_NAME:'label30',
                                   MainLoop.DB_IND_PB_NAME:'progressbar1'})
        # 2nd line
        self.db_indicators.append({MainLoop.DB_DESC_LBL_NAME:'label15',
                                   MainLoop.DB_IND_LBL_NAME:'label31',
                                   MainLoop.DB_IND_PB_NAME:'progressbar2'})
        # 3d line
        self.db_indicators.append({MainLoop.DB_DESC_LBL_NAME:'label16',
                                   MainLoop.DB_IND_LBL_NAME:'label32',
                                   MainLoop.DB_IND_PB_NAME:'progressbar3'})
        # 4th line
        self.db_indicators.append({MainLoop.DB_DESC_LBL_NAME:'label17',
                                   MainLoop.DB_IND_LBL_NAME:'label33',
                                   MainLoop.DB_IND_PB_NAME:'progressbar4'})
        # 5th line
        self.db_indicators.append({MainLoop.DB_DESC_LBL_NAME:'label19',
                                   MainLoop.DB_IND_LBL_NAME:'label34',
                                   MainLoop.DB_IND_PB_NAME:'progressbar6'})
        # 6th line
        self.db_indicators.append({MainLoop.DB_DESC_LBL_NAME:'label18',
                                   MainLoop.DB_IND_LBL_NAME:'label35',
                                   MainLoop.DB_IND_PB_NAME:'progressbar5'})
        # 7th line
        self.db_indicators.append({MainLoop.DB_DESC_LBL_NAME:'label36',
                                   MainLoop.DB_IND_LBL_NAME:'label_voltage',
                                   MainLoop.DB_IND_PB_NAME:'progressbar_voltage'})

        #
        # Initialize dictionary containing OBD PIDs displayed
        # along with label and progressbar name they use as indicators.
        #
        self.pid2dashboard_map = {}
        # RPM
        self.pid2dashboard_map['0C'] = self.db_indicators[0]
        # Turbo
        self.pid2dashboard_map['Turbo'] = self.db_indicators[1]
        # MAF
        self.pid2dashboard_map['10'] = self.db_indicators[2]
        # Water Temperature
        self.pid2dashboard_map['05'] = self.db_indicators[3]
        # RAIL
        self.pid2dashboard_map['23'] = self.db_indicators[4]
        # EGR
        self.pid2dashboard_map['2C'] = self.db_indicators[5]
        # Battery Voltage
        self.pid2dashboard_map['Batt'] = self.db_indicators[6]

        # Populate parameters labels on dashboard.
        for k in self.pid2dashboard_map.keys():
            l = self.builder.get_object(
                self.pid2dashboard_map[k][MainLoop.DB_DESC_LBL_NAME])
            l.set_text(self.obd_pids[k].label)

        # Initialize OBD communication object.
        self.obd_comm = OBD()

        #
        # Create list of PID priority groups
        #
        self.pid_speed_groups = []
        # Fast - Intake Manifold Absolute Pressure, EGR
        self.pid_speed_groups.append({'name':'Fast', 'curr_index':-1,
                                      'pid_list':['0B', '2C']})
        # Medium - RPM, MAF, Rail Pressure
        self.pid_speed_groups.append({'name':'Medium', 'curr_index':-1,
                                      'pid_list':['0C', '10', '23']})
        # Slow - Water & Air Temperature, Vehicle Speed, Barometric Pressure,
        #        Battery Voltage
        self.pid_speed_groups.append({'name':'Slow', 'curr_index':-1,
                                      'pid_list':['05', '0D', '0F', '33',
                                                  'Batt']})
        # List of all PIDs to be processed. Start with the highest priority.
        self.pid_list=[]
        for i in range(len(self.pid_speed_groups)):
            self.pid_list += self.pid_speed_groups[i]['pid_list']

        #
        # Add calculated 'Turbo' parameter to the pid list, so that it gets
        # logged.
        #
        self.pid_list.append('Turbo')

#        print 'PID list:', self.pid_list

        # Log filename is in format of 'obd-YYYYMMDD-HHMMSS.txt'
        self.log_filename = time.strftime('obd-%Y%m%d-%H%M%S.txt')
        # Open log file for append.
        self.log_file = open('/home/dambi/obd-log/' + self.log_filename, 'a')
        # Initialize log timer and create header.
        self.log_start_time = get_time()
        par_header = 'Time[s]'
        for pid in self.pid_list:
            obd_par = self.obd_pids[pid]
            par_header += '\t%s[%s]' % (obd_par.label, obd_par.units)

        self.log_file.write('%s\n' % par_header)

        signal_map = {
            "on_quitbutton_clicked": self.quit_app}

        missing_handlers = self.builder.connect_signals(signal_map)
        if missing_handlers is not None:
            print "Internal issue - Missing Signal Handlers!"

#        font_type = "sans 24"
#        font_type = "monospace bold 24"
        font_type = "normal bold 16"

        # Initialize parameter labels. Change font size.
        for lbl_num in ["14", "15", "16", "17", "18", "19",
                        "30", "31", "32", "33", "34", "35", "36",
                        "_voltage"]:
            lbl = self.builder.get_object("label" + lbl_num)
            lbl.modify_font(pango.FontDescription(font_type))

        #
        # Initialize indicator on status bar reflecting state of communication
        # with ECU.
        #
        sts_comm_img = self.builder.get_object("sts_communication")
        sts_comm_img.set_from_stock('gtk-no', -1)


    def update_label(self, lbl_name, lbl_text):
        ''' Updates specified label with new text.'''
        lbl = self.builder.get_object(lbl_name)
        lbl.set_text(lbl_text)
        return False


    def update_progressbar(self, pb_name, pb_fraction):
        ''' Updates specified progressbar with new fraction.'''
        # Fraction has to fit <0.0. 1.0> interval.
        if pb_fraction < 0.0:
            pb_fraction = 0.0
        elif pb_fraction > 1.0:
            pb_fraction = 1.0

        pb = self.builder.get_object(pb_name)
        pb.set_fraction(pb_fraction)
        return False


    def update_stock_image(self, img_name, img_type, img_size):
        ''' Updates specified progressbar with new stock image.'''
        img = self.builder.get_object(img_name)
        img.set_from_stock(img_type, img_size)
        return False


    def update_indicators(self, curr_pid):
        ''' Updates label and progressbar indicators with new PID value.'''

        # If PID has no indicator associated, there is nothing to update.
        if not self.pid2dashboard_map.has_key(curr_pid):
            return

        #
        # Check if new value is different from previous one.
        # If both are the same, there is no need to update
        # indicators.
        #
        curr_par = self.obd_pids[curr_pid]
        if not curr_par.value_changed():
            return

        # Calculate updated values for indicators.
        text_value = curr_par.phys2text()
        fraction = curr_par.phys2pb_fraction()

        #
        # Refresh appropriate indicators.
        #
        lbl_name = self.pid2dashboard_map[curr_pid][MainLoop.DB_IND_LBL_NAME]
        pb_name = self.pid2dashboard_map[curr_pid][MainLoop.DB_IND_PB_NAME]

        # Text
        gobject.idle_add(self.update_label, lbl_name, text_value)

        # Progressbar
        gobject.idle_add(self.update_progressbar, pb_name, fraction)


    def init_pid_state_machine(self):
        ''' Initializes PID state machine.
        '''
        for i in range(len(self.pid_speed_groups)):
            self.pid_speed_groups[i]['curr_index'] = -1


    def get_next_pid_to_process(self):
        '''
        '''
        #
        # Move to the next PID to be queried. Start with the first group.
        # If all PIDs has been processed there, continue with next group, etc.
        #
        for i in range(len(self.pid_speed_groups)):
            self.pid_speed_groups[i]['curr_index'] += 1
            curr_index = self.pid_speed_groups[i]['curr_index']
            max_index = len(self.pid_speed_groups[i]['pid_list']) - 1
            if curr_index <= max_index:
                if self.debug:
                    print 'GRP=%d, PID=%s' % \
                           (i,
                            self.pid_speed_groups[i]['pid_list'][curr_index])

                return self.pid_speed_groups[i]['pid_list'][curr_index]
            else:
                self.pid_speed_groups[i]['curr_index'] = -1

        #
        # If we got here, it means that PID from last group is to be served
        # now, though all PIDs in the last group were already processed.
        # In such case, go with the first PID in the last group.
        #
        last_grp = len(self.pid_speed_groups) - 1
        self.pid_speed_groups[last_grp]['curr_index'] += 1
        curr_index = self.pid_speed_groups[last_grp]['curr_index']

        if self.debug:
            print 'GRP=%d, PID=%s' % \
                   (i, self.pid_speed_groups[i]['pid_list'][curr_index])

        return self.pid_speed_groups[last_grp]['pid_list'][curr_index]


    def pid_group_processed(self, grp_num):
        ''' Returns True if all PIDs in specified group have been served.
        Otherwise returns False.
        '''

        return (self.pid_speed_groups[grp_num]['curr_index'] == -1)

         
    def run(self):
        ''' Thread body.'''
        while not self.quit:
            tstart = time.time()
            t1 = -1.0
            t2 = -1.0
            t3 = -1.0
            t4 = -1.0

            pc = self.obd_comm.kwp2000.pic_conv

            t1 = time.time() - tstart
            t2 = time.time() - tstart

            # Establish communication with PIC module
            if self.state == MainLoop.ST_COMM_INIT_PIC:
                # Wait until communication with PIC module is established
                if not self.obd_comm.kwp2000.pic_conv.comm_init():
                    # Sleep for a while and retry.
                    time.sleep(2.0)
                    continue

                self.state = MainLoop.ST_COMM_INIT_ECU

            # Establish communication with ECU
            elif self.state == MainLoop.ST_COMM_INIT_ECU:
                # Wait until communication with ECU is established 
                if not self.obd_comm.comm_init():
                    # Sleep for a while and retry.
                    time.sleep(5.0)
                    continue

                print "Communication with ECU established"

                # Let user know via indicator on status bar.
                self.update_stock_image("sts_communication", 'gtk-yes', -1)

                self.init_pid_state_machine()

                self.current_pid_id = self.get_next_pid_to_process()

                #
                # Handle battery voltage separately, since it's measured
                # directly by PIC, not read from ECU.
                #
                if self.current_pid_id == 'Batt':
                    self.state = MainLoop.ST_GET_VBB
                else:
                    self.state = MainLoop.ST_GET_PIDS

            elif self.state == MainLoop.ST_GET_PIDS or \
                 self.state == MainLoop.ST_GET_VBB:

                # Assume that indicators are not to be refreshed.
                refresh_indicators = False

                #
                # Handle battery voltage in slightly different way, since
                # it's directly measured by PIC, not read from ECU.
                #
                if self.state == MainLoop.ST_GET_VBB:
                    # HW v1 - AD channel 6
                    #pc.send_packet('A', '06')
                    # HW v2 - AD channel 8
                    pc.send_packet('A', '08')

                    # Wait for response from PIC module.
                    p = pc.recv_packet()
                    if p == None:
                        print "[%0.3f] Timeout when waiting for " \
                           "packet from PIC module" % get_run_time()

                        #
                        # Connection with PIC module lost, wait for connection
                        # to be established again.
                        #
                        self.state = MainLoop.ST_COMM_INIT_PIC
                    elif p[0] != 'a':
                        print "Invalid PIC response, awaiting 'a', received" \
                           "%s" % p

                        #
                        # Invalid response from PIC module received,
                        # re-establishe connection.
                        #
                        self.state = MainLoop.ST_COMM_INIT_PIC
                    else:
                        # Valid response received, process it.
                        self.battery_voltage['value'] = [int(p[1:3], 16),
                                                         int(p[3:5], 16)]
                        # Adjust currently processed PID.
                        curr_pid = 'Batt'
                        curr_par = self.obd_pids[curr_pid]
                        # Calculate new parameter value.
                        curr_par.raw2phys(self.battery_voltage['value'])

                        refresh_indicators = True
                else:
                    # Ask ECU for PID value
                    pidv = self.obd_comm.get_pid_value(self.current_pid_id)

                    #
                    # If communication with ECU is broken,
                    # try to establish it again.
                    #
                    if pidv == None:
                        # Let user know via indicator on status bar.
                        self.update_stock_image('sts_communication',
                                                'gtk-no', -1)

                        #
                        # Sleep for a while, then retry to establish
                        # communication again.
                        #
                        time.sleep(5.0)
                        self.state = MainLoop.ST_COMM_INIT_ECU
                    else:
                        # Currently processed PID.
                        curr_pid = self.current_pid_id
                        curr_par = self.obd_pids[curr_pid]
                        # Calculate new parameter value.
                        curr_par.raw2phys(pidv)

                        refresh_indicators = True

                if refresh_indicators:
                    self.update_indicators(curr_pid)

                    # Recalculate and display TurboIndicator if applicable.
                    if self.obd_pids['Turbo'].recalculate(curr_pid):
                        self.obd_pids['Turbo'].raw2phys(self.obd_pids)
                        self.update_indicators('Turbo')

                    #
                    # Move to the next PID.
                    #
                    self.current_pid_id = self.get_next_pid_to_process()

                    #
                    # Handle battery voltage separately, since it's measured
                    # directly by PIC, not read from ECU.
                    #
                    if self.current_pid_id == 'Batt':
                        self.state = MainLoop.ST_GET_VBB
                    else:
                        self.state = MainLoop.ST_GET_PIDS

                    #
                    # Log data into log file every time all PIDs in the first
                    # group get processed.
                    #
                    if self.pid_group_processed(0):
                        # Log time first.
                        par_str='%0.3f' % (time.time() - self.log_start_time)

                        #
                        # Now enumerate through the list of all PIDs measured
                        # and log value of each of them.
                        #
                        for pid in self.pid_list:
                            par_str += '\t%s' % self.obd_pids[pid].phys2log()

                        self.log_file.write('%s\n' % par_str)
                        self.log_file.flush()

            t4 = time.time() - tstart
#            print 't1=%.3f, t2=%.3f, t3=%.3f, t4=%.3f' % (t1, t2, t3, t4)

        # Thread has been asked to end its job, so close log file.
        self.log_file.write('==============================================\n')
        self.log_file.write('Final time: %0.3f\n' %
                            (time.time() - self.log_start_time))
        self.log_file.write('==============================================\n')
        self.log_file.flush()
        self.log_file.close()


    def enter(self):
        '''enter method for the progress screen'''

        toplevel = self.builder.get_object("mainwindow")
        toplevel.show_all()


    def quit_app(self, widget):
        # clean up before exit
#        gobject.source_remove(self.comm_cb_id)

        # quit the application
        gtk.main_quit()

# Record start time of the applicaton
app_start_time = get_time()

# Initialize threading
gobject.threads_init()

# Create communication thread
main_loop = MainLoop()
main_loop.enter()

# Start communication thread
main_loop.start()

#
# Pass control over to Gtk+. Handle Ctrl^C
#
try:
    gtk.main()
except KeyboardInterrupt:
    print "Application interrupted, exiting."

# Let communication thread know it's time to terminate its job.
main_loop.quit = True

# Try to 'join' communication thread. Though wait no longer than 5 seconds.
main_loop.join(5.0)

# If the attempt to join the thread failed, inform user and exit with error.
if main_loop.isAlive():
    print 'Could not join the communication thread.'
    sys.exit(1)

sys.exit(0)

