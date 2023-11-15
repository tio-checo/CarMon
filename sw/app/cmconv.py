#!/usr/bin/python3

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
    for i in range(int(len(s) / 2)):
        ba.append(int(s[2 * i:2 * (i + 1)], 16))

    return ba

#
# Returns True if given record carries information about ECU pid;
# False otherwise.
#
def is_obd_pid(r):
    # Truncated record by no means does not carry valid PID value
    if len(r) < 3:
        return False
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

    parser.add_argument('-e', metavar='ENGINE', dest='engine',
                        default='2adftv', help='engine type (default is 2adftv)')

    args = parser.parse_args()

    # Instantiate pool of known probes
    probe_pool = ProbePool(args.engine)

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
        #
        # Some OBD PIDs carry more than one probe. In such case,
        # add all such compound probes this OBD PID carries to the probe list.
        #
        if probe_pool.is_compound_probe(pid):
            probe_list += probe_pool.get_compound_list(pid)
        else:
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
        pid_recognized = False
        if probe_pool.is_compound_probe(pid):
            # Compound probes
            pid_recognized = True
            for cprobe_id in probe_pool.get_compound_list(pid):
                cprobe = probe_pool.get_probe(cprobe_id)
                cprobe.update(value_list)
                conv_record[cprobe_id] = cprobe.phys_to_log()
        else:
            # OBD probe
            pid_recognized = True
            oprobe = probe_pool.get_probe(pid)
            oprobe.update(value_list)
            conv_record[pid] = oprobe.phys_to_log()

            # Also recalculate 'synthetic' Turbo probe
            turbo_probe = probe_pool.get_probe('Turbo')
            if turbo_probe.update(oprobe):
                conv_record['Turbo'] = turbo_probe.phys_to_log()

        if not pid_recognized:
            sys.stderr.write('Unknown PID\n')

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
