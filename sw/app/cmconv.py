#!/usr/bin/python2.7

import argparse

import os
import sys
import csv

from probe import ProbePool
from datetime import datetime, timedelta

#
# Converts raw timestamp to datetime object
# Timestamp in in form of 'YYYYMMDDHHMMSSmmm'
#
def ts2datetime(ts):
    dt = datetime.strptime(ts[0:-3], '%Y%m%d%H%M%S')
    # milliseconds must be processed separately
    dt += timedelta(milliseconds=int(ts[-3:]))

    return dt


#
# Extracts PID value from log record.
# Converts sequence of bytes encoded as a string of ASCII HEXA
# characters to the list of integers - one representing each byte.
#
def extract_raw_pid_value(r):
    s = r[2]
    ba = []
    for i in range(len(s) / 2):
        ba.append(int(s[2 * i:2 * (i + 1)], 16))

    return ba

#
# Returns True if given record carries information about ECU pid;
# False otherwise.
#
def is_obd_pid(r):
    return r[1].startswith('ECU')

#
# Extracts OBD PID from log record.
#
def extract_obd_pid(r):
    return r[1][-2:]


#
# Main application
#
def main():
    # Parse CLI options.
    parser = argparse.ArgumentParser(description='Converts ' +
                                     'raw captured data.')
    parser.add_argument('-f', metavar='FILE', dest='filename',
                        help='path to input file containing ' +
                             'raw captured data')

    args = parser.parse_args()

    # Instantiate pool of known probes
    probe_pool = ProbePool()

    #
    # Behave as a "filter" by default - read data from standard input
    # and write result to the standard output.
    # If name of input file is explicitly specified via '-f' option,
    # then read data from that file and write the result to the
    # file with the same name, but with 'txt' extension.
    #

    #
    # Read raw logged data - they are in CSV format.
    # Skip records not carrying ECU pids (e.g. GPS records)
    #
    if args.filename is not None:
        with open(args.filename, 'r') as fcsv:
            log_reader = csv.reader(fcsv, delimiter=',')
            # Read all records into the memory
            log_records = []
            for row in log_reader:
                if is_obd_pid(row):
                    log_records.append(row)
    else:
        log_reader = csv.reader(sys.stdin, delimiter=',')
        # Read all records into the memory
        log_records = []
        for row in log_reader:
            if is_obd_pid(row):
                log_records.append(row)

    # Discard the last record - it is very likely truncated
    del log_records[-1]

    # Determine start time from the first record
    start_time = ts2datetime(log_records[0][0])

    # Reconstruct list of all PIDs and PIDs in fast group
    pid_list = []
    group_fast_list = []
    update_fast_list = True
    for lr in log_records:
        pid = extract_obd_pid(lr)
        # If PID already in the list, skip it.
        if pid not in pid_list:
            pid_list.append(pid)

        if pid in group_fast_list:
            update_fast_list = False

        if update_fast_list:
            group_fast_list.append(pid)

    #
    # Remove the last PID from the fast group list, since
    # that PID belongs to the subsequent ('medium') group.
    #
    del group_fast_list[-1]

    # Generate list of logged probes from the list of logged PIDs
    probe_list = []
    for pid in pid_list:
        if probe_pool.get_probe('EGT11').get_obd_pid() == pid:
            # EGT probes logged
            probe_list.append('EGT11')
            probe_list.append('EGT12')
        elif probe_pool.get_probe('Inj1').get_obd_pid() == pid:
            # Inj probes logged
            probe_list.append('Inj1')
            probe_list.append('Inj2')
            probe_list.append('Inj3')
            probe_list.append('Inj4')
        elif probe_pool.get_probe('DPFar').get_obd_pid() == pid:
            # DPFar probe logged
            probe_list.append('DPFar')
        else:
            # Standard OBD probe
            probe_list.append(pid)

    # Append 'synthetic' Turbo probe
    probe_list.append('Turbo')

    # Initialize dictionary holding one converted data record.
    conv_record = {}
    conv_record['Time'] = ""
    for pid in probe_list:
        conv_record[pid] = ""

    # Generate header for converted data from labels of logged probes
    header = ['Time[s]']
    for pid in probe_list:
        probe = probe_pool.get_probe(pid)
        header.append(probe.get_label() + '[' + probe.get_units() + ']')

    # Write converted data in CSV format.
    if args.filename is not None:
        fout = open(args.filename.replace('csv', 'txt'), 'w')
    else:
        fout = sys.stdout

    log_writer = csv.writer(fout, delimiter='\t')

    # Write header containing probe labels with units
    log_writer.writerow(header)

    #
    # Process sequentially all log records
    #
    last_logged_time = start_time
    for lr in log_records:
        # Get timestamp
        ts = ts2datetime(lr[0])

        # Decode PID
        pid = extract_obd_pid(lr)
        #
        # Convert PID value in form of HEX ASCII string
        # to the list of bytes
        #
        value_list = extract_raw_pid_value(lr)

        #
        # Convert raw value to the string to be logged
        # and store the converted value to the temporary buffer
        # If Pid is not in the list of logged probes,
        # then it is used to calculate value of one of
        # "synthetic" probes.
        #
        if pid in probe_list:
            probe = probe_pool.get_probe(pid)
            probe.update(value_list)
            conv_record[pid] = probe.phys_to_log()

            # Also recalculate 'synthetic' Turbo probe
            turbo_probe = probe_pool.get_probe('Turbo')
            if turbo_probe.update(probe):
                conv_record['Turbo'] = turbo_probe.phys_to_log()
        elif probe_pool.get_probe('EGT11').get_obd_pid() == pid:
            # Update EGT Probes
            probe = probe_pool.get_probe('EGT11')
            probe.update(value_list)
            conv_record['EGT11'] = probe.phys_to_log()

            probe = probe_pool.get_probe('EGT12')
            probe.update(value_list)
            conv_record['EGT12'] = probe.phys_to_log()
        elif probe_pool.get_probe('Inj1').get_obd_pid() == pid:
            # Update Injector Probes
            probe = probe_pool.get_probe('Inj1')
            probe.update(value_list)
            conv_record['Inj1'] = probe.phys_to_log()

            probe = probe_pool.get_probe('Inj2')
            probe.update(value_list)
            conv_record['Inj2'] = probe.phys_to_log()

            probe = probe_pool.get_probe('Inj3')
            probe.update(value_list)
            conv_record['Inj3'] = probe.phys_to_log()

            probe = probe_pool.get_probe('Inj4')
            probe.update(value_list)
            conv_record['Inj4'] = probe.phys_to_log()
        elif probe_pool.get_probe('DPFar').get_obd_pid() == pid:
            # Update DPF accumulation ratio
            probe = probe_pool.get_probe('DPFar')
            probe.update(value_list)
            conv_record['DPFar'] = probe.phys_to_log()
        else:
            sys.stderr.writeline('Unknown PID')

        # Flush the temporary buffer to the log record if appropriate
        if pid == group_fast_list[-1]:
            #
            # Calculate time delta from the first record
            # and store the result to the temporary buffer.
            #
            td = ts - start_time
            td_str = '%d' % td.seconds + '.'
            td_str += '%03d' % (td.microseconds / 1000)
            conv_record['Time'] = td_str

            out_record = []
            out_record.append(conv_record['Time'])
            for pid in probe_list:
                out_record.append(conv_record[pid])

            log_writer.writerow(out_record)

    # If writing to the file, close it.
    if fout != sys.stdout:
        fout.close()

if __name__ == "__main__":
    main()
    sys.exit(0)
