#
# Makefile for oplus's custom devices
#
#obj-y += oplus/osetup.o //Placed in the upper layer makefile.
obj-y += boot/

obj-y += snshub/
#obj-y += dap_swd/
obj-$(CONFIG_OPLUS_CROWN) += oplus_crown/
obj-y += startup_monitor/


obj-m += oplus_switch.o
obj-$(CONFIG_AW86927_HAPTIC)+= aw86927_haptic/

#obj-y += oplus_shutdown_detect.o
obj-m += oplus_shutdown_detect.o

obj-m += oplus_shutdown.o

obj-m += oplus_ddr_freq.o

obj-m += net/
#obj-y += feedback/

obj-$(CONFIG_OPLUS_BLUESLEEP) += bluesleep.o
