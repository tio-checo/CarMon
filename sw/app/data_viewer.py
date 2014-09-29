#!/usr/bin/python

import argparse

import matplotlib.pyplot as plt
import numpy as np
import os
import sys

def read_starting_voltage(probe_fname):
    ''' Reads starting voltage and returns it as a list of time-voltage pairs.
    Older versions of data logger stored starting voltage at the beginning
    of probe file. New version stores it in a separate file.

    Returns list with two sub-lists - the first one containing time values,
    the second one voltage values.
    '''

    # Instantiates empty resulting list.
    sv = []

    #
    # Determine if starting voltage is stored in a separate file.
    # Starting voltage filename is in form of 'sv-YYYYMMDD-HHMMSS.txt -
    # similar to the one which probe filename uses, just 'obd-' prefix
    # is replaced with 'sv-' one.
    #
    sv_fname = probe_fname.replace('obd-', 'sv-')

    # If separate file exists, process it.
    if (os.path.exists(sv_fname)):
        # Create sub-lists for time and voltage.
        sv.append([])
        sv.append([])

        try:
            # Try to open and read provided file.
            with open(sv_fname, 'r') as fdata:
                #
                # Process file line-by-line.
                # Discard first two lines containing comment and labels.
                # Measured data are terminated with '===========' line.
                #
                line = fdata.readline()
                line = fdata.readline()
                line = fdata.readline()

                while line and not line.startswith('===='):
                    split_line = line.split()

                    # Ignore all lines which do not contain two values.
                    if (len (split_line) != 2):
                        continue

                    # Add the time-voltage pair into the list.
                    sv[0].append(split_line[0])
                    sv[1].append(split_line[1])

                    # Get next line.
                    line = fdata.readline()

        except IOError, err:
            print 'Failed to process %s file due to following error:' % \
                   sv_fname

            print str(err)
    else:
        # Starting voltage captured in probe file, extract it from there.

        # Create sub-list for time values

        try:
            # Try to open and read probe file.
            with open(probe_fname, 'r') as fdata:
                #
                # If starting voltage is captured, the first line in the file
                # contains following comment:
                # '# Starting phase battery voltage'
                #
                line = fdata.readline()
                if (line.startswith('# Starting phase battery voltage')):
                    # Battery starting voltage captured, read captured data.
                    line = fdata.readline()

                    # Values are on one line - only separated by whitespace.
                    v_list = line.strip().split()

                    #
                    # Time is not provided in older version, thus populate
                    # time sub-list manually - assume 10ms sampling period.
                    #
                    t_list = []
                    for i in range(len (v_list)):
                        t_list.append(i * 0.010)
                else:
                    # Starting voltage not captured, create empty sub-list.
                    v_list = []

                # Add time and voltage sub-lists to the resulting list.
                sv.append(t)
                sv.append(v)

        except IOError, err:
            print 'Failed to process %s file due to following error:' % \
                   probe_fname

            print str(err)

    return sv


# Main application
def main():
    # Parse CLI options.
    parser = argparse.ArgumentParser(description='Visualize ' +
                                     'captured OBD data.')
    parser.add_argument('-f', metavar='FILE', required=True, dest='filename',
                        help='path to file containing captured data')

    parser.add_argument('-i', metavar='DATA1,DATA2,...',
                        dest='index_list', default='all',
                        help='list of parameters to be displayed')

    parser.add_argument('-q', dest='query', action='store_true', default=False,
                        help='display information about supplied file')

    parser.add_argument('-s', dest='show_start_voltage', action='store_true',
                        default=False,
                        help='display starting voltage')

    parser.add_argument('-t', dest='chart_type', default='XY',
                        choices=['XY', 'hist'],
                        help='type of chart')

    args = parser.parse_args()

    print 'index list:', args.index_list

    # Read starting voltage data
    starting_voltage = read_starting_voltage(args.filename);

    try:
        # Try to open and read probe file.
        with open(args.filename, 'r') as fdata:
            #
            # The first three lines in the file may contain battery voltage
            # captured during starting phase. The format of data is following:
            #
            #  # Starting phase battery voltage
            #   7.9 7.9 .... 14.1 14.2
            #  # Starting phase battery voltage
            #
            line = fdata.readline()

            if (line.startswith('# Starting phase battery voltage')):
                #
                # Probe file contains battery starting voltage, 
                # skip related lines, as they were already processed
                # in read_starting_voltage().
                #
                line = fdata.readline()

                # Skip footer
                line = fdata.readline()

                # Read subsequent line for further processing.
                line = fdata.readline()

            # Process line with data labels.
            data_desc = line.strip().split()

            print data_desc

            # Determine number of parameters.
            num_pars = len (data_desc)
            print 'num_pars:', num_pars

            if (len (starting_voltage[0]) != 0):
                print 'Starting voltage captured: Yes'
            else:
                print 'Starting voltage captured: No'

            #
            # Read captured data in two dimensional list. Each row
            # contains data for one captured OBD parameter.
            # Discard incomplete lines.
            # Read until EOF or until end marker (line which starts
            # with '====') is found.
            #
            obd_data = []
            for i in range(num_pars):
                obd_data.append([])

            data_line = fdata.readline()
            while data_line and not data_line.startswith('===='):
                split_line = data_line.strip().split()

                # Discard incomplete lines.
                if len (split_line) == num_pars:
#                    print 'split_line:', split_line
                    for i in range(len (split_line)):
                        obd_data[i].append(float(split_line[i]))
#                else:
#                    print 'Line %s incomplete, discarding.', split_line[0]

                data_line = fdata.readline()
    except IOError, err:
        print 'Failed to process %s file due to following error:' % \
               args.filename

        print str(err)

        return

#    print '# of rows:', len (obd_data)
#    print '# of entires in row [0]:', len (obd_data[0])
#    print '# of entires in row [1]:', len (obd_data[1])
#    print 'Start of row 1:', obd_data[0][0:100]
#    print 'Start of row 2:', obd_data[1][0:100]
#    print 'row 2:', obd_data[1]

    # Display information about data in given file and exit.
    if args.query:
        print 'Data rows:'
        for i in range(len (data_desc)):
            print ' Row %d: %s' % (i, data_desc[i])

        if (len (starting_voltage[0]) != 0):
            print 'Starting voltage captured: Yes'
        else:
            print 'Starting voltage captured: No'

        return

    # Display starting voltage and exit.
    if (args.show_start_voltage):
        plt.plot(starting_voltage[0], starting_voltage[1])
        plt.grid(True)
        plt.xlabel('Time[s]')
        plt.ylabel('Vbat[V]')
        plt.show()

        return

    if args.index_list == 'all':
        index_list = '1'
        for i in range(len (data_desc) - 2):
            index_list += ',%s' % str(i + 2)
    else:
        index_list = args.index_list

    ilist = index_list.split(',')
    num_rows = len (ilist)

    y = []
    for i in range(num_rows):
        y.append(obd_data[int(ilist[i])])

    x = obd_data[0]

    #
    # Plot selected data.
    # The first row contains time information. Use it for X axis.
    #

    # line colors
    line_colors = ['r', 'g', 'b', 'y', 'm', 'c']

    fig = plt.figure()
    ax = []

    for i in range(num_rows):
        # XY type of graph (with time on axis X)
        if args.chart_type == 'XY':
            # All subcharts share X axis with the first one.
            if i == 0:
                ax.append(fig.add_subplot(num_rows, 1, i + 1))
            else:
                ax.append(fig.add_subplot(num_rows, 1, i + 1, sharex=ax[0]))

            ax[i].set_ylabel(data_desc[int(ilist[i])])
            ax[i].plot(x, y[i], line_colors[i % len (line_colors)])
            ax[i].grid(True)
        elif args.chart_type == 'hist':
            ax.append(fig.add_subplot(1, num_rows, i + 1))
            ax[i].hist(y[i], bins=20, log=False)
            ax[i].set_xlabel(data_desc[int(ilist[i])])
            ax[i].set_ylabel('#samples')
            ax[i].grid(True)

    if args.chart_type == 'XY':
        t = ax[0].set_title('OBD data')
        plt.xlabel(data_desc[0])

    # Now just let matplotlib module handle displaying graphs.
    plt.show()


if __name__ == "__main__":
    main()
    sys.exit(0)
