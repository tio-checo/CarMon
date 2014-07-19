#!/usr/bin/python
import serial
import sys
import time

# Start KWP2000 communication
# ECU response: 83 F1 10 C1 E9 8F BD
KWP2000_COMM_START='C133F181'

# Stop KWP2000 communication
# ECU response: 81 F1 10 C2 44
KWP2000_COMM_STOP='C133F182'

# Get value of PID
# ECU response: 81 F1 10 C2 44
KWP2000_GET_PID_VALUE='C233F101'


def kwp2000_send_recv_packet(ser, s, wup=False):
    # Append checksum and send the packet
    s1 = s + kwp2000_calc_chsum(s)

    # Assemble packet for OBD PIC converter
    if wup:
        # Prepend KWP2000 packet with 'Wake up Pattern'
        s1 = '<W' + s1
    else:
        s1 = '<S' + s1

    s1 += '>'

    print '<- ' + s1
    ser.write(s1)

    # Wait for acknowledge first
    r = ''
    ack = ''
    while r is not '>':
        r = ser.read(1)
        ack += r

    print 'a: ' + ack

    # Wait for response packet.
    # Each KWP2000 byte (pair of ASCII characters) is transmitted as
    # sequence of <rBB>.

    # Determine the length of the whole packet from the first received byte.
    response = ser.read(5)
    if len(response) != 5:
        print 'Malformed ECU response received: "%s"' % response
        return ''

    ln = int(response[3], 16)

    # account for additional KWP2000 packet overhead
    for i in range(ln + 3):
        b = ser.read(5)
        if len(b) != 5 or b[0:2] != '<r' or b[-1] != '>':
            print 'Malformed ECU response received: "%s"' % (response + b)
            return ''

        response += b

    print '-> ' + response
    return response.strip('<r>')


class ECUSim(object):
    ''' class ECUSim
    '''

    # response dictionary
    resp_dict1 = {'WC133F18166':'<A><r83><rF1><r10><rC1><rE9><r8F><rBD>',
                  'SC233F1010BF2':'<A><r83><rF1><r10><r41><r0B><r00><r00>',
                  'SC233F1010CF3':'<A><r84><rF1><r10><r41><r0C><r02><r00><r00>',
                  'SC233F1010DF4':'<A><r83><rF1><r10><r41><r0D><r00><r00>',
                  'SC233F10110F7':'<A><r84><rF1><r10><r41><r10><r02><r00><r00>',
                  'SC233F101230A':'<A><r84><rF1><r10><r41><r23><r02><r00><r00>',
                  'SC233F10105EC':'<A><r83><rF1><r10><r41><r05><r78><r00>',
                  'SC233F1010FF6':'<A><r83><rF1><r10><r41><r0F><r64><r00>',
                  'SC233F1012C13':'<A><r83><rF1><r10><r41><r2C><r80><r00>',
                  'SC233F1012D14':'<A><r83><rF1><r10><r41><r2D><r90><r00>',
                  'SC233F101331A':'<A><r83><rF1><r10><r41><r33><r00><r00>',
                  'A06':'<a0200>'}

    resp_dict = {'C133F18166': ['83F110C1E98FBD'],
                 # supported PIDs 01-20
                 'C233F10100E7': ['86F1104100983B80132E'],
                 'C233F10104EB': ['83F11041048000'],
                 'C233F10105EC': ['83F11041057800'],
                 'C233F1010BF2': ['83F110410B80D1'],
                 'C233F1010CF3': ['84F110410C020000'],
                 'C233F1010DF4': ['83F110410D0000'],
                 'C233F1010FF6': ['83F110410F6400'],
                 'C233F10110F7': ['84F1104110020000'],
                 'C233F10111F8': ['83F11041117000'],
                 # supported PIDs 21-40
                 'C233F1012007': ['86F1104120A019A00142'],
                 'C233F101230A': ['84F1104123020000'],
                 'C233F1012C13': ['83F110412C8000'],
                 'C233F1012D14': ['83F110412D9000'],
                 'C233F101331A': ['83F11041336400'],
                 # supported PIDs 41-60
                 'C233F1014027': ['86F110414040DE000026'],
                 # read stored DTCs
                 'C133F103E8': ['87F11043000000000000CB'],
                 # read pending DTCs
                 'C133F107EC': ['87F11047123456789ABCCF'],
                 # read calibration ID - response contains 4 packets
                 'C233F10904F3': ['87F11049040133304631B0',
                                  '87F110490402303630309D',
                                  '87F11049040300000000D8',
                                  '87F11049040400000000D9']}

    #
    # Various timing aspects of KWP2000 packet - values are in seconds.
    #--------------------------------
    # TIMING
    #
    # Tester            ECU1        ECU2       Tester
    # request         response    response     request
    #______________________________________________________
    # U V V ... U    U V ... U    U V ... U    U V ... U
    # \ /        \  /\ /      \  /         \  /
    #  P4         P2  P1       P2           P3
    #
    # default values for P1-P4 timing parameters
    #
    # Value min max description
    #--------------------------
    # P1    0   20  Inter byte time in ECU response
    # P2   25   50  Time between end of tester request and start of ECU response
    # P3   55  5000 Time between end of ECU response and start of new tester
    #               request
    # P4    5   20  Inter byte time in tester request
    #
    KWP2K_T_P1_DEFMIN = 0	# P1 default min
    KWP2K_T_P1_DEFMAX = 0.020	# P1 default max
    KWP2K_T_P2_DEFMIN = 0.025	# P2 default min
    KWP2K_T_P2_DEFMAX = 0.050	# P2 default max
    KWP2K_T_P3_DEFMIN = 0.055	# P3 default min
    KWP2K_T_P3_DEFMAX = 5	# P3 default max
    KWP2K_T_P4_DEFMIN = 0.005	# P4 default min
    KWP2K_T_P4_DEFMAX = 0.020	# P4 default max

    KWP2K_HDR_LEN_MSK = 0x3f

    def __init__(self):
        # physical ECU address
        self.ecu_padr = 0x10
        # functional ECU address
        self.ecu_padr = 0x33

        # physical tester address
        self.tester_padr = 0xF1

        return

    def comm_open(self, device, speed):
        '''
        '''
        self.ser = serial.Serial(device, speed)

    def comm_close(self):
        '''
        '''
        self.ser.close()

    def calc_kwp_chsum(self, p):
        ''' Calculate checksum for given packet. Return calculated checksum.
        '''
        chsum = 0
        for i in range(len(p) - 1):
            chsum += p[i]

        return chsum & 0xff

    def verify_kwp_chsum(self, p):
        ''' Verify checksum for given packet. Return True if packet is valid,
        otherwise return False.
        '''
        return p[-1] == self.calc_kwp_chsum(p)

    def master_recv_packet(self):
        '''
        '''
        # wait for SOF first
        ch = ''
        while ch != '<':
            ch = self.ser.read(1)
#            print ch

        p = ''
        ch = self.ser.read(1)
#        print ch
        # now receive characters until EOF is received
        while ch != '>':
            p += ch
            ch = self.ser.read(1)
#            print ch

        return p

    def master_send_packet(self, p):
        '''
        '''
        return self.ser.write(p)

    def tester_recv_packet(self):
        ''' Receives packet from tester. Filters out possible Wake up Pattern.
        It was observed that WuP is received as a sequence of 'FF 00 00'
        with almost zero interbyte delay, then the first byte of packet
        arrives after approximately 50 milliseconds.
        '''

        #
        # Receive sequence of bytes until valid packet is detected.
        # Valid packet has to meet following requirements:
        #  - delay between two bytes is not longer than KWP2K_T_P1_DEFMAX
        #  - length of packet complies with length information specified
        #    in packet
        #  - valid checksum
        #
        is_first_byte = True
        ln = 0
        curr_byte = 0
        p_list = []
        p_str = ''

        # Initialize timer measuring inter-byte delay.
        ib_timer = time.time()

	while True:
            ch = self.ser.read(1)
            ord_ch = ord(ch)

            # calculate inter-byte time, then reset timer
            ib_delay = time.time() - ib_timer
            ib_timer = time.time()

            #
            # If too much time has passed since last character was received,
            # assume this is just a beginning of new packet.
            #
            if ib_delay > ECUSim.KWP2K_T_P1_DEFMAX:
                is_first_byte = True

            # Process first byte, it contains information about packet size.
            if is_first_byte:
                # Discard currently received packet. Start from scratch.
                is_first_byte = False
                ln = ord_ch & ECUSim.KWP2K_HDR_LEN_MSK
                curr_byte = 0
                p_list = []
                p_str = ''

                # account for header and checksum
                if ln == 0:
                    # length transmitted in separate length byte
                    has_separate_length_byte = True
                    ln += 5
                else:
                    has_separate_length_byte = False
                    ln += 4

                print "\n->[%02X]" % ln,

            # print currently received byte
            print "%02X(%.1f)," % (ord_ch, ib_delay * 1000.0),

            # Increment counter of received bytes.
            curr_byte += 1

            # Handle separate length byte.
            if has_separate_length_byte and curr_byte == 4:
                ln += ord_ch

            # save character to buffer
            p_list.append(ord_ch)
            p_str += "%02X" % ord_ch

            # if it is last character received, verify checksum
            ln -= 1
            if ln == 0:
                if self.verify_kwp_chsum(p_list):
                    break
                else:
                    # invalid checksum, wait for another packet
                    is_first_byte = True
        print "<%s>" % p_str

        return p_str

    def tester_send_packet(self, p):
        ''' Sends packet to tester. Calculate checksum. Generate desired
        inter packet as well as inter byte delays.
        '''
        # First wait until line is silent for desired amount of time
        time.sleep(0.028)

        # Convert packet from ASCII string to list of bytes.
        p_list = []
        for i in range(len(p) / 2):
            p_list.append(int(p[i * 2:(i + 1) * 2], 16))

        # Calculate checksum
        chsum = self.calc_kwp_chsum(p_list)
        p_list[-1] = chsum

        ln = len(p_list)
        for i in range(ln):
            self.ser.write(chr(p_list[i]))
            #
            # Wait approximately two milliseconds (the measured value was
            # about 1.3ms. Smaller values seem to be rounded down to zero.
            #
            time.sleep(0.002)

        # discard echoes
        self.ser.read(ln)

        print "<-[%02X] <%s>" % (len(p_list), p)

        return

# Main application
def main():
    # ser=serial.Serial('/dev/ttyS0', 115200, timeout=0.5)

    ecu_sim = ECUSim()

#    ecu_sim.comm_open('/dev/ttya', 9600)
#    ecu_sim.comm_open('/tmp/SUNWut/units/IEEE802.00144f916844/dev/term/a', 9600)
#    ecu_sim.comm_open('/tmp/SUNWut/units/IEEE802.002128de936c/dev/term/a', 9600)
    ecu_sim.comm_open('/tmp/SUNWut/units/IEEE802.0021287e7ee5/dev/term/a', 9600)

    while True:
        # Wait for packet from tester.
        p = ecu_sim.tester_recv_packet()

        # If there is a response in dictionary, send it to tester.
        if ECUSim.resp_dict.has_key(p):
            # response is stored as a list of packets
            response = ECUSim.resp_dict[p]

            for i in range(len(response)):
                ecu_sim.tester_send_packet(response[i])
        else:
            # response not in dictionary
            print "Unknown packet: %s" % (p)

    ecu_sim.comm_close()

    return

    time_start = time.time()
    pid_value = {'0B':0, '0C':0, '0D':0, '10':0, '23':0,
                 '05':0, '0F':0, '2C':0, '2D':0, '33':0, 'Batt':0}
    while True:
        # read value of given PID
    #    pid = raw_input('PID: ')

        # receive packet from tester
        p = ecu_sim.tester_recv_packet()

        print "%.3f -> %s" % (time.time() - time_start, p)

        # wait for a while
        time.sleep(0.028)

        # response not in dictionary
        if not p in resp_dict:
            print "Unknown packet: %s" % (p)
            continue

        # find response in dictionary
        response = resp_dict[p]
        pic_cmd = p[0]
        pid_id = response[25:27]
        # Battery voltage
        if pic_cmd == 'A':
            pid_id = 'Batt'
            pidv = '%04X' % pid_value[pid_id]
            pid_value[pid_id] += 10
            if pid_value[pid_id] > 0x3ff:
                pid_value[pid_id] = 0

            resp_mod = response[0:2] + pidv + response[6:]
            print "<- %s" % resp_mod
            tester_send_packet(ser, resp_mod)
        elif pid_id == '0C' or pid_id == '10' or pid_id == '23':
            pidv = '%02X' % pid_value[pid_id]
            pid_value[pid_id] += 1
            if pid_value[pid_id] > 0x50:
                pid_value[pid_id] = 0
          
            resp_mod = response[0:30] + pidv + response[32:]
            print "<- %s" % resp_mod
            tester_send_packet(ser, resp_mod)
        elif pid_id == '05' or pid_id == '0B' or pid_id == '0D' or \
            pid_id == '0F' or pid_id == '2C' or pid_id == '2D' or \
            pid_id == '33':
            pidv = '%02X' % pid_value[pid_id]
            pid_value[pid_id] += 5
            if pid_value[pid_id] > 0xff:
                pid_value[pid_id] = 0

            resp_mod = response[0:30] + pidv + response[32:]
            print "<- %s" % resp_mod
            tester_send_packet(ser, resp_mod)            
        else:
            print "<- %s" % response
            tester_send_packet(ser, response)

        # start communication
    #    kwp2000_send_recv_packet(ser, KWP2000_COMM_START, True)
    #    if not kwp2000_send_recv_packet(ser, KWP2000_COMM_START, True):
    #        continue

        # wait for a while (250ms) before sending next request (min is 55ms)
    #    time.sleep(0.25)
        # get value of PID
    #    pid_packet = kwp2000_send_recv_packet(ser, KWP2000_GET_PID_VALUE +
    #                                         pid.upper()[0:2])

    #    if not pid_packet:
    #        continue

    #    pid_ln = int(pid_packet[1], 16) - 2
    #    print 'pid:' + pid_packet[10:10 + 2 * pid_ln]

        # wait for a while (250ms) before sending next request (min is 55ms)
    #    time.sleep(0.25)
        # stop communication
    #    kwp2000_send_recv_packet(ser, KWP2000_COMM_STOP)

if __name__ == "__main__":
    main()
    sys.exit(0)
