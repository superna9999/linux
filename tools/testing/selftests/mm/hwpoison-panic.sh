#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Verify vm.panic_on_unrecoverable_memory_failure by injecting a hwpoison
# error on a kernel-owned page and confirming the kernel panics.
#
# Three "kinds" of kernel-owned page can be targeted, selectable via the
# first positional argument (default: rodata):
#
#   rodata  - a PG_reserved page in the kernel rodata range
#             (sourced from /proc/iomem "Kernel rodata").  Exercises
#             memory_failure() -> get_any_page() on a PageReserved page.
#
#   slab    - a slab page found via /proc/kpageflags (KPF_SLAB).
#             Exercises memory_failure() -> get_any_page() on a non
#             PG_reserved kernel-owned page.  This path is what catches
#             regressions where get_any_page() collapses kernel-owned
#             pages into a transient -EIO instead of -ENOTRECOVERABLE.
#
#   pgtable - a page-table page found via /proc/kpageflags (KPF_PGTABLE).
#             Same path as slab, different page type.
#
# This test is DESTRUCTIVE: a successful run crashes the kernel.  It is
# meant to be executed inside a disposable VM (e.g. virtme-ng) with a
# serial console captured by the harness.  It is skipped unless the
# caller opts in via RUN_DESTRUCTIVE=1.
#
# Test passes externally: the kernel must panic with
#   "Memory failure: <pfn>: unrecoverable page"
# A return from the inject means no panic fired: that is a failure,
# unless the target PFN raced to a different page type before injection,
# in which case the run is inconclusive and is skipped.
#
# Author: Breno Leitao <leitao@debian.org>

set -u

# KTAP output helpers (ktap_print_msg, ktap_skip_all, ktap_exit_fail_msg, ...).
DIR="$(dirname "$(readlink -f "$0")")"
# shellcheck source=../kselftest/ktap_helpers.sh
source "${DIR}"/../kselftest/ktap_helpers.sh

sysctl_path=/proc/sys/vm/panic_on_unrecoverable_memory_failure
inject_path=/sys/devices/system/memory/hard_offline_page
kpageflags_path=/proc/kpageflags
unpoison_path=/sys/kernel/debug/hwpoison/unpoison-pfn

# /proc/kpageflags bit positions (see include/uapi/linux/kernel-page-flags.h)
KPF_SLAB=7
KPF_COMPOUND_TAIL=16
KPF_HWPOISON=19
KPF_NOPAGE=20
KPF_PGTABLE=26
KPF_RESERVED=32

pagesize=$(getconf PAGE_SIZE)

kind=${1:-rodata}

if [ "$(id -u)" -ne 0 ]; then
	ktap_skip_all "must run as root"
	exit "$KSFT_SKIP"
fi

if [ ! -w "$sysctl_path" ]; then
	ktap_skip_all "$sysctl_path not present (kernel without the sysctl?)"
	exit "$KSFT_SKIP"
fi

if [ ! -w "$inject_path" ]; then
	ktap_skip_all "$inject_path not present (no MEMORY_HOTPLUG?)"
	exit "$KSFT_SKIP"
fi

if [ "${RUN_DESTRUCTIVE:-0}" != "1" ]; then
	ktap_skip_all "destructive test; re-run with RUN_DESTRUCTIVE=1 inside a disposable VM"
	exit "$KSFT_SKIP"
fi

# Pick a PFN inside the kernel image rodata region of /proc/iomem.
# This is preferred over a top-level "Reserved" entry because top-level
# Reserved ranges are often firmware holes that have no backing struct
# page; pfn_to_online_page() returns NULL on those and memory_failure()
# bails out with -ENXIO before reaching the panic path.
#
# "Kernel rodata" is reported as a sub-resource of "System RAM" on every
# major architecture, which guarantees:
#   - the PFN is backed by struct page (within an online memory range);
#   - PG_reserved is set on the page (kernel image area);
#   - the memory is read-only, so setting PG_hwpoison on it does not
#     corrupt writable kernel state if the panic somehow does not fire.
#
# /proc/iomem entries look like (indented for sub-resources):
#     "  02500000-02ffffff : Kernel rodata"
pick_rodata_phys_addr() {
	awk -v pagesize="$(getconf PAGE_SIZE)" '
	# Convert a hex string to a number without relying on the gawk-only
	# strtonum().  mawk lacks it and would otherwise spuriously skip
	# this test on distros that ship mawk as /usr/bin/awk.
	function hex2num(s,   n, i, c, v) {
		n = 0
		for (i = 1; i <= length(s); i++) {
			c = tolower(substr(s, i, 1))
			v = index("0123456789abcdef", c) - 1
			if (v < 0)
				return -1
			n = n * 16 + v
		}
		return n
	}
	/: Kernel rodata[[:space:]]*$/ {
		sub(/^[[:space:]]+/, "")
		n = split($0, a, /[- ]/)
		start = hex2num(a[1])
		end   = hex2num(a[2])
		if (end <= start)
			next
		# Page-align upward and emit the first byte of that page.
		pfn = int((start + pagesize - 1) / pagesize)
		printf "0x%x\n", pfn * pagesize
		exit 0
	}
	' /proc/iomem
}

# Walk /proc/kpageflags and return the phys addr of the first PFN that
# has bit $1 set, with KPF_HWPOISON, KPF_NOPAGE and KPF_COMPOUND_TAIL
# all clear (so we attack a real, non-tail, not-already-poisoned page).
#
# We skip the first 16 MiB of PFNs to step past low-memory special
# ranges (BIOS/EFI/ACPI/etc.) that often are PG_reserved and would not
# exhibit the slab/pgtable type we are looking for.
pick_kpageflags_phys_addr() {
	local want_bit=$1
	local pagesize skip_pfn

	[ -r "$kpageflags_path" ] || return

	pagesize=$(getconf PAGE_SIZE)
	skip_pfn=$(((16 * 1024 * 1024) / pagesize))

	od -An -tx8 -v -w8 -j "$((skip_pfn * 8))" "$kpageflags_path" 2>/dev/null | \
	awk -v want_bit="$want_bit" \
	    -v hwp_bit="$KPF_HWPOISON" \
	    -v nopage_bit="$KPF_NOPAGE" \
	    -v tail_bit="$KPF_COMPOUND_TAIL" \
	    -v base_pfn="$skip_pfn" \
	    -v pagesize="$pagesize" '
	# Test whether bit "b" is set in the 16-hex-digit value "hex".
	# Done with substring + per-digit lookup so we never rely on awk
	# bitwise operators (mawk lacks them), 64-bit FP precision or the
	# gawk-only strtonum().
	function bit_set(hex, b,    di, bi, c, v) {
		di = int(b / 4)
		bi = b - di * 4
		c = substr(hex, length(hex) - di, 1)
		v = index("0123456789abcdef", tolower(c)) - 1
		if (bi == 0) return (v % 2) == 1
		if (bi == 1) return int(v / 2) % 2 == 1
		if (bi == 2) return int(v / 4) % 2 == 1
		return int(v / 8) % 2 == 1
	}
	{
		gsub(/^[[:space:]]+/, "")
		h = $1
		if (bit_set(h, want_bit) &&
		    !bit_set(h, hwp_bit) &&
		    !bit_set(h, nopage_bit) &&
		    !bit_set(h, tail_bit)) {
			pfn = base_pfn + NR - 1
			printf "0x%x\n", pfn * pagesize
			exit 0
		}
	}
	'
}

# Return 0 if /proc/kpageflags bit $2 is set for PFN $1, 1 if it is
# clear, or 2 if the word cannot be read.  Used to re-confirm the target
# page type after a non-panicking inject.
kpageflags_bit_set() {
	local word

	word=$(od -An -tx8 -v -j "$(($1 * 8))" -N 8 "$kpageflags_path" 2>/dev/null | tr -d '[:space:]')
	[ -n "$word" ] || return 2
	(( (16#$word >> $2) & 1 ))
}

# Best-effort: drop the PG_hwpoison marker set by the inject so a failed
# run does not leave a poisoned page behind.  hard_offline_page() injects
# with MF_SW_SIMULATED, so the page stays unpoisonable through the
# hwpoison debugfs interface (needs CONFIG_HWPOISON_INJECT + debugfs).
try_unpoison() {
	[ -w "$unpoison_path" ] || return 0
	echo "$1" > "$unpoison_path" 2>/dev/null || true
}

case "$kind" in
rodata)
	phys_addr=$(pick_rodata_phys_addr)
	recheck_bit=$KPF_RESERVED
	missing_msg='no "Kernel rodata" entry in /proc/iomem'
	;;
slab)
	phys_addr=$(pick_kpageflags_phys_addr "$KPF_SLAB")
	recheck_bit=$KPF_SLAB
	missing_msg="no usable slab PFN found in $kpageflags_path"
	;;
pgtable)
	phys_addr=$(pick_kpageflags_phys_addr "$KPF_PGTABLE")
	recheck_bit=$KPF_PGTABLE
	missing_msg="no usable page-table PFN found in $kpageflags_path"
	;;
*)
	ktap_exit_fail_msg "unknown kind '$kind' (expected: rodata|slab|pgtable)"
	;;
esac

if [ -z "$phys_addr" ]; then
	ktap_skip_all "$missing_msg"
	exit "$KSFT_SKIP"
fi

ktap_print_msg "enabling $sysctl_path"
prior=$(cat "$sysctl_path")
echo 1 > "$sysctl_path" || ktap_exit_fail_msg "failed to enable sysctl"

pfn=$((phys_addr / pagesize))
ktap_print_msg "injecting hwpoison at phys 0x$(printf '%x' "$phys_addr") (pfn 0x$(printf '%x' "$pfn"), kind=$kind)"
ktap_print_msg "expecting kernel panic: 'Memory failure: <pfn>: unrecoverable page'"

# A successful run never returns from the inject -- it panics the kernel.
# Reaching the code below therefore means no panic fired.  Note whether
# the write itself succeeded, then put the machine back: restore the
# sysctl and best-effort unpoison the page we just marked.
if echo "$phys_addr" > "$inject_path"; then
	verdict="inject returned without panic; sysctl ineffective"
else
	verdict="inject failed before reaching the panic path"
fi

echo "$prior" > "$sysctl_path"
try_unpoison "$pfn"

# The page type can change between selection and injection (e.g. a slab
# or page-table page is freed and reused).  Only treat a missing panic as
# a failure if the target PFN is still the kernel-owned type we aimed at;
# if it raced to another type the run is inconclusive, so skip instead.
kpageflags_bit_set "$pfn" "$recheck_bit"
case $? in
0)	ktap_exit_fail_msg "$verdict (page still $kind)" ;;
1)	ktap_skip_all "target PFN no longer $kind; raced before inject, inconclusive"
	exit "$KSFT_SKIP" ;;
*)	ktap_exit_fail_msg "$verdict (could not reconfirm page type via $kpageflags_path)" ;;
esac
