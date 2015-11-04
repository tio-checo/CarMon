#!/usr/bin/python2.6 -u

import argparse
import serial
import sys
import time
import threading

ser = serial.Serial()

class IntelHex():
    ''' Class providing mechanism for manipulating ASCII files in Intel
    HEX format.
        filename - name of file which is loaded when object is instantiated
    '''
    def __init__(self, filename = None):
        if filename is None:
            return



class Bootloader():
    ''' Implements bootloader communication protocol
    '''
    CRC_TABLE = [ \
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, \
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef \
    ]

    SOH=0x01
    EOT=0x04
    DLE=0x10

    CMD_VERSION=0x01
    CMD_ERASE=0x02
    CMD_PROGRAM=0x03
    CMD_GET_CRC=0x04
    CMD_RUN=0x05

    #
    # At this point, only UART is supported as low level communication
    # protocol.
    #
    def __init__(self, comm_device, baudrate = 115200, timeout = 1.0,
                 half_duplex = False):

        self.comm_device = comm_device
        self.baudrate = baudrate
        self.timeout = timeout
        self.half_duplex = half_duplex

        self.ser = serial.Serial(self.comm_device, self.baudrate,
                                 timeout = self.timeout)

    def deactivate(self):
        self.ser.close()

    def calc_crc(self, data):
        ''' Calculates CRC16 checksum.
        '''
        crc = 0;
    
        for j in range(len(data)):
            i = (crc >> 12) ^ (data[j] >> 4);
            crc = Bootloader.CRC_TABLE[i & 0x0F] ^ (crc << 4);
            i = (crc >> 12) ^ (data[j] >> 0);
            crc = Bootloader.CRC_TABLE[i & 0x0F] ^ (crc << 4);

        return (crc & 0xFFFF);

    def recv_l2_char(self):
        '''
        '''
        b = self.ser.read(1)
        if len(b) == 0:
            return None
        else:
            #print "->0x%X" % ord(b[0])
            return ord(b[0])

    def send_l2_char(self, ch):
        '''
        '''
        #print "<-0x%X" % ord(ch)
        self.ser.write(ch)

        # If running in half duplex mode, every sent character is echoed back.
        # Discard that echo
        if self.half_duplex:
            self.recv_l2_char()

    def send_l3_char(self, ch):
        '''
        '''
        if (ord(ch) == Bootloader.SOH) or (ord(ch) == Bootloader.EOT) \
           or (ord(ch) == Bootloader.DLE):
            self.send_l2_char(chr(Bootloader.DLE))
        self.send_l2_char(ch)

    def send_l3_packet(self, p):
        '''
        '''
        # calculate crc
        crc = self.calc_crc(p)

        # 'Start of Header'
        self.send_l2_char(chr(Bootloader.SOH))

        # data
        for i in range(len(p)):
            self.send_l3_char(chr(p[i]))

        # crcL
        self.send_l3_char(chr(crc & 0xff))
        # crcH
        self.send_l3_char(chr(crc >> 8 & 0xff))

        # 'End of Frame'
        self.send_l2_char(chr(Bootloader.EOT))

    def recv_l3_char(self):
        ''' Receives one character. Handles DLE escaping.
        '''
        return self.recv_l2_char()

    def recv_l3_packet(self):
        '''
        '''
        buff = []
        # Wait for SOH first
        c = None
        while c != Bootloader.SOH:
            c = self.recv_l3_char()
            # If timeout reached, abort receiving
            if c == None:
                return None

        # Wait for the first character after SOH
        c = self.recv_l3_char()
        if c == None:
            return None

        # Now receive data until EOT is detected
        while c != Bootloader.EOT:
            # Handle escaped character
            if c == Bootloader.DLE:
                c = self.recv_l3_char()
                if c == None:
                    return None

            # Save data byte
            buff.append(c)

            # Get next character
            c = self.recv_l3_char()
            if c == None:
                return None

        # TODO: Verify checksum
        return buff

    def get_version(self):
        '''Get version of bootloader from device.
        '''
        self.send_l3_packet([Bootloader.CMD_VERSION])
        p = self.recv_l3_packet()

        # timeout expired
        if p == None:
            return None

        return "%d.%d" % (p[1], p[2])

    def erase_flash(self):
        '''
        '''
        self.send_l3_packet([Bootloader.CMD_ERASE])
        # It may take a while, so give it a try couple times
        tries = 10
        for i in range(tries):
            print ".",
            p = self.recv_l3_packet()
            if p != None:
                break;

        if p == None or len(p) != 3 or p[0] != Bootloader.CMD_ERASE:
            return False
        else:
            return True

    def program_flash(self, filename):
        '''
        '''
        #
        # Read Intel HEX file into memory. It is internally stored
        # as a list of lines.
        #
        try:
            with open(filename) as fd:
                line = fd.readline()

                # Empty string indicates 'End of File'
                while line != "":
                    # Remove leading ':' and trailing newline
                    stripped_line = line.strip().strip(':')

                    #print stripped_line

                    p = [Bootloader.CMD_PROGRAM]
                    # Convert ASCII hex characters to bootloader format
                    for i in range(len(stripped_line) / 2):
                        p.append(int(stripped_line[i * 2:(i + 1) * 2], 16))

                    self.send_l3_packet(p)

                    # Read next line from file
                    line = fd.readline()

                    # It may take a while, so give it a try couple times
                    tries = 10
                    for i in range(tries):
                        p = self.recv_l3_packet()
                        if p != None:
                            break;

                    if p == None or len(p) != 3 \
                       or p[0] != Bootloader.CMD_PROGRAM:
                        return False

                    print ".",
        except:
            raise

        return True

    def start_application(self):
        '''
        '''
        self.send_l3_packet([Bootloader.CMD_RUN])

        # There is no response from bootloader
        #p = self.recv_l3_packet()
        return True

    def read_crc(self):
        '''Read CRC.
        '''
        self.send_l3_packet([Bootloader.CMD_GET_CRC, 0, 0, 0, 0x9d,
                             4, 0, 0, 0])
        p = self.recv_l3_packet()

        # timeout expired
        if p == None or len(p) != 5 or p[0] != Bootloader.CMD_GET_CRC:
            return None

        return "0x%X%X" % (p[2], p[1])

# Main application
def main():
    # Parse CLI options.
    parser = argparse.ArgumentParser(description='PIC32MX bootloader')
    parser.add_argument('-p', metavar='FILE', required=False, dest='filename',
                        help='erase and program flash', default=None)

    parser.add_argument('-d', dest='comm_device', metavar='DEVICE',
#                        default='/tmp/SUNWut/units/IEEE802.00144fd6bfca/dev/term/a',
                        default='/tmp/SUNWut/units/IEEE802.00144f916844/dev/term/a',
                        help='device port (e.g. /dev/ttya)')

    parser.add_argument('-H', dest='half_duplex', action='store_true',
                        default=False,
                        help='run in half duplex mode')

    parser.add_argument('-v', dest='get_version', action='store_true',
                        default=False,
                        help='get bootloader version from device')

    parser.add_argument('-e', dest='erase_flash', action='store_true',
                        default=False,
                        help='erase flash memory')

    parser.add_argument('-j', dest='start_application', action='store_true',
                        default=False,
                        help='start application')

    parser.add_argument('-r', dest='read_crc', action='store_true',
                        default=False,
                        help='read crc')

    args = parser.parse_args()

    # Instantiate bootloader object
    #   - serial port is initialized
    #   - Intel HEX file is loaded to memory
    bl = Bootloader(args.comm_device, half_duplex = args.half_duplex)

    # open serial port for communication with PIC module
    #ser = serial.Serial('/dev/rfcomm0', 115200, timeout=1.0)
    #ser = serial.Serial(args.comm_device, 115200, timeout=1.0)

    # Try to establish connection first
    print "Establishing connection",
    for i in range(5):
        print ".",

        v = bl.get_version()
        if v != None:
            break

    if v == None:
        print "Failed."
        sys.exit(1)
    else:
        print "Done."

    # Get and display version of bootloader
    if args.get_version:
        v = bl.get_version()
        if v != None:
            print "get_version(): %s" % v
        else:
            print "get_version(): failed"

    if args.erase_flash:
        print "Erasing flash",
        if bl.erase_flash():
            print "Done."
        else:
            print "Operation failed."

    if args.filename != None:
        print "Programing flash",
        if bl.program_flash(args.filename):
            print "Done."
        else:
            print "Operation failed."

    if args.read_crc:
        print "Reading CRC..."
        crc = bl.read_crc()
        if crc != None:
            print "CRC: %s" % crc
        else:
            print "Operation failed."

    if args.start_application:
        print "Passing control to application...",
        if bl.start_application():
            print "Done."
        else:
            print "Operation failed."
    # Deactivate bootloader object
    #   - serial port is closed
    bl.deactivate()

if __name__ == "__main__":
    main()
    sys.exit(0)

