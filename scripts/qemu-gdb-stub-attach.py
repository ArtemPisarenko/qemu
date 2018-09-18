#!/usr/bin/python

# GDB debugging support
#
# Works automatically, no user interaction required.
# (Detects attached inferior and skips extra STOP events.)

import gdb
import subprocess

log_prefix = __file__ + ": "
inferior_ready = False
stop_counter = 2

def stop_event_handler(event):
    global inferior_ready, log_prefix, stop_counter
    if (not inferior_ready):
        inferior_ready = True
        pid = gdb.selected_inferior().pid
        is_attached = gdb.selected_inferior().was_attached
        is_invoked_via_stub = (subprocess.call('sed \'s/\\x0/\\n/g\' /proc/'+str(pid)+'/environ | grep -q QEMU_GDB_STUB=', shell=True) == 0)
        if (not (is_attached and is_invoked_via_stub)):
            gdb.events.stop.disconnect(stop_event_handler)
            return
        gdb.write(log_prefix + "detected 'qemu-debug-stub' session\n")
        return
    stop_counter -= 1
    if (stop_counter == 0):
        gdb.events.stop.disconnect(stop_event_handler)
    gdb.write(log_prefix + "auto skipping stop event\n")
    gdb.execute("continue")

gdb.events.stop.connect(stop_event_handler)

