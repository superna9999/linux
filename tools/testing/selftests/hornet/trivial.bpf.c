// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

int monitored_pid = 0;

SEC("tracepoint/syscalls/sys_enter_unlinkat")
int handle_enter_unlink(struct trace_event_raw_sys_enter *ctx)
{
	char filename[128] = { 0 };
	struct task_struct *task;
	unsigned long start_time = 0;
	int pid = bpf_get_current_pid_tgid() >> 32;
	char *pathname_ptr = (char *) BPF_CORE_READ(ctx, args[1]);

	bpf_probe_read_str(filename, sizeof(filename), pathname_ptr);
	task = (struct task_struct *)bpf_get_current_task();
	start_time = BPF_CORE_READ(task, start_time);

	bpf_printk("BPF triggered unlinkat by PID: %d, start_time %ld. pathname = %s",
		   pid, start_time, filename);

	if (monitored_pid == pid)
		bpf_printk("target pid found");

	return 0;
}
