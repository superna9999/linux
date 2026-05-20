# SPDX-License-Identifier: GPL-2.0
include ../../../build/Build.include
include ../../../scripts/Makefile.arch
include ../../../scripts/Makefile.include

CLANG ?= clang
CFLAGS := -g -O2 -Wall
BPFTOOL ?= $(TOOLSDIR)/bpf/bpftool/bpftool
SCRIPTSDIR := $(abspath ../../../../scripts/hornet)
TOOLSDIR := $(abspath ../../..)
LIBDIR := $(TOOLSDIR)/lib
BPFDIR := $(LIBDIR)/bpf
TOOLSINCDIR := $(TOOLSDIR)/include
APIDIR := $(TOOLSINCDIR)/uapi
CERTDIR := $(abspath ../../../../certs)
PKG_CONFIG ?= $(CROSS_COMPILE)pkg-config

TEST_GEN_PROGS := loader
TEST_GEN_FILES := vmlinux.h loader.h trivial.bpf.o map.bin sig.bin insn.bin signed_loader.h
$(TEST_GEN_PROGS): LDLIBS += -lbpf
$(TEST_GEN_PROGS): $(TEST_GEN_FILES)

include ../lib.mk

BPF_CFLAGS := -target bpf \
	-D__TARGET_ARCH_$(ARCH) \
	-I/usr/include/$(shell uname -m)-linux-gnu \
	$(KHDR_INCLUDES)

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

trivial.bpf.o: trivial.bpf.c vmlinux.h
	$(CLANG) $(CFLAGS) $(BPF_CFLAGS) -c $< -o $@

loader.h: trivial.bpf.o
	$(BPFTOOL) gen skeleton -S -k $(CERTDIR)/signing_key.pem -i $(CERTDIR)/signing_key.x509 \
		-L $< name trivial > $@

insn.bin: loader.h
	$(SCRIPTSDIR)/extract-insn.sh $< > $@

map.bin: loader.h
	$(SCRIPTSDIR)/extract-map.sh $< > $@

$(OUTPUT)/gen_sig: ../../../../scripts/hornet/gen_sig.c
	$(call msg,GEN_SIG,,$@)
	$(Q)$(CC) $(shell $(PKG_CONFIG) --cflags libcrypto 2> /dev/null) \
		  $< -o $@ \
		  $(shell $(PKG_CONFIG) --libs libcrypto 2> /dev/null || echo -lcrypto)

sig.bin: insn.bin map.bin $(OUTPUT)/gen_sig
	$(OUTPUT)/gen_sig --key $(CERTDIR)/signing_key.pem --cert $(CERTDIR)/signing_key.x509 \
		--data insn.bin --add map.bin:0 --out sig.bin

signed_loader.h: sig.bin
	$(SCRIPTSDIR)/write-sig.sh loader.h sig.bin > $@

loader: loader.c signed_loader.h
	$(CC) $(CFLAGS) -I$(LIBDIR) -I$(APIDIR) $< -o $@ -lbpf


EXTRA_CLEAN = $(OUTPUT)/gen_sig
