dtb-$(CONFIG_ARCH_VEXPRESS) += rtsm_ve-aemv8a.dtb foundation-v8.dtb \
				fvp-base-gicv2-psci.dtb
dtb-$(CONFIG_ARCH_XGENE) += apm-mustang.dtb
dtb-$(CONFIG_ARCH_VEXPRESS) += juno.dtb

dtb-y += amlogic/gxbb_p200.dtb amlogic/odroidc2.dtb

targets += dtbs

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
targets += $(DTB_LIST)

prep:
	mkdir -p arch/$(ARCH)/boot/dts/amlogic

dtbs: prep $(addprefix $(obj)/, $(DTB_LIST))

clean-files := dts/*.dtb *.dtb
