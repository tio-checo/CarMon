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


def kwp2000_calc_chsum(p):
    chsum = 0
    for i in range(len(p) / 2):
        chsum += int(p[2 * i:2 * (i + 1)], 16)
    return '%(ch)02X' % {'ch': chsum & 255}


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


def tester_recv_packet(ser):
    # wait for SOF first
    ch = ''
    while ch != '<':
        ch = ser.read(1)
#        print ch

    p = ''
    ch = ser.read(1)
#    print ch
    # now receive characters until EOF is received
    while ch != '>':
        p += ch
        ch = ser.read(1)
#        print ch

    return p

def tester_send_packet(ser, p):
    return ser.write(p)

# response dictionary
resp_dict={'WC133F18166':'<A><r83><rF1><r10><rC1><rE9><r8F><rBD>',
           'SC233F1010CF3':'<A><r84><F1><rF1><r10><r41><r0C><r02><r00><r00>',
           'SC233F10110F7':'<A><r84><F1><rF1><r10><r41><r10><r02><r00><r00>',
           'SC233F101230A':'<A><r84><F1><rF1><r10><r41><r23><r02><r00><r00>',
           'SC233F10105EC':'<A><r83><F1><rF1><r10><r41><r05><r78><r00>',
           'SC233F1010FF6':'<A><r83><F1><rF1><r10><r41><r0F><r64><r00>',
           'SC233F1012C13':'<A><r83><F1><rF1><r10><r41><r2C><r80><r00>',
           'SC233F1012D14':'<A><r83><F1><rF1><r10><r41><r2D><r90><r00>',
           'A06':'<a0200>'}

# ser=serial.Serial('/dev/ttyS0', 115200, timeout=0.5)
ser=serial.Serial('/dev/ttyUSB0', 115200)
time_start = time.time()
while True:
    # read value of given PID
#    pid = raw_input('PID: ')

    # receive packet from tester
    p = tester_recv_packet(ser)
    print "%.3f -> %s" % (time.time() - time_start, p)

    # find response in dictionary
    if p in resp_dict:
        print "<- %s" % resp_dict[p]
        tester_send_packet(ser, resp_dict[p])

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

