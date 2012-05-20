#!/bin/sh
# OBD log application wrapper
APP_DIR=/home/dambi/projekty/obdII/app

# Start bluetooth first.
${APP_DIR}/bt_manage.sh start

# Make sure it's up and running
${APP_DIR}/bt_manage.sh status || exit 1

# Now launch the application.
${APP_DIR}/obd_logger.py

# We are done, shut down bluetooth connection.
${APP_DIR}/bt_manage.sh stop

