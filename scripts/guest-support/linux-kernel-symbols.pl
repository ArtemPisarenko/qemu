#!/usr/bin/env perl
#
# (Adapted and improved version of ksyms.pl taken from https://rwmj.wordpress.com/2016/03/17/tracing-qemu-guest-execution/ )
#
# Find program counters with linux kernel address in the input
# and append it with source code descriptions using addr2line.
#
# Compatible with trace data, produced by QEMU 'simpletrace_tcg-exec-icount.py' script.
#
# Usage:
#   linux-kernel-symbols.pl source_root_dir vmlinux_path addr2line_path < input > output
# where
#   'input' is a trace data with lines containing "pc=" key-value (hex) pairs
#   'output' is a processed input data appended with descriptive source lines for debugging
#   'source_root_dir' is a root of path with linux kernel sources, from which
#     vmlinux binary was build (actually, any path may be given, it's just to shorten file paths in output)
#   'vmlinux_path' is a path to vmlinux binary
#   'addr2line_path' is a path to addr2line utility from toolchain vmlinux was built
#     (system/distro default one may fit in case if host is of same architecture)
#
# Example (standalone):
#    ./linux-kernel-symbols.pl ./kernel-src ./kernel-build/vmlinux addr2line < input_trace_dump.txt > trace_dump_vmlinux.txt
# Example (QEMU):
#    ./scripts/simpletrace_tcg-exec-icount.py trace-events-all <trace-file> | \
#      ./scripts/guest-support/linux-kernel-symbols.pl ../kernel-src ../kernel-build/vmlinux addr2line > trace_dump_vmlinux.txt

use File::Spec qw(abs2rel);
use FileHandle;
use IPC::Open3;

my %cache = ();

my $source_root_dir = shift;
$source_root_dir = `readlink -n $source_root_dir`;
my $vmlinux_path = shift;
my $addr2line_path = shift;

# Start addr2line utility in parallel to use its stdio streams to perform conversions
# (makes huge speedup to processing of large data)
my $addr2line_cmd = "$addr2line_path -f -i -p -e $vmlinux_path";
my $addr2line_in = new FileHandle;
my $addr2line_out = new FileHandle;
my $addr2line_err = new FileHandle;
open3($addr2line_in, $addr2line_out, $addr2line_err, "$addr2line_cmd") or die "$addr2line_cmd: $!";
vec($addr2line_s_bits, $addr2line_out->fileno(), 1) = 1;
vec($addr2line_s_bits, $addr2line_err->fileno(), 1) = 1;

while (<>) {
    s{^(.*pc\=)([0-9a-fA-F]{6,16})(.*)$}{ $1 . $2 . $3 . addr_to_src_code_desc ($2) }e;
    print
}

sub addr_to_src_code_desc
{
    local $_;
    my $addr = $_[0];
    my $src_code_desc;

    # Check if data is already memorized
    return $cache{$addr} if exists $cache{$addr};

    # Send address to addr2line utility...
    print $addr2line_in "$addr\n";

    # ... and wait&receive result from it
    # (we cannot simply use buffered "$src_code_desc = <$addr2line_out>",
    #  because it may consist of multiple lines)
    # (also we assume that addr2line flushes stdout explicitly and once it writes complete answer)
    my $nfound = select($addr2line_s_bits,undef,undef,undef);
    die "addr2line I/O error\n" if ($nfound == -1);
    sysread $addr2line_out,$src_code_desc,100000; #TODO: replace magic LENGTH with appropriate value

    if ($src_code_desc =~ m/^\?\?/) {
        # No match, just return the empty string
        $src_code_desc = "";
    } else {
        # Format result for better readability
        $src_code_desc =~ s{(at\s+)(.+)(\:\d+)}{ $1 . path_relative_to_source_root ($2) . $3 }ge;
        $src_code_desc = "\n$src_code_desc";
    }

    # Memorize data for later reuse.
    $cache{$addr} = $src_code_desc;

    return $src_code_desc;
}

sub path_relative_to_source_root
{
    local $_;
    my $abs_path = $_[0];
    my $rel_path = File::Spec->abs2rel( $abs_path, $source_root_dir );
    return $rel_path;
}

