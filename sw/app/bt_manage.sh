#!/bin/sh
#
# Start/stop bluetooth service
#
RETVAL=0

# devices
RFCOMM_DEVICE=rfcomm0
HCI_DEVICE=hci0

# commands
CAT_CMD=/bin/cat
HCIATTACH_CMD=/usr/sbin/hciattach
HCICONFIG_CMD=/usr/sbin/hciconfig
KILL_CMD=/bin/kill
PGREP_CMD=/usr/bin/pgrep
RFCOMM_CMD=/usr/bin/rfcomm
RM_CMD=/bin/rm
TAIL_CMD=/usr/bin/tail

# PIDs of running programs
HCIATTACH_PID=/var/run/hciattach.pid
RFCOMM_PID=/var/run/rfcomm.pid

start() {
	#
	# Establish HCI communication channel to bluetooth device.
	# If channel was already established, just reset HCI device.
	#
	if [ -f $HCIATTACH_PID ] ; then
		$HCICONFIG_CMD $HCI_DEVICE reset
	else
		$HCIATTACH_CMD -p /dev/ttyS1 any 115200 |
		    $TAIL_CMD -1 > $HCIATTACH_PID

		$HCICONFIG_CMD $HCI_DEVICE up
	fi

	# Establish connection with remote RFCOMM device.
	$RFCOMM_CMD -i $HCI_DEVICE connect $RFCOMM_DEVICE >/dev/null &

	# It might take couple of seconds to establish the connection.
	sleep 5

	# Save PID of rfcomm process
	$PGREP_CMD "^rfcomm$" > $RFCOMM_PID
}

stop() {
	# Kill rfcomm process first.
	$KILL_CMD $($PGREP_CMD "^rfcomm$")
	[ -f $RFCOMM_PID ] && $RM_CMD $RFCOMM_PID
	sleep 1

	# And now kill hciattach process.
	$KILL_CMD $($PGREP_CMD "^hciattach$")
	[ -f $HCIATTACH_PID ] && $RM_CMD $HCIATTACH_PID
}

case "$1" in
	start)
		start
		;;
	stop)
		stop
		;;
	restart)
		stop
		sleep 3
		start
		;;
	status)
		echo "<A01>" > "/dev/${RFCOMM_DEVICE}"
		RETVAL=$?
		;;
	*)
		printf "Usage: %s {start|stop|restart|status}\n" $(basename $0)
		RETVAL=1
		;;
esac

exit $RETVAL
