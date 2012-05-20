#!/usr/bin/python

import gobject
import gtk
import pango
import pygtk
pygtk.require('2.0')

import serial
import time

def get_time():
    return time.time()

def time_expired(captured_time, expiration_time):
    if (captured_time + expiration_time) < get_time():
        return True
    else:
        return False

class PICConv(object):
    def __init__(self, kwp2000_recv_cb):
        # open serial port for communication with PIC module
#        self.ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.5)
#        self.ser = serial.Serial('/dev/ttyUSB0', 115200)
        self.ser = serial.Serial('/dev/ttyS0', 115200, timeout=0.5)

        # initialize state machine
#        self.recv_state = PICConv.ST_WAIT4SOF

        # initialize receive buffer
        self.recv_buf = ''

        #
        # Callback function called when packet related to KWP2000
        # protocol is received.
        #
        self.kwp2000_recv_cb = kwp2000_recv_cb

        # Initialize data structure measuring battery voltage.
        self.battery_voltage = {'rdy':False, 'value':0.0,
                                # protection diode
                                'c_add':0.7,
                                # Voltage divider created from 2k7 and 6k8
                                # resistors. Measuring voltage on 2k7 one.
                                'c_mul':(5 * (2.7 + 6.8) / 2.7) / 1024,
                                'max':5 * (2.7 + 6.8) / 2.7}

    def send_packet(self, cmd, data):
        pic_packet = '<' + cmd + data + '>'
        self.ser.write(pic_packet)

    def recv_state_machine(self):
        # If there are no characters received, exit.
        if self.ser.inWaiting() == 0:
            return

        while self.ser.inWaiting() != 0:
#            t1 = t2 = t3 = -1.0
#            tstart = time.time()

            rcv_chars = self.ser.read(self.ser.inWaiting())
#            t1 = time.time() - tstart
#            print 'pic: recv_buf=%s' % rcv_chars

            for ch in rcv_chars:
                if ch == '<':
                    # SOF character received, restart receive process.
                    self.recv_buf = ''
                elif ch == '>':
                    #
                    # EOF character received. If it's KWP2000 related character,
                    # let KWP2000 layer process received packet. Otherwise
                    # process received packet here.
                    #
                    if self.recv_buf[0] == 'A' or self.recv_buf[0] == 'r':
                        self.kwp2000_recv_cb(self.recv_buf)
#                        t2 = time.time() - tstart
                    elif self.recv_buf[0] == 'a':
                        v = int(self.recv_buf[1:5], 16)
                        self.battery_voltage['value'] = v * self.battery_voltage['c_mul'] + \
                                                        self.battery_voltage['c_add']
                        self.battery_voltage['rdy'] = 'True'
#!HA!                    print "Vbb: %d" % self.battery_voltage['value']

                else:
                    # Otherwise store received character in buffer.
                    self.recv_buf += ch
#                t3 = time.time() - tstart

#            print 'pic: t1=%.3f, t2=%.3f, t3=%.3f' % (t1, t2, t3)

class KWP2000(object):
    # kwp2000 communication times - values are in seconds.

    #
    # P3 - time between end of ECU response and start of new tester request,
    # or time between end of tester request and start of new tester request
    # if ECU fails to respond. P3 shall be measured from the last byte in the
    # latest response message from any ECU responding.
    #
    P3_MIN = 0.055
    P3_MAX = 5

    # states of send state machine
    ST_SEND_IDLE = "st_send_idle"
    ST_SEND_WAIT4P3 = "st_send_wait4p3"

    # states of receive state machine
    ST_RECV_IDLE = "st_recv_idle"
    ST_RECV_WAIT4ACK = "st_recv_wait4ack"
    ST_RECV_HDR = "st_recv_hdr"
    ST_RECV_DATA = "st_recv_data"

    def __init__(self, obd_recv_cb):
        # turn on debug mode
        self.debug = True
        self.debug = False

        self.comm_initialized = False
        self.pic_conv = PICConv(self.recv_state_machine)
        # Callback function called when whole packet is received.
        self.obd_recv_cb = obd_recv_cb

        # Records time of last received packet from ECU.
        self.time_ecu_recv_packet = 0

        # Initialize receive and send state machines.
        self.change_recv_state(KWP2000.ST_RECV_IDLE)
        self.change_send_state(KWP2000.ST_SEND_IDLE)

        # receive buffer
        self.recv_buf = ""

    def change_recv_state(self, new_state):
        if self.debug:
            print "kwp2000: R-> %s" % new_state
        self.recv_state = new_state

    def change_send_state(self, new_state):
        if self.debug:
            print "kwp2000: S-> %s" % new_state
        self.send_state = new_state

    def calc_chsum(self, p):
        chsum = 0
        for i in range(len(p) / 2):
            chsum += int(p[2 * i:2 * (i + 1)], 16)
        return '%(ch)02X' % {'ch': chsum & 255}

    def send_packet(self, p, wup = False):
        # Append checksum and send the packet
        p1 = p + self.calc_chsum(p)

        #
        # There must be a period of silence before tester can send a packet
        # after last packet was received from ECU.
        #
        # If that period of silence hasn't expired yet, postpone sending
        # the packet.
        #
#        if False:
        if not time_expired(self.time_ecu_recv_packet, KWP2000.P3_MIN):
            if self.debug:
                print "kwp2000: rcv=%f, p3min=%f, time=%f" % \
                       (self.time_ecu_recv_packet, KWP2000.P3_MIN,
                        get_time())
            #
            # Wait for a while to meet KWP2000 timing specification,
            # then send the packet.
            #
            self.send_buff = [p1, wup]
            self.change_send_state(KWP2000.ST_SEND_WAIT4P3)
        else:
            #
            # If it's an initialization packet, prepend it
            # with 'Wake up Pattern'.
            #
            if wup:
                self.pic_conv.send_packet('W', p1)
            else:
                self.pic_conv.send_packet('S', p1)

            # Wait for ACK.
            self.change_recv_state(KWP2000.ST_RECV_WAIT4ACK)

    def send_state_machine(self):
        if self.send_state == KWP2000.ST_SEND_IDLE:
            return
        elif self.send_state == KWP2000.ST_SEND_WAIT4P3:
            if time_expired(self.time_ecu_recv_packet, KWP2000.P3_MIN):
                #
                # If it's an initialization packet,
                # prepend it with 'Wake up Pattern'.
                #
                if self.send_buff[1]:
                    self.pic_conv.send_packet('W', self.send_buff[0])
                else:
                    self.pic_conv.send_packet('S', self.send_buff[0])

                # Switch send state machine to IDLE state.
                self.change_send_state(KWP2000.ST_SEND_IDLE)

                # Wait for ACK.
                self.change_recv_state(KWP2000.ST_RECV_WAIT4ACK)

    def recv_state_machine(self, data):
        if self.recv_state == KWP2000.ST_RECV_WAIT4ACK:
            if data[0] == 'A':
                # ACK received, start receiving data.
                self.recv_buf = ""
                self.change_recv_state(KWP2000.ST_RECV_HDR)

                return
        elif data[0] != 'r':
            return

        # Extract data portion and convert from hex ASCII form into binary one.
        d = int(data[1:3], 16)

        # Store data in a buffer (in ASCII form).
        self.recv_buf += data[1:3]

        # print received byte for debug purposes
        if self.debug:
            print "kwp2000: recv_buf=" + self.recv_buf

        # Process received data
        if self.recv_state == KWP2000.ST_RECV_IDLE:
            # no-op
            return
        elif self.recv_state == KWP2000.ST_RECV_HDR:
            # Received KWP2000 header byte. 6 lower bits contain length
            # of data to be received.
            self.data_ln = d & 0x3f
            # account for overhead (ECU address, tester address, checksum)
            self.data_ln += 3

            if self.debug:
                print "kwp2000: ln=%d" % self.data_ln

            self.change_recv_state(KWP2000.ST_RECV_DATA)
        elif self.recv_state == KWP2000.ST_RECV_DATA:
            self.data_ln -= 1
            if self.data_ln <= 0:
                #
                # Whole packet received. Let OBD layer process it.
                # TODO: Verify checksum of received packet. Inform upper layer
                #       if checksum is not correct.
                #

                # Record time when the packet was received from ECU.
                self.time_ecu_recv_packet = get_time()

                if self.debug:
                    print "kwp2000: time_ecu_recv_packet=%f" % \
                           self.time_ecu_recv_packet

                self.obd_recv_cb(self.recv_buf)
                self.recv_buf = ""

                self.change_recv_state(KWP2000.ST_RECV_IDLE)

class OBD(object):
    ST_IDLE = "st_idle"
    ST_COMM_INIT = "st_comm_init"
    ST_GET_PID = "st_get_pid"

    PKT_START_COMM = "C133F181"
    PKT_START_COMM_RESP = "83F110C1"
    PKT_STOP_COMM = "C133F182"
    PKT_STOP_COMM_RESP = "81F110C2"
    PKT_GET_PID = "C233F101"
    PKT_GET_PID_RESP = "83F11041"

    def __init__(self):
        # turn on debug mode
        self.debug = True
        self.debug = False

        self.kwp2000 = KWP2000(self.recv_state_machine)

        self.communication_established = False

        # Incorrect response received.
        self.incorrect_response = False

        # Put state machine into IDLE state.
        self.change_recv_state(OBD.ST_IDLE)

        #
        # Received PID value - it is in form of list of 'bytes'
        # (numbers in 0..255 range).
        #
        self.pid_value = []

        # New PID value received.
        self.pid_value_ready = False

    def change_recv_state(self, new_state):
        if self.debug:
            print "obd: -> %s" % new_state
        self.recv_state = new_state

    def main_handler(self):
        return True


    def comm_start(self):
        # If communication channel is already established, return.
        if self.communication_established:
            return

        #
        # Send StartCommunication packet to ECU (let receive state machine
        # process a response).
        #
        self.change_recv_state(OBD.ST_COMM_INIT)
        self.kwp2000.send_packet(OBD.PKT_START_COMM, True)


    def get_pid_value(self, pid_id):
        self.change_recv_state(OBD.ST_GET_PID)
        self.kwp2000.send_packet(OBD.PKT_GET_PID + pid_id)


    def recv_state_machine(self, data):
        if self.recv_state == OBD.ST_IDLE:
            return
        elif self.recv_state == OBD.ST_COMM_INIT:
            #
            # Verify response packet. If incorrect response has been 
            # received, set the flag. Let upper layer deal with that.
            #
            # TODO: Make verification step more robust and flexible.
            #
            if data.startswith(OBD.PKT_START_COMM_RESP):
                self.communication_established = True
            else:
                self.incorrect_response = True

            # Switch to IDLE state.
            self.change_recv_state(OBD.ST_IDLE)
        elif self.recv_state == OBD.ST_GET_PID:
            #
            # Verify response packet. Ignore first header byte, since it
            # contains length of data portion, thus may vary.
            # If incorrect response has been received, set the flag. Let upper
            # layer deal with that.
            #
            # TODO: Make verification step more robust and flexible.
            #
            if data[2:].startswith(OBD.PKT_GET_PID_RESP[2:]):
                #
                # If previous PID value was not processed yet,
                # drop the new one.
                #
                if not self.pid_value_ready:
                    # Get number of bytes representing PID
                    num = (int(data[0:2], 16) & 0x3f) - 2
                    if self.debug:
                        print "obd: PID data length=%d" % num

                    # populate PID value
                    self.pid_value = []

                    for i in range(num):
                        self.pid_value.append(int(data[10 + 2 * i: \
                                                       10 + 2 * (i + 1)], 16))
                    if self.debug:
                        print "obd: PID data: %s" % str(self.pid_value)

                    # Indicate that new PID value is available.
                    self.pid_value_ready = True
            else:
                self.incorrect_response = True

            # Switch to IDLE state.
            self.change_recv_state(OBD.ST_IDLE)


class MainScreen(object):
    ST_COMM_INIT_SEND = "st_comm_init_send"
    ST_COMM_INIT_RECV = "st_comm_init_recv"
    ST_GET_PIDS = "st_get_pids"
    ST_GET_VBB = "st_get_vbb"

    # Times are in seconds.
    TIME_RETRY_START_COMM = 1

    def __init__(self):
        self.builder = gtk.Builder()
        self.builder.add_from_file("./obd_logger.glade")
        self._window = self.builder.get_object("mainwindow")
#        self._window.set_default_size(600, 450)
        self._window.set_default_size(450, 600)
        self._window.connect("destroy", self.quit)
        self.state = MainScreen.ST_COMM_INIT_SEND

        # EGR - desired value, error
        self.egr_w = 0
        self.egr_e = 0

        #
        # List of 'fast' OBD PIDs
        #  0C - RPM
        #  10 - MAF
        #
        self.fast_pids = ['0C', '10']
        self.fast_pid_index = 0

        #
        # List of 'slow' OBD PIDs
        #  23 - Rail pressure
        #  05 - Coolant temperature
        #  0F - Intake air temperature
        #  2C - Commanded EGR
        #  2D - EGR error
        #
        self.slow_pids = ['23', '05', '0F', '2C', '2D']
#        self.slow_pids = ['23', '05', '0F', '2C']
#        self.slow_pids = ['23', '05']
        self.slow_pid_index = 0

        # Values from previous measurement cycle.
        self.previous_fraction = {'0C':0.0, '10':0.0, '23':0.0, '05':0.0,
                                  '0F':0.0, '2C':0.0, '2D':0.0, 'Batt':0.0}
        self.previous_text =  {'0C':'', '10':'', '23':'', '05':'',
                               '0F':'', '2C':'', '2D':'', 'Batt':''}

        #
        # Physical units of measured values - they are later initialized
        # from glade file.
        #
        self.units = {'0C':'', '10':'', '23':'', '05':'',
                      '0F':'', '2C':'', '2D':'', 'Batt':''}

        # Log filename is in format of 'obd-YYYYMMDD-HHMMSS.txt'
        self.log_filename = time.strftime('obd-%Y%m%d-%H%M%S.txt')
        # Open log file for append.
        self.log_file = open('/home/dambi/obd-log/' + self.log_filename, 'a')
        self.log_start_time = 0.0

        signal_map = {
            "on_quitbutton_clicked": self.quit}

        missing_handlers = self.builder.connect_signals(signal_map)
        if missing_handlers is not None:
            print "Internal issue - Missing Signal Handlers!"

#        font_type = "sans 24"
#        font_type = "monospace bold 24"
        font_type = "normal bold 16"

        # change font size for indicators
        lb14 = self.builder.get_object("label14")
	lb14.modify_font(pango.FontDescription(font_type))
        lb15 = self.builder.get_object("label15")
	lb15.modify_font(pango.FontDescription(font_type))
        lb16 = self.builder.get_object("label16")
	lb16.modify_font(pango.FontDescription(font_type))
        lb17 = self.builder.get_object("label17")
	lb17.modify_font(pango.FontDescription(font_type))
        lb18 = self.builder.get_object("label18")
	lb18.modify_font(pango.FontDescription(font_type))
        lb19 = self.builder.get_object("label19")
	lb19.modify_font(pango.FontDescription(font_type))

        # RPM - value
        lb30 = self.builder.get_object("label30")
	lb30.modify_font(pango.FontDescription(font_type))
        self.units['0C'] = lb30.get_text()

        # MAF - value
        lb31 = self.builder.get_object("label31")
	lb31.modify_font(pango.FontDescription(font_type))
        self.units['10'] = lb31.get_text()

        # RAIL - value
        lb32 = self.builder.get_object("label32")
	lb32.modify_font(pango.FontDescription(font_type))
        self.units['23'] = lb32.get_text()

        # WATER - value
        lb33 = self.builder.get_object("label33")
	lb33.modify_font(pango.FontDescription(font_type))
        self.units['05'] = lb33.get_text()

        # AIR - value
        lb34 = self.builder.get_object("label34")
	lb34.modify_font(pango.FontDescription(font_type))
        self.units['0F'] = lb34.get_text()

        # EGR - value
        lb35 = self.builder.get_object("label35")
	lb35.modify_font(pango.FontDescription(font_type))
        self.units['2C'] = lb35.get_text()
        self.units['2D'] = lb35.get_text()

        # Battery - name
        lb36 = self.builder.get_object("label36")
	lb36.modify_font(pango.FontDescription(font_type))

        # Battery - value
        lb37 = self.builder.get_object("label_voltage")
	lb37.modify_font(pango.FontDescription(font_type))
        self.units['Batt'] = lb37.get_text()

        #
        # Initialize indicator on status bar reflecting state of communication
        # with ECU.
        #
        sts_comm_img = self.builder.get_object("sts_communication")
        sts_comm_img.set_from_stock('gtk-no', -1)

        # Initialize OBD communication object.
        self.obd_comm = OBD()

        #
        # Register callback function which will be called every 10 ms
        # and will handle communication with ECU.
        #
        self.comm_cb_id = gobject.timeout_add(20, self.control_loop)

        # Measure battery voltage.
#        pc = self.obd_comm.kwp2000.pic_conv
#        pc.send_packet('A', '06')
#        self.state == MainScreen.ST_GET_VBB


    def control_loop(self):
        tstart = time.time()
        t1 = -1.0
        t2 = -1.0
        t3 = -1.0
        t4 = -1.0

        pc = self.obd_comm.kwp2000.pic_conv

        # Handle received packets.
        pc.recv_state_machine()

        t1 = time.time() - tstart

        # Serve kwp2000 send state machine.
        self.obd_comm.kwp2000.send_state_machine()

        t2 = time.time() - tstart

        # Establish communication channel
        if self.state == MainScreen.ST_COMM_INIT_SEND:
            # Initialize timeout timer.
            self.time_comm_start = get_time()

            self.obd_comm.comm_start()
            self.state = MainScreen.ST_COMM_INIT_RECV

        elif self.state == MainScreen.ST_COMM_INIT_RECV:
            #
            # Retry to establish communication channel if no response
            # has been received during specified time frame.
            #
            if time_expired(self.time_comm_start,
                            MainScreen.TIME_RETRY_START_COMM):
                self.time_comm_start = get_time()
                self.obd_comm.comm_start()        

            # Communication established, start with collecting PID values.
            if self.obd_comm.communication_established:
                # Let user know via indicator on status bar.
                sts_comm_img = self.builder.get_object("sts_communication")
                sts_comm_img.set_from_stock('gtk-yes', -1)

                self.fast_pid_index = 0
                self.slow_pid_index = -1

                self.current_pid_id = self.fast_pids[0]
                self.obd_comm.get_pid_value(self.current_pid_id)

                # Initialize timeout timer.
                self.time_packet_sent = get_time()

                self.state = MainScreen.ST_GET_PIDS
        elif self.state == MainScreen.ST_GET_PIDS:
            # If communication with ECU is broken, try to establish it again.
            if time_expired(self.time_packet_sent,
                            4 * MainScreen.TIME_RETRY_START_COMM):
                self.obd_comm.communication_established = False

                # Let user know via indicator on status bar.
                sts_comm_img = self.builder.get_object("sts_communication")
                sts_comm_img.set_from_stock('gtk-no', -1)

                self.time_comm_start = get_time()
                self.obd_comm.comm_start()

                self.state = MainScreen.ST_COMM_INIT_RECV
            elif self.obd_comm.pid_value_ready:
                # PID received
                raw_value = self.obd_comm.pid_value

                #
                # Refresh appropriate indicator.
                #
                if self.current_pid_id == '0C':
                    # RPM
                    physical_value = (raw_value[0] * 256 + raw_value[1]) / 4.0

                    # consider 5000 to be max we want to process
                    fraction = physical_value / 5000.0
                    if fraction > 1.0:
                        fraction = 1.0

                    text_value = "%.0f" % physical_value

                    pb_name = "progressbar1"
                    lbl_name = "label30"
                elif self.current_pid_id == '10':
                    # MAF
                    physical_value = (raw_value[0] * 256 + raw_value[1]) / 100.0

                    # consider 150.0 to be max we want to process
                    fraction = physical_value / 150.0
                    if fraction > 1.0:
                        fraction = 1.0

                    text_value = "%.0f" % physical_value

                    pb_name = "progressbar2"
                    lbl_name = "label31"
                elif self.current_pid_id == '05':
                    # Engine Coolant Temperature
                    physical_value = raw_value[0] - 40

                    # consider range <-40;110> to be max we want to process
                    fraction = (physical_value + 40.0) / (40.0 + 110.0)
                    if fraction > 1.0:
                        fraction = 1.0

                    text_value = "%d" % physical_value

                    pb_name = "progressbar4"
                    lbl_name = "label33"
                elif self.current_pid_id == '0F':
                    # Intake Air Temperature
                    physical_value = raw_value[0] - 40

                    # consider range <-40;50> to be max we want to process
                    fraction = (physical_value + 40.0) / (40.0 + 50.0)
                    if fraction > 1.0:
                        fraction = 1.0

                    text_value = "%d" % physical_value

                    pb_name = "progressbar6"
                    lbl_name = "label34"
                elif self.current_pid_id == '23':
                    # Fuel Rail Pressure
                    physical_value = (raw_value[0] * 256 + raw_value[1]) / 100.0

                    # consider 120.0 to be max we want to process
                    fraction = physical_value / 120.0
                    if fraction > 1.0:
                        fraction = 1.0

                    text_value = "%.1f" % physical_value
                    text_value = text_value.replace('.', ',')

                    pb_name = "progressbar3"
                    lbl_name = "label32"
                elif self.current_pid_id == '2C' or \
                    self.current_pid_id == '2D':

                    if self.current_pid_id == '2C':
                        # Commanded EGR
                        self.egr_w = (raw_value[0] * 100) / 255
                    else:
                        # EGR error
                        self.egr_e = ((raw_value[0] - 128) * 100) / 128

                    # consider 100 to be max we want to process
                    fraction = self.egr_w / 100.0
                    if fraction > 1.0:
                        fraction = 1.0

                    if self.egr_e == 0:
                        text_value = "%d" % self.egr_w
                    elif self.egr_e > 0:
                        text_value = "%d+%d" % (self.egr_w, self.egr_e)
                    else:
                        text_value = "%d%d" % (self.egr_w, self.egr_e)

                    text_value = text_value.replace('.', ',')

                    pb_name = "progressbar5"
                    lbl_name = "label35"

                # Update indicators - but only if they changed.
#                if True:
                if self.previous_fraction[self.current_pid_id] != fraction:
                    self.previous_fraction[self.current_pid_id] = fraction
                    pb = self.builder.get_object(pb_name)
                    pb.set_fraction(fraction)

#                if True:
                if self.previous_text[self.current_pid_id] != text_value:
                    self.previous_text[self.current_pid_id] = text_value
                    lbl = self.builder.get_object(lbl_name)
                    lbl.set_text(text_value + ' ' +
                                 self.units[self.current_pid_id])

                # Indicate that PID was processed.
                self.obd_comm.pid_value_ready = False

                #
                # Move to the next fast PID. If all fast PIDs have been served,
                # handle slow PIDs. If all slow PIDs have been served, measure
                # battery voltage.
                #
                self.fast_pid_index += 1
                i = self.fast_pid_index

                if i < len(self.fast_pids):
                    self.current_pid_id = self.fast_pids[i]
                    self.obd_comm.get_pid_value(self.current_pid_id)

                    # Initialize timeout timer.
                    self.time_packet_sent = get_time()
                else:
                    t3 = time.time() - tstart
                    # All fast PIDs read. Log data into log file
                    if self.log_start_time == 0:
                        # Initialize log timer and create header.
                        self.log_start_time = time.time()
                        self.log_file.write('Time[s]\tRPM[min-1]\tMAF[g/sec]' +
                                             '\tRail[MPa]\tWater[degC]\tAir' +
                                             '[degC]\tEGR[%]\tBatt[V]\n')

                    self.log_file.write('%0.3f\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' %
                                        (time.time() - self.log_start_time,
                                         self.previous_text['0C'],
                                         self.previous_text['10'],
                                         self.previous_text['23'],
                                         self.previous_text['05'],
                                         self.previous_text['0F'],
                                         self.previous_text['2C'],
                                         self.previous_text['Batt']))
                    self.log_file.flush()

                    #
                    # After one slow PID is served, continue with fast PIDs
                    # again.
                    #
                    self.fast_pid_index = -1

                    self.slow_pid_index += 1
                    i = self.slow_pid_index

                    if i < len(self.slow_pids):
                        self.current_pid_id = self.slow_pids[i]
                        self.obd_comm.get_pid_value(self.current_pid_id)

                        # Initialize timeout timer.
                        self.time_packet_sent = get_time()
                    else:
                        self.slow_pid_index = -1

                        #
                        # Finally measure battery voltage - it's connected
                        # to analog channel 6.
                        #
                        pc.send_packet('A', '06')
                        self.state = MainScreen.ST_GET_VBB

        elif self.state == MainScreen.ST_GET_VBB:
            # If there is a new value available, refresh voltage indicator.
            if pc.battery_voltage['rdy']:
                # refresh analog indicator (progressbar)
                pb_name = "progressbar_voltage"
                fraction = pc.battery_voltage['value'] / \
                           pc.battery_voltage['max']
                if fraction > 1.0:
                    fraction = 1.0

                lbl_name = "label_voltage"
                # round to tenths and replace decimal dot with decimal comma
                rounded_text = '%.1f' % pc.battery_voltage['value']
                text_value = rounded_text.replace('.', ',')

                # Update indicators - but only if they changed.
                if self.previous_fraction['Batt'] != fraction:
                    self.previous_fraction['Batt'] = fraction
                    pb = self.builder.get_object(pb_name)
                    pb.set_fraction(fraction)

                if self.previous_text['Batt'] != text_value:
                    self.previous_text['Batt'] = text_value
                    lbl = self.builder.get_object(lbl_name)
                    lbl.set_text(text_value + ' ' + self.units['Batt'])

                # value is processed, clear the flag
                pc.battery_voltage['rdy'] = False

                # Now serve fast PIDs again.
                self.fast_pid_index = 0
                self.current_pid_id = self.fast_pids[0]
                self.obd_comm.get_pid_value(self.current_pid_id)

                # Initialize timeout timer.
                self.time_packet_sent = get_time()

                self.state = MainScreen.ST_GET_PIDS

#                pc.send_packet('A', '06')
#                self.state == MainScreen.ST_GET_VBB


#        pb = self.builder.get_object("progressbar1")
#        fr = pb.get_fraction()
#        fr += 0.1
#        if fr > 1:
#            fr = 0
#        pb.set_fraction(fr)
        
#        print 'a'
        t4 = time.time() - tstart

#        print 't1=%.3f, t2=%.3f, t3=%.3f, t4=%.3f' % (t1, t2, t3, t4)

        return True


    def enter(self):
        '''enter method for the progress screen'''

        toplevel = self.builder.get_object("mainwindow")
        toplevel.show_all()


    def quit(self, widget):
        # clean up before exit
        gobject.source_remove(self.comm_cb_id)

        # Close log file.
        self.log_file.write('===============================================\n')
        self.log_file.flush()
        self.log_file.close()

        # quit the application
        gtk.main_quit()

main_screen = MainScreen()
main_screen.enter()

# Pass control over to Gtk+
gtk.main()

