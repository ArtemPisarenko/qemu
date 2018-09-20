#!/usr/bin/perl

# Wrapper script for running qemu process,
# prepared to attach from any debugger running on same host.
# (Linux only.)
#
# Simple usage: just prepend qemu invokation command with "<this script> -- ".
#
# Synopsis
#    qemu-debug-stub.pl [options] -- qemu_binary [arguments]
# Options
#    --only-debugger=pid (restrict debug access to specified process and its subprocesses tree)
#    --print-qemu-pid (print invoked QEMU pid to stdout)
# Exit status
#    Script exits with status of QEMU process if it runs and exits normally.
#    Othwerwise it returns one of error codes defined as EXIT_* constants below.

use constant {
    EXIT_PROCESS_TERMINATED => 1,
    EXIT_EXECUTE_FAILED     => 2,
    EXIT_INTERNAL_ERROR     => 3,
};

use Getopt::Long;
use Env;
use POSIX qw(WIFSTOPPED WIFEXITED WEXITSTATUS);
use POSIX qw(:termios_h);
use Sys::Ptrace qw(ptrace PTRACE_TRACEME PTRACE_DETACH); # requires installing https://metacpan.org/pod/Sys::Ptrace
use Linux::Prctl(set_ptracer);

my $debugger_pid = -1;
my $do_print_qemu_pid = '';
my $qemu_pid;
my $terminal = POSIX::Termios->new;

GetOptions ("only-debugger=i" => \$debugger_pid,
            "print-qemu-pid" => \$do_print_qemu_pid)
or die("Error in command line arguments\n");

$ENV{'QEMU_GDB_STUB'} = 1;

sub qemu_pre_exec {
    $terminal->getattr(0);
}

sub qemu_post_exec {
    $terminal->setattr( 0, &POSIX::TCSANOW );
}

sub parent_sigchld_handler {
    waitpid($qemu_pid, WNOHANG);
    if (WIFSTOPPED(${^CHILD_ERROR_NATIVE})) {
       kill("STOP", $qemu_pid);
       ptrace(PTRACE_DETACH, $qemu_pid, 0, 0);
       if ($do_print_qemu_pid) {
          print "*** Use PID to attach debugger: $qemu_pid ***\n";
       }
    } else {
       my $exitcode;
       if (WIFEXITED(${^CHILD_ERROR_NATIVE})) {
          $exitcode = WEXITSTATUS(${^CHILD_ERROR_NATIVE});
       } else {
          $exitcode = EXIT_PROCESS_TERMINATED;
       }
       qemu_post_exec();
       exit $exitcode;
    }
}
$SIG{'CHLD'} = \&parent_sigchld_handler;

qemu_pre_exec();

$qemu_pid = fork();
if (not defined $qemu_pid) {
    print "Failed to fork: $!\n";
    exit EXIT_INTERNAL_ERROR;
}

if ($qemu_pid) {
    sleep while 1;
} else {
    $SIG{'CHLD'} = 'DEFAULT';
    set_ptracer($debugger_pid);
    ptrace(PTRACE_TRACEME, $$, 0, 0);
    exec { $ARGV[0] } @ARGV;
    print "Failed to execute qemu: $!\n";
    exit EXIT_EXECUTE_FAILED;
}

