#!/usr/bin/env python
#
# Alternative pretty-printer for simple trace backend binary trace files
# produced by TCG-mode emulation in icount mode.
# Output filtered and format tuned for 'diff' comparison of separate executions.
#
# For help see scripts/simpletrace.py

from simpletrace import *

class TcgExecFormatter(Analyzer):
    def __init__(self):
        self.last_timestamp = None

    def timesamp_delta(self, timestamp):
        if self.last_timestamp is None:
            self.last_timestamp = timestamp
        delta = timestamp - self.last_timestamp
        self.last_timestamp = timestamp
        return delta

    def Dropped_Event(self, num_events_dropped):
        raise ValueError('Dropped events detected, so it cannot be used ' \
                         'for correct tracing of guest execution\n')

    def exec_tb_icount_guest(self, virtualclock, pc):
        print("+%u: pc=%x" % (self.timesamp_delta(virtualclock), pc))

#def alt_run():
#    import sys
#    import argparse
#
#    argparser = argparse.ArgumentParser()
#    argparser.add_argument("trace_events", type=str, metavar="trace-events", help="trace events file")
#    argparser.add_argument("trace_file", type=str, metavar="trace-file", help="trace file")
#    argparser.add_argument("--no-header", action="store_true")
#    args = argparser.parse_args()
#
#    events = read_events(open(args.trace_events, 'r'), args.trace_events)
#    process(events, args.trace_file, TcgExecFormatter(), read_header=args.no_header)

if __name__ == '__main__':
    run(TcgExecFormatter())

