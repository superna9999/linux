// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include  "signed_loader.h"

int main(int argc, char **argv)
{
	struct trivial *skel;

	skel = trivial__open_and_load();
	if (!skel)
		return -1;

	trivial__destroy(skel);
	return 0;
}
