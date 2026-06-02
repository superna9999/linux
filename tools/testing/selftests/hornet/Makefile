# SPDX-License-Identifier: GPL-2.0
include ../../../build/Build.include
include ../../../scripts/Makefile.arch
include ../../../scripts/Makefile.include

CLANG ?= clang
CFLAGS := -g -O2 -Wall
TOOLSDIR := $(abspath ../../..)
BPFTOOL ?= $(TOOLSDIR)/bpf/bpftool/bpftool
SCRIPTSDIR := $(abspath ../../../../scripts/hornet)
LIBDIR := $(TOOLSDIR)/lib
BPFDIR := $(LIBDIR)/bpf
TOOLSINCDIR := $(TOOLSDIR)/include
APIDIR := $(TOOLSINCDIR)/uapi
CERTDIR := $(abspath ../../../../certs)
HOSTPKG_CONFIG ?= pkg-config

SIGNING_KEY  := $(CERTDIR)/signing_key.pem
SIGNING_CERT := $(CERTDIR)/signing_key.x509

VMLINUX_BTF_PATHS ?= $(if $(O),$(O)/vmlinux)				\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../../../vmlinux				\
		     /sys/kernel/btf/vmlinux				\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))

# The hornet selftest needs the kernel module signing key/cert (generated when
# the kernel is built with CONFIG_MODULE_SIG=y), a bpftool binary, and a
# vmlinux with BTF for trivial.bpf.o. If any of those are missing (cross-build
# without artifacts, container CI, CONFIG_MODULE_SIG disabled, etc.) skip the
# targets rather than failing the global selftests build.
hornet_skip_reason :=
ifeq ($(wildcard $(SIGNING_KEY)),)
hornet_skip_reason := module signing key not found at $(SIGNING_KEY) (build the kernel with CONFIG_MODULE_SIG=y first)
else ifeq ($(wildcard $(SIGNING_CERT)),)
hornet_skip_reason := module signing cert not found at $(SIGNING_CERT)
else ifeq ($(wildcard $(BPFTOOL)),)
hornet_skip_reason := bpftool not found at $(BPFTOOL) (build it under tools/bpf/bpftool first)
else ifeq ($(VMLINUX_BTF),)
hornet_skip_reason := no vmlinux with BTF found; tried $(VMLINUX_BTF_PATHS) (build the kernel with CONFIG_DEBUG_INFO_BTF=y or set VMLINUX_BTF=)
endif

ifneq ($(hornet_skip_reason),)
$(warning Skipping hornet selftests: $(hornet_skip_reason))
TEST_GEN_PROGS :=
TEST_GEN_FILES :=

include ../lib.mk

else

TEST_GEN_PROGS := loader
TEST_GEN_FILES := vmlinux.h loader.h trivial.bpf.o map.bin sig.bin insn.bin signed_loader.h

include ../lib.mk

define get_sys_includes
$(shell $(1) $(2) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__loongarch_grlen ' | awk '{printf("-D__BITS_PER_LONG=%d", $$3)}') \
$(shell $(1) $(2) -dM -E - </dev/null | grep -E 'MIPS(EL|EB)|_MIPS_SZ(PTR|LONG) |_MIPS_SIM |_ABI(O32|N32|64) ' | awk '{printf("-D%s=%s ", $$2, $$3)}')
endef

ifneq ($(CROSS_COMPILE),)
CLANG_TARGET_ARCH = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif
CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG),$(CLANG_TARGET_ARCH))

IS_LITTLE_ENDIAN := $(shell $(CC) -dM -E - </dev/null | \
			grep 'define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__')
BPF_TARGET_ENDIAN := $(if $(IS_LITTLE_ENDIAN),--target=bpfel,--target=bpfeb)

BPF_CFLAGS := $(BPF_TARGET_ENDIAN) \
	-D__TARGET_ARCH_$(SRCARCH) \
	$(CLANG_SYS_INCLUDES) \
	$(KHDR_INCLUDES)

$(OUTPUT)/vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL)
	$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@

$(OUTPUT)/trivial.bpf.o: trivial.bpf.c $(OUTPUT)/vmlinux.h
	$(CLANG) $(CFLAGS) $(BPF_CFLAGS) -I$(OUTPUT) -c $< -o $@

$(OUTPUT)/loader.h: $(OUTPUT)/trivial.bpf.o
	$(BPFTOOL) gen skeleton -S -k $(SIGNING_KEY) -i $(SIGNING_CERT) \
		-L $< name trivial > $@

$(OUTPUT)/insn.bin: $(OUTPUT)/loader.h
	$(SCRIPTSDIR)/extract-insn.sh $< > $@

$(OUTPUT)/map.bin: $(OUTPUT)/loader.h
	$(SCRIPTSDIR)/extract-map.sh $< > $@

$(OUTPUT)/gen_sig: ../../../../scripts/hornet/gen_sig.c
	$(call msg,GEN_SIG,,$@)
	$(Q)$(HOSTCC) $(shell $(HOSTPKG_CONFIG) --cflags libcrypto 2> /dev/null) \
		  $< -o $@ \
		  $(shell $(HOSTPKG_CONFIG) --libs libcrypto 2> /dev/null || echo -lcrypto)

$(OUTPUT)/sig.bin: $(OUTPUT)/insn.bin $(OUTPUT)/map.bin $(OUTPUT)/gen_sig
	$(OUTPUT)/gen_sig --key $(SIGNING_KEY) --cert $(SIGNING_CERT) \
		--data $(OUTPUT)/insn.bin --add $(OUTPUT)/map.bin --out $@

$(OUTPUT)/signed_loader.h: $(OUTPUT)/sig.bin $(OUTPUT)/loader.h
	$(SCRIPTSDIR)/write-sig.sh $(OUTPUT)/loader.h $(OUTPUT)/sig.bin > $@

BPFOBJ := $(OUTPUT)/libbpf/libbpf.a

$(OUTPUT)/libbpf:
	$(Q)mkdir -p $@

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile) \
	   $(APIDIR)/linux/bpf.h | $(OUTPUT)/libbpf
	$(Q)$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/libbpf/ \
		    DESTDIR=$(OUTPUT) prefix= \
		    $(BPFOBJ) install_headers

$(OUTPUT)/loader: loader.c $(OUTPUT)/signed_loader.h $(BPFOBJ)
	$(CC) $(CFLAGS) -I$(LIBDIR) -I$(APIDIR) -I$(OUTPUT) \
		$< $(BPFOBJ) -o $@ -lelf -lz


EXTRA_CLEAN = $(OUTPUT)/gen_sig $(OUTPUT)/libbpf

endif
