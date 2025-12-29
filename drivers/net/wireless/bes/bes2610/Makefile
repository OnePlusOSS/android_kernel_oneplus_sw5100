RWNX_VERS_NUM=6.10.0.0
KERN_DIR = /usr/src/$(shell uname -r)
KERN_DIR = /lib/modules/$(shell uname -r)/build
ARCH ?= $(shell arch)
CROSS_COMPILE ?=

# target platform select
#1.android platform ROCKCHIP
PLAT_ROCKCHIP ?= n
#2.android platform QIHUA
PLAT_QIHUA ?= n
#3.andorid paltrom sigmastar
PLAT_SIGMASTAR ?= n
#4.paltrom great-wall laptop
PLAT_LAPTOP ?= n
#5.platform fpga
PLAT_FPGA ?= n
#6.platform oppo watch
PLAT_OPPO ?= y

# adapt Android GKI kernel
ADPT_GKI_KERNEL ?= y
export CONFIG_WIFI_CALI = y
export CONFIG_BES_COEX_SUPPORT = y
export BES_POWER_CTRL_METHOD = 1
export NX_CHAN_CTXT_CNT = 4
export CONFIG_BES_MSPI = y
export CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED = 1
export CONFIG_DPD_CALI_DATA_SAVE = n
export CONFIG_BES_TXLOOP = y
export CONFIG_BES_TXLOOP_AUTO_WIFI_OFF = 1
export CONFIG_OPLUS_PLATFORM ?= y

ifeq ($(PLAT_FPGA),y)
CONFIG_WIFI_CALI ?= n
else
CONFIG_WIFI_CALI ?= y
endif

ifeq ($(PLAT_QIHUA),y)
KERN_DIR := $(ANDROID_BUILD_TOP)/../longan/kernel/linux-4.9
include $(ANDROID_BUILD_TOP)/../longan/.buildconfig
export ARCH := arm64
export CROSS_COMPILE=$(LICHEE_TOOLCHAIN_PATH)/bin/$(LICHEE_CROSS_COMPILER)-
endif

export CONFIG_BES_SDIO          ?= n
export CONFIG_BES_USB           ?= n
export CONFIG_RWNX_PCIE         ?= n
export CONFIG_BES_IBUS          ?= n
export CONFIG_BES_DSPI          ?= n
export CONFIG_BES_MSPI          ?= n

export CONFIG_PLATFORM_DEVICE_BUILTIN ?= n

ifeq ($(CONFIG_BES_USB),y)
export CONFIG_BES_BT ?= n
endif

CONFIG_BES_WIFI_BOOT_ON ?= n
CONFIG_BES_BT_BOOT_ON   ?= n

#0. not use power control
#1. power control of gpio
#2. power control of message
BES_POWER_CTRL_METHOD ?= 1

#0. driver control bt switch
#1. bt switch self managed
CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED ?= 1

# Save dpd data on host side
CONFIG_DPD_CALI_DATA_SAVE ?= y

ifeq ($(BES_POWER_CTRL_METHOD),1)
CONFIG_BES_POWER_CTRL_GPIO    := y
CONFIG_BES_POWER_CTRL         := y
GPIO_CHIP_POWER_DTS_NAME      ?= "BES2610,poweren_gpio"
GPIO_WAKEUP_WIFI_DTS_NAME     ?= "BES2610_WIFI,host_wakeup_wifi"
GPIO_WAKEUP_HOST_DTS_NAME     ?= "WIFI,host_wake_irq"
GPIO_BT_POWER_DTS_NAME        ?= "BES2610_BT,bt_power"
GPIO_WAKEUP_BT_DTS_NAME       ?= "BES2610_BT,host_wakeup_bt"
ifeq ($(CONFIG_RWNX_PCIE),y)
GPIO_CHIP_POWER_DTS_NAME      := "WIFI,poweren_gpio"
GPIO_WAKEUP_WIFI_DTS_NAME     := "BT,wake_host_irq"
GPIO_BT_POWER_DTS_NAME        := "BT,reset_gpio"
GPIO_WAKEUP_BT_DTS_NAME       := "BT,wake_gpio"
CONFIG_BES_WIFI_BOOT_ON       := y
GPIO_HOST_USED                := y
CONFIG_BES_WIFI_LP            := n
CONFIG_BES_WOWLAN             := y
CONFIG_BES_SLEEP_MANAGER      := y
endif
ifeq ($(CONFIG_BES_MSPI),y)
GPIO_CHIP_POWER_DTS_NAME      := "WIFI,poweren_gpio"
GPIO_WAKEUP_WIFI_DTS_NAME     :=
GPIO_WAKEUP_HOST_DTS_NAME     :=
GPIO_BT_POWER_DTS_NAME        :=
GPIO_WAKEUP_BT_DTS_NAME       :=
CONFIG_BES_WIFI_LP            := y
endif
else ifeq ($(BES_POWER_CTRL_METHOD),2)
ifeq ($(CONFIG_BES_USB),y)
CONFIG_BES_POWER_CTRL_MSG    := y
CONFIG_BES_POWER_CTRL        := y
else ifeq ($(CONFIG_BES_IBUS),y)
CONFIG_BES_POWER_CTRL_MSG    := y
CONFIG_BES_POWER_CTRL        := y
CONFIG_BES_WIFI_LP           ?= n
CONFIG_BES_IBUS_RPMSG        ?= y
else ifeq ($(CONFIG_BES_DSPI),y)
CONFIG_BES_POWER_CTRL_MSG    := y
CONFIG_BES_POWER_CTRL        := y
CONFIG_BES_WIFI_LP           ?= n
endif
endif

ifeq ($(CONFIG_BES_POWER_CTRL),y)
CONFIG_BES_WIFI_LP           ?= y
CONFIG_BES_IBUS_RPMSG        ?= n
CONFIG_BES_POWER_CTRL_ALLOW_SHUTDOWN ?= 1
endif

ifeq ($(CONFIG_BES_WIFI_LP),y)
CONFIG_BES_SLEEP_MANAGER     ?= y
CONFIG_BES_WOWLAN        ?= y
CONFIG_SCAN_BLOCK_SUSPEND ?= y
endif

## Select this if crda is already available in Android system
ifeq ($(PLAT_ROCKCHIP),y)
CONFIG_BES_CRDA_REGDOM        ?= n
endif

## Select this if FW is compiled with AMSDU Deaggregation
export WFROM_VER ?= 2
ifneq ($(WFROM_VER), 1)
CONFIG_BES_AMSDU_DEAGG ?= y
else
CONFIG_BES_AMSDU_DEAGG ?= n
endif

## Select whether to enable the tx loop mechanism
ifeq ($(CONFIG_RWNX_PCIE),n)
CONFIG_BES_TXLOOP ?= y
CONFIG_BES_TXLOOP_AUTO_WIFI_OFF ?= 1
endif

DUMP_FW_LOG_ENABLE        ?= y
ifeq ($(DUMP_FW_LOG_ENABLE), y)
# 1. Print directly through messages
# 2. Print via fifo (only for pcie)
# 3. Store in FIFO through messages and read from FIFO when an error occurs
# 4. Android loggr (TODO)
DUMP_FW_LOG_METHOD      ?= 1

ifeq ($(DUMP_FW_LOG_METHOD), 2)
CONFIG_FW_LOG_FIFO_DUMP := y
else ifeq ($(DUMP_FW_LOG_METHOD), 3)
CONFIG_FW_LOG_FIFO_DUMP := y
endif
else # DUMP_FW_LOG_ENABLE
DUMP_FW_LOG_METHOD      := 0
endif

# Rate Control Optimize
BES_RC_OPT ?= n

#CONFIG_RWNX_SOFTMAC ?= n
#CONFIG_RWNX_FHOST   ?= n
CONFIG_RWNX_FULLMAC  := y

# FW ARCH:
CONFIG_RWNX_SDM ?= n
CONFIG_RWNX_TL4 ?= n

# IPC version
CONFIG_RWNX_OLD_IPC ?= n

# VENDOR CMD
CONFIG_VENDOR_CMD ?= y

# WPA PRIVATE CMD
CONFIG_WPA_PRIV_CMD ?= y

# CUSTOM FILTER
CONFIG_CUSTOM_FILTER ?= y

# GTK OFFLOAD
CONFIG_GTK_OFFLOAD ?= n

# SCHED SCAN
CONFIG_SCHED_SCAN ?= y

# The kernel may also define this macro
CONFIG_DEBUG_FS ?= n

ifeq ($(CONFIG_DEBUG_FS),y)
CONFIG_RWNX_DEBUGFS     := y
# Support of Fw Dump DebugFS
CONFIG_RWNX_FW_DUMP     := n
# Support of Fw Trace DebugFS
CONFIG_RWNX_FW_TRACE    := n
# Support of P2P DebugFS for enabling/disabling NoA and OppPS
CONFIG_RWNX_P2P_DEBUGFS ?= y
# DEBUG OPTIONS
CONFIG_RWNX_UM_HELPER_DFLT ?= "/usr/bin/rwnx_umh.sh"
# reuse rate contorl config
CONFIG_BES_RC_CFG_SAVE ?= n
ifeq ($(CONFIG_BES_RC_CFG_SAVE),y)
BES_RC_CFG_SAVE := 1
else
BES_RC_CFG_SAVE := 0
endif
endif

#0.random macaddr
#1.rock api(fixed macaddr)
#2.get from flash
#3.get from User layer
GET_MAC_ADDR_METHOD      ?= 0

CONFIG_CALI_TIME         ?= 400

CONFIG_BES_NAPI ?= y
CONFIG_BES_GRO ?= y

# basic function
define boolen_flag
$(strip $(if $(findstring $($(1)),$(2)),-D$(1)))
endef

define string_flag
$(strip $(if $($(1)),-D$(1)=\"$($(1))\"))
endef

define value_flag
$(strip $(if $($(1)),-D$(1)=$($(1))))
endef

ifeq ($(CONFIG_BES_USB),y)
FW_DOWNLOAD_BY_USB                     ?= y
ifeq ($(FW_DOWNLOAD_BY_USB),y)
FW_DOWNLOAD_RAM_RUN_MOD                ?= n
endif
endif # CONFIG_BES_USB

ifeq ($(CONFIG_BES_SDIO),y)
FW_DOWNLOAD_BY_SDIO                    ?= y
ifeq ($(FW_DOWNLOAD_BY_SDIO),y)
FW_DOWNLOAD_RAM_RUN_MOD                ?= n
endif
SDIO_AGGR                              ?= y
CONFIG_SDIO_HOST_ADMA                  ?= n
endif # CONFIG_BES_SDIO

ifeq ($(CONFIG_RWNX_PCIE),y)
FW_DOWNLOAD_BY_PCIE                    ?= y
ifeq ($(PLAT_LAPTOP),y)
DPD_SAVE_DIR = "/lib/modules/$(shell uname -r)/updates/bes_dpd.txt"
endif
endif # CONFIG_RWNX_PCIE

ifeq ($(CONFIG_BES_IBUS),y)
FW_DOWNLOAD_BY_IBUS                    ?= y
ifeq ($(PLAT_LAPTOP),y)
DPD_SAVE_DIR = "/lib/modules/$(shell uname -r)/updates/bes_dpd.txt"
endif
endif # CONFIG_BES_IBUS

# FW VARS
NX_VIRT_DEV_MAX   := 4
NX_REMOTE_STA_MAX := 10
NX_MU_GROUP_MAX   := 62
NX_TX_MAX_RATES   := 4
NX_CHAN_CTXT_CNT  ?= 4

# Scan VARS
NX_SCAN_PROBE_INTERVAL  := 20000
NX_SCAN_PROBE_MAX_CNT   := 2

# Enable A-MSDU support (need FW support)
## Select this if FW is compiled with AMSDU support
CONFIG_RWNX_SPLIT_TX_BUF ?= y
## Select this TO send AMSDU
ifeq ($(CONFIG_RWNX_SPLIT_TX_BUF), y)
CONFIG_RWNX_AMSDUS_TX    ?= n
endif

# AMSDU TX values
# 0: AMSDU configured as recommended by peer
# 1: enable
# 2: disable
RWNX_AMSDU_TX_VAL       ?= 0

# Enable BFMER support (need FW support)
CONFIG_RWNX_BFMER ?= n

# Support of MU-MIMO transmission (need FW support)
ifeq ($(CONFIG_RWNX_BFMER), y)
CONFIG_RWNX_MUMIMO_TX ?= n
else
CONFIG_RWNX_MUMIMO_TX = n
endif

ifeq ($(CONFIG_RWNX_MUMIMO_TX), y)
CONFIG_USER_MAX  := 2
else
CONFIG_USER_MAX  := 1
endif

# Enable handling of radar event
CONFIG_RWNX_RADAR ?= y

# Enable HW queue for Broadcast/Multicast traffic (need FW support)
CONFIG_RWNX_BCMC ?= y
ifeq ($(CONFIG_RWNX_BCMC), y)
NX_TXQ_CNT       := 5
else
NX_TXQ_CNT       := 4
endif

# Enable Monitor+Data interface support (need FW support)
CONFIG_RWNX_MON_DATA ?= n

# extra DEBUG config
CONFIG_RWNX_SW_PROFILING ?= n
CONFIG_BES_DBG ?= n

# Enable P2P interface create
CONFIG_BES_P2P_MULTIVIF ?= n

# Channel BW of 2G
CONFIG_2G_20M_ONLY ?= n

#0. maximum negotiation rate
#1. the first rate of highest throughput
#2. the rate of the highest probability
CONFIG_BES_GET_TX_RATE_TYPE ?= 0

# Send eapol/dhcp with the lowest rate
CONFIG_BES_TX_RATE_LIMIT ?= y

# The maximum number of software retransmissions.
SW_RETRY_CNT_MAX := 6

# Control the key frame retransmission function.
KEY_FRAME_RETRY ?= y

# STA connect retry count
STA_CONN_RETRY_CNT ?= 1

# Use add/remove to notify exception for adapting mdev
CONFIG_BES_MDEV_SUPPORT ?= n

# wifi and bt coexistence support
CONFIG_BES_COEX_SUPPORT ?= y

# wifi and bt coexistence hardware mode
# 0: tdd mode(single antenna)
# 1: fdd mode(dual antennas)
BES_COEX_HW_MODE  ?= 0

# VHT MCS map value  0: MCS0_7, 1: MCS0_8, 2: MCS0_9(Default: 2)
CONFIG_VHT_MCS_MAP ?= 2
# HE MCS map value  0: MCS0_7, 1: MCS0_9, 2: MCS0_11(Default: 2)
CONFIG_HE_MCS_MAP ?= 2

# csi_cap support
CONFIG_RWNX_CSI_CAP ?= n

VENDOR_HAL_VERSION_LEVEL    ?= 1
VENDOR_HAL_VERSION_SUBLEVEL ?= 6

# tx/rx support non_linear skb and reduce data copy numbers
CONFIG_TRX_COPY_LESS ?= n

ifneq ($(CONFIG_WIRELESS_EXT),)
BES_WIRELESS_EXT ?= y
endif

BES_NO_REBOOT_CHG_FW    ?= n
ifeq ($(CONFIG_BES_MSPI),y)
BES_NO_REBOOT_CHG_FW    := y
endif

# Notify the changed mac to WPA
CONFIG_NOTIFY_CHANGED_MAC ?= y

# Support BES FW error dump
CONFIG_BES_FW_ERR_DUMP ?= y

#support update power table by country code
CONFIG_TX_PWR_TAB_CTRY_CODE ?= y

# Support throughput debugging
CONFIG_BES_TP_DBG ?= y

# Throughput debug feature support
# bit0: Throughput tx/rx rate
# bit1: Coex state
# bit2: tx/rx rate
# bit3: ipc delay statistics
# bit4: tx delay statistics
ifeq ($(CONFIG_BES_TP_DBG), y)
CONFIG_BES_TP_DBG_FEATURE_SUPPORT ?= 0x1f
endif

#0: NULL frame keepalive
#1：arp keepalive
CONFIG_BES_ARP_KEEP_ALIVE ?= 0

ccflags-y += $(call boolen_flag,PLAT_ROCKCHIP,y)
ccflags-y += $(call boolen_flag,PLAT_QIHUA,y)
ccflags-y += $(call boolen_flag,PLAT_SIGMASTAR,y)
ccflags-y += $(call boolen_flag,PLAT_LAPTOP,y)
ccflags-y += $(call boolen_flag,PLAT_FPGA,y)
ccflags-y += $(call boolen_flag,CONFIG_WIFI_CALI,y)
ccflags-y += $(call value_flag,GET_MAC_ADDR_METHOD)
ccflags-y += $(call value_flag,DUMP_FW_LOG_METHOD)
ccflags-y += $(call value_flag,CONFIG_CALI_TIME)

ccflags-y += $(call boolen_flag,CONFIG_RWNX_FULLMAC,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_SDIO,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_USB,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_PCIE,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_IBUS,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_DSPI,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_MSPI,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_BT,y)
ccflags-y += $(call boolen_flag,FW_DOWNLOAD_BY_USB,y)
ccflags-y += $(call boolen_flag,FW_DOWNLOAD_BY_SDIO,y)
ccflags-y += $(call boolen_flag,FW_DOWNLOAD_BY_PCIE,y)
ccflags-y += $(call boolen_flag,FW_DOWNLOAD_BY_IBUS,y)
ccflags-y += $(call boolen_flag,FW_DOWNLOAD_RAM_RUN_MOD,y)
ccflags-y += $(call boolen_flag,SDIO_AGGR,y)
ccflags-y += $(call boolen_flag,CONFIG_SDIO_HOST_ADMA,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_AMSDU_DEAGG,y)
ccflags-y += $(call value_flag,WFROM_VER)
ccflags-y += $(call boolen_flag,CONFIG_PLATFORM_DEVICE_BUILTIN,y)
ccflags-y += $(call boolen_flag,CONFIG_DPD_CALI_DATA_SAVE,y)

# power control
ccflags-y += $(call boolen_flag,CONFIG_BES_WIFI_BOOT_ON,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_BT_BOOT_ON,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_POWER_CTRL,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_POWER_CTRL_GPIO,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_CRDA_REGDOM,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_WIFI_LP,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_WOWLAN,y)
ccflags-y += $(call boolen_flag,CONFIG_SCAN_BLOCK_SUSPEND,y)
ccflags-y += $(call string_flag,GPIO_CHIP_POWER_DTS_NAME)
ccflags-y += $(call string_flag,GPIO_WAKEUP_WIFI_DTS_NAME)
ccflags-y += $(call string_flag,GPIO_WAKEUP_HOST_DTS_NAME)
ccflags-y += $(call string_flag,GPIO_BT_POWER_DTS_NAME)
ccflags-y += $(call string_flag,GPIO_WAKEUP_BT_DTS_NAME)
ccflags-y += $(call boolen_flag,CONFIG_BES_POWER_CTRL_MSG,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_IBUS_RPMSG,y)
ccflags-y += $(call value_flag,CONFIG_BES_POWER_CTRL_ALLOW_SHUTDOWN)
ccflags-y += $(call value_flag,CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED)

ccflags-y += $(call boolen_flag,DUMP_FW_LOG_ENABLE,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_TXLOOP,y)
ccflags-y += $(call value_flag,CONFIG_BES_TXLOOP_AUTO_WIFI_OFF,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_SDM,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_TL4,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_OLD_IPC,y)

ccflags-y += $(call boolen_flag,CONFIG_VENDOR_CMD,y)
ccflags-y += $(call boolen_flag,CONFIG_WPA_PRIV_CMD,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_P2P_DEBUGFS,y)

ccflags-y += $(call boolen_flag,CONFIG_RWNX_DEBUGFS,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_FW_DUMP,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_FW_TRACE,y)
ccflags-y += $(call string_flag,CONFIG_RWNX_UM_HELPER_DFLT)
ccflags-y += $(call value_flag,BES_RC_CFG_SAVE)
ccflags-y += $(call boolen_flag,CONFIG_BES_DBG,y)

ccflags-y += $(call value_flag,NX_VIRT_DEV_MAX)
ccflags-y += $(call value_flag,NX_REMOTE_STA_MAX)
ccflags-y += $(call value_flag,NX_MU_GROUP_MAX)
ccflags-y += $(call value_flag,NX_TX_MAX_RATES)
ccflags-y += $(call value_flag,NX_CHAN_CTXT_CNT)

ccflags-y += $(call value_flag,NX_SCAN_PROBE_INTERVAL)
ccflags-y += $(call value_flag,NX_SCAN_PROBE_MAX_CNT)

ccflags-y += $(call boolen_flag,CONFIG_RWNX_SPLIT_TX_BUF,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_AMSDUS_TX,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_BFMER,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_MUMIMO_TX,y)

ccflags-y += $(call boolen_flag,CONFIG_RWNX_RADAR,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_MON_DATA,y)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_SW_PROFILING,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_P2P_MULTIVIF,y)
ccflags-y += $(call boolen_flag,CONFIG_2G_20M_ONLY,y)

ccflags-y += $(call boolen_flag,CONFIG_CUSTOM_FILTER,y)
ccflags-y += $(call boolen_flag,CONFIG_GTK_OFFLOAD,y)

ccflags-y += $(call boolen_flag,CONFIG_SCHED_SCAN,y)

ccflags-y += $(call value_flag,CONFIG_USER_MAX)
ccflags-y += $(call value_flag,NX_TXQ_CNT)
ccflags-y += $(call value_flag,CONFIG_BES_GET_TX_RATE_TYPE)

ccflags-y += $(call boolen_flag,CONFIG_BES_TX_RATE_LIMIT,y)
ccflags-y += $(call boolen_flag,CONFIG_FW_LOG_FIFO_DUMP,y)
ccflags-y += $(call boolen_flag,BES_RC_OPT,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_MDEV_SUPPORT,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_COEX_SUPPORT,y)
ccflags-y += $(call boolen_flag,GPIO_HOST_USED,y)
ccflags-y += $(call value_flag,BES_COEX_HW_MODE)
ccflags-y += $(call boolen_flag,CONFIG_RWNX_CSI_CAP,y)

ccflags-y += $(call boolen_flag,CONFIG_BES_NAPI,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_GRO,y)

ccflags-y += $(call cc-disable-warning,implicit-fallthrough)

ccflags-y += $(call string_flag,DPD_SAVE_DIR)

ccflags-y += $(call value_flag,CONFIG_VHT_MCS_MAP)
ccflags-y += $(call value_flag,CONFIG_HE_MCS_MAP)
ccflags-y += $(call value_flag,RWNX_AMSDU_TX_VAL)
ccflags-y += $(call value_flag,VENDOR_HAL_VERSION_LEVEL)
ccflags-y += $(call value_flag,VENDOR_HAL_VERSION_SUBLEVEL)

ccflags-y += $(call boolen_flag,CONFIG_TRX_COPY_LESS,y)
ccflags-y += $(call boolen_flag,ADPT_GKI_KERNEL,y)

ccflags-y += $(call boolen_flag,BES_WIRELESS_EXT,y)
ccflags-y += $(call boolen_flag,BES_NO_REBOOT_CHG_FW,y)
ccflags-y += $(call boolen_flag,CONFIG_OPLUS_PLATFORM,y)

ccflags-y += $(call value_flag,SW_RETRY_CNT_MAX)
ccflags-y += $(call boolen_flag,KEY_FRAME_RETRY,y)

ccflags-y += $(call value_flag,STA_CONN_RETRY_CNT)

ccflags-y += $(call boolen_flag,CONFIG_NOTIFY_CHANGED_MAC,y)
ccflags-y += $(call boolen_flag,CONFIG_BES_FW_ERR_DUMP,y)
ccflags-y += $(call boolen_flag,CONFIG_TX_PWR_TAB_CTRY_CODE,y)

ccflags-y += $(call boolen_flag,CONFIG_BES_TP_DBG,y)
ccflags-y += $(call value_flag,CONFIG_BES_TP_DBG_FEATURE_SUPPORT)

ccflags-y += $(call value_flag,CONFIG_BES_ARP_KEEP_ALIVE)

obj-m := bes2610.o
bes2610-y :=   rwnx_msg_tx.o          \
               rwnx_msg_rx.o          \
               rwnx_utils.o           \
               rwnx_cmds.o            \
               rwnx_cfgfile.o         \
               rwnx_strs.o            \
               rwnx_rx.o              \
               rwnx_tx.o              \
               rwnx_txq.o             \
               rwnx_main.o            \
               rwnx_mod_params.o      \
               rwnx_mesh.o            \
               rwnx_platform.o        \
               ipc_host.o             \
               rwnx_tdls.o            \
               hal_desc.o             \
               bes_sysfs.o            \
               rwnx_testmode.o        \
               bes_log.o              \
               bes_common.o           \
               bes_filter_map.o       \
               lmac_log_ctrl.o        \
               bes_vif_info.o         \
               bes_eapol.o            \
               bes_dbg.o              \
               bes_linkinfo.o         \
               bes_filter.o           \
               bes_filter_manager.o   \
               bes_regd.o             \
               bes_data_monitor.o     \
               bes_phy.o              \
               bes_conn_state.o       \
               bes_signal_test.o


bes2610-$(CONFIG_RWNX_PCIE)        += rwnx_pci.o
bes2610-$(CONFIG_RWNX_PCIE)        += rwnx_dini.o
bes2610-$(CONFIG_RWNX_PCIE)        += rwnx_v7.o
bes2610-$(CONFIG_RWNX_PCIE)        += rwnx_irqs.o
bes2610-$(CONFIG_BES_IBUS)         += bes_ibus.o
bes2610-$(CONFIG_BES_IBUS)         += bes_soc.o
bes2610-$(CONFIG_BES_IBUS)         += rwnx_irqs.o
bes2610-$(CONFIG_BES_USB)          += bes_psa.o
bes2610-$(CONFIG_BES_USB)          += bes_usb.o
bes2610-$(CONFIG_BES_BT)           += bes_btusb.o
bes2610-$(CONFIG_BES_SDIO)         += bes_psa.o
bes2610-$(CONFIG_BES_SDIO)         += bes_sdio.o
bes2610-$(CONFIG_BES_DSPI)         += bes_psa.o
bes2610-$(CONFIG_BES_DSPI)         += bes_dspi.o
bes2610-$(CONFIG_BES_MSPI)         += bes_psa.o
bes2610-$(CONFIG_BES_MSPI)         += bes_mspi.o
bes2610-$(CONFIG_RWNX_RADAR)       += rwnx_radar.o
bes2610-$(CONFIG_DEBUG_FS)         += rwnx_debugfs.o
bes2610-$(CONFIG_RWNX_FW_DUMP)     += rwnx_fw_dump.o
bes2610-$(CONFIG_RWNX_FW_TRACE)    += rwnx_fw_trace.o
bes2610-$(CONFIG_RWNX_BFMER)       += rwnx_bfmer.o
bes2610-$(CONFIG_RWNX_MUMIMO_TX)   += rwnx_mu_group.o
bes2610-$(FW_DOWNLOAD_BY_USB)      += bes_fw_usb.o bes_fw_common.o
bes2610-$(FW_DOWNLOAD_BY_SDIO)     += bes_fw_sdio.o bes_fw_common.o
bes2610-$(FW_DOWNLOAD_BY_PCIE)     += bes_fw_pci.o bes_fw_common.o
bes2610-$(FW_DOWNLOAD_BY_IBUS)     += bes_fw_ibus.o bes_fw_common.o
bes2610-$(CONFIG_BES_POWER_CTRL)   += bes_power_ctrl.o
bes2610-$(CONFIG_BES_POWER_CTRL_GPIO) += bes_power_ctrl_gpio.o
bes2610-$(CONFIG_VENDOR_CMD)       += bes_cfgvendor.o
bes2610-$(CONFIG_WPA_PRIV_CMD)     += bes_wpa_priv_cmd.o
bes2610-$(CONFIG_BES_WIFI_LP)      += bes_pwr.o
bes2610-$(CONFIG_BES_WOWLAN)       += bes_wowlan.o
bes2610-$(CONFIG_BES_SLEEP_MANAGER)   += bes_sleep_manager.o
bes2610-$(CONFIG_BES_POWER_CTRL_MSG)  += bes_power_ctrl_msg.o
bes2610-$(CONFIG_BES_IBUS)         += bes_ibus_rpmsg.o
bes2610-$(CONFIG_CUSTOM_FILTER)    += bes_custom_filter.o
bes2610-$(CONFIG_GTK_OFFLOAD)      += bes_gtk_offload.o
bes2610-$(CONFIG_SCHED_SCAN)       += bes_sched_scan.o
bes2610-$(CONFIG_BES_TXLOOP)       += bes_tx_loop.o
bes2610-$(CONFIG_FW_LOG_FIFO_DUMP) += bes_fw_log_fifo_dump.o
bes2610-$(CONFIG_BES_COEX_SUPPORT) += bes_coex_wifi.o bes_coex_bt.o bes_coex_arb.o
bes2610-$(BES_WIRELESS_EXT)        += bes_iw_priv_cmd.o
bes2610-$(CONFIG_RWNX_CSI_CAP)     += bes_csi.o
bes2610-$(CONFIG_WIFI_CALI)        += bes_dbac_cali.o
bes2610-$(CONFIG_BES_FW_ERR_DUMP)  += bes_error_dbg.o
bes2610-$(CONFIG_OPLUS_PLATFORM)   += oplus_custom_wlan.o
bes2610-$(CONFIG_OPLUS_PLATFORM)   += oplus_wlan_log_drv.o
bes2610-$(CONFIG_BES_TP_DBG)       += bes_tp_dbg.o

ccflags-y += -I$(src)

all: modules

install:
	$(MAKE) -C $(KERN_DIR) M=$(shell pwd) INSTALL_MOD_PATH=$(INSTALL_DIR) modules_install

modules clean:
	@$(PWD)/mklink.sh
	@$(PWD)/mkvers.sh
	$(MAKE) -C $(KERN_DIR) M=$(shell pwd) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) $@
	@$(PWD)/mklink.sh clean
