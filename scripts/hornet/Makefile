# SPDX-License-Identifier: GPL-2.0
hostprogs-always-y	:= gen_sig

HOSTCFLAGS_gen_sig.o = $(shell $(HOSTPKG_CONFIG) --cflags libcrypto 2> /dev/null)
HOSTLDLIBS_gen_sig = $(shell $(HOSTPKG_CONFIG) --libs libcrypto 2> /dev/null || echo -lcrypto)
