#!/usr/bin/python

import argparse

import matplotlib.pyplot as plt
import numpy as np
import sys


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

    parser.add_argument('-t', dest='chart_type', default='XY',
                        choices=['XY', 'hist'],
                        help='type of chart')

    args = parser.parse_args()

    print 'index list:', args.index_list

    try:
        # Try to open and read provided file.
        with open(args.filename, 'r') as f_data:
            #
            # First line contains description which kind of data provided
            # file contains.
            #
            line = f_data.readline()
            data_desc = line.strip().split()

            print data_desc

            # Determine number of parameters.
            num_pars = len (data_desc)
            print 'num_pars:', num_pars

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

            data_line = f_data.readline()
            while data_line and not data_line.startswith('===='):
                split_line = data_line.strip().split()

                # Discard incomplete lines.
                if len(split_line) == num_pars:
#                    print 'split_line:', split_line
                    for i in range(len(split_line)):
                        obd_data[i].append(float(split_line[i]))
#                else:
#                    print 'Line %s incomplete, discarding.', split_line[0]

                data_line = f_data.readline()
    except IOError, err:
        print 'Failed to process %s file due to following error:' % \
               args.filename

        print str(err)

#    print '# of rows:', len(obd_data)
#    print '# of entires in row [0]:', len(obd_data[0])
#    print '# of entires in row [1]:', len(obd_data[1])
#    print 'Start of row 1:', obd_data[0][0:100]
#    print 'Start of row 2:', obd_data[1][0:100]
#    print 'row 2:', obd_data[1]

    # Just display information about data in supplied file and exit.
    if args.query:
        print 'Data rows:'
        for i in range(len(data_desc)):
            print ' Row %d: %s' % (i, data_desc[i])

        return

    if args.index_list == 'all':
        index_list = '1'
        for i in range(len(data_desc) - 2):
            index_list += ',%s' % str(i + 2)
    else:
        index_list = args.index_list

    ilist = index_list.split(',')
    num_rows = len(ilist)

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
            ax[i].plot(x, y[i], line_colors[i % len(line_colors)])
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
