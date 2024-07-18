// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MHI PCI driver - MHI over PCI controller driver
 *
 * This module is a generic driver for registering MHI-over-PCI devices,
 * such as PCIe QCOM modems.
 *
 * Copyright (C) 2020 Linaro Ltd <loic.poulain@linaro.org>
 */

#include <linux/aer.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/local_mhi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define LASSEN_V1_DEVICE_ID 0x600
#define LASSEN_V2_DEVICE_ID 0x601

#define MHI_PCI_DEFAULT_BAR_NUM 0

#define MHI_POST_RESET_DELAY_MS 500

#define HEALTH_CHECK_PERIOD (HZ * 2)
#define CRASH_CHECK_PERIOD (HZ * 60)

/**
 * struct mhi_pci_dev_info - MHI PCI device specific information
 * @config: MHI controller configuration
 * @vf_config: MHI controller configuration for VF (optional)
 * @name: name of the PCI module
 * @fw: firmware path (if any)
 * @edl: emergency download mode firmware path (if any)
 * @bar_num: PCI base address register to use for MHI MMIO register space
 * @sideband_wake: Devices using dedicated sideband GPIO for wakeup instead
 *		   of inband wake support (such as sdx24)
 * @auto_edl_load: Do not wait for sysfs triggers to proceed with image download
 * 		   in Emergency Download Mode
 * @allow_user_soc_reset: Enable SYS entry to perform mhi_reset (SOC reset) to perform
 * 		  device to reboot
 * @reset_on_driver_unbind: Set true for devices support SOC reset and perform it when
 * 		     unbinding driver
 */
struct mhi_pci_dev_info {
	const struct mhi_controller_config *config;
	const struct mhi_controller_config *vf_config;
	const char *name;
	const char *fw;
	const char *edl;
	unsigned int bar_num;
	bool sideband_wake;
	bool auto_edl_load;
	unsigned int max_vfs;
	bool allow_user_soc_reset;
	bool reset_on_driver_unbind;
};

#define MHI_CHANNEL_CONFIG_UL(ch_num, ch_name, elems, ev_ring, ee,	\
			      dbmode, lpm, poll, overflow_disable, offload, modeswitch,	\
			      ch_type)					\
	{								\
		.dir = DMA_TO_DEVICE,					\
		.num = ch_num,						\
		.name = ch_name,					\
		.num_elements = elems,					\
		.event_ring = ev_ring,					\
		.ee_mask = BIT(ee),					\
		.pollcfg = poll,					\
		.ovf_disable = overflow_disable,			\
		.doorbell = dbmode,					\
		.lpm_notify = lpm,					\
		.offload_channel = offload,				\
		.doorbell_mode_switch = modeswitch,			\
		.wake_capable = false,					\
		.auto_queue = false,					\
		.local_elements = 0,					\
		.type = ch_type,					\
	}

#define MHI_CHANNEL_CONFIG_DL(ch_num, ch_name, elems, ev_ring, ee,	\
			      dbmode, lpm, poll, overflow_disable, offload, modeswitch,	\
			      wake, autoq, local_el, ch_type)		\
	{								\
		.dir = DMA_FROM_DEVICE,					\
		.num = ch_num,						\
		.name = ch_name,					\
		.num_elements = elems,					\
		.event_ring = ev_ring,					\
		.ee_mask = BIT(ee),					\
		.pollcfg = poll,					\
		.ovf_disable = overflow_disable,			\
		.doorbell = dbmode,					\
		.lpm_notify = lpm,					\
		.offload_channel = offload,				\
		.doorbell_mode_switch = modeswitch,			\
		.wake_capable = wake,					\
		.auto_queue = autoq,					\
		.local_elements = local_el,				\
		.type = ch_type,					\
	}

#define MHI_EVENT_CONFIG(ev_ring, ev_irq, type, num_elems, int_mod,	\
			 dbmode, hw, cl_manage, offload, ch_num)	\
	{								\
		.num_elements = num_elems,				\
		.irq_moderation_ms = int_mod,				\
		.irq = ev_irq,						\
		.mode = dbmode,						\
		.data_type = type,					\
		.hardware_event = hw,					\
		.client_managed = cl_manage,				\
		.offload_channel = offload,				\
		.channel = ch_num,					\
	}

#define MHI_CONTROLLER_CONFIG(max_chan, timeout, dma_width, num_chan,	\
			      chan_cfg, num_ev, ev_cfg)	        	\
	{								\
		.max_channels = max_chan,				\
		.timeout_ms = timeout,					\
		.dma_data_width = dma_width,				\
		.num_channels = num_chan,				\
		.ch_cfg = chan_cfg,					\
		.num_events = num_ev,					\
		.event_cfg = ev_cfg,					\
	}

struct mhi_channel_config modem_qcom_mhi_lsn_600_pf_channels[] = {
	/* SBL channels  */
	MHI_CHANNEL_CONFIG_UL(2, "SAHARA", 128, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0,false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(3, "SAHARA", 128, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(25, "BL", 32, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	/* AMSS channels */
	MHI_CHANNEL_CONFIG_UL(0, "LOOPBACK", 64, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(1, "LOOPBACK", 64, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(4, "DIAG", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(5, "DIAG", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(9, "QDSS", 1024, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(14, "NMEA", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(15, "NMEA", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(16, "CSM_CTRL", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(17, "CSM_CTRL", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	/* added for TSC TSYNC */
	MHI_CHANNEL_CONFIG_UL(40, "MHI_PHC", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(41, "MHI_PHC", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	/* SW IP channels */
	MHI_CHANNEL_CONFIG_UL(46, "IP_SW0", 256, 5, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(47, "IP_SW0", 256, 5, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(48, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(49, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	/* S-plane channel */
	MHI_CHANNEL_CONFIG_UL(50, "IP_SW3", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(51, "IP_SW3", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),

};

struct mhi_channel_config modem_qcom_mhi_lsn_601_pf_channels[] = {
	/* SBL channels  */
	MHI_CHANNEL_CONFIG_UL(2, "SAHARA", 128, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0,false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(3, "SAHARA", 128, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(25, "BL", 32, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	/* AMSS channels */
	MHI_CHANNEL_CONFIG_UL(0, "LOOPBACK", 64, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(1, "LOOPBACK", 64, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(4, "DIAG", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(5, "DIAG", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(9, "QDSS", 1024, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(14, "NMEA", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(15, "NMEA", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(16, "CSM_CTRL", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(17, "CSM_CTRL", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	/* added for TSC TSYNC */
	MHI_CHANNEL_CONFIG_UL(40, "MHI_PHC", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(41, "MHI_PHC", 32, 4, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),

	/* SW IP channels */
	MHI_CHANNEL_CONFIG_UL(46, "IP_SW0", 256, 5, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(47, "IP_SW0", 256, 5, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(48, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(49, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),

	/* S-plane channel */
	MHI_CHANNEL_CONFIG_UL(52, "IP_SW3", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(53, "IP_SW3", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),

	/* FH LTE broadcast support */
	MHI_CHANNEL_CONFIG_UL(54, "IP_SW4", 128, 8, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(55, "IP_SW4", 128, 8, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(56, "IP_SW5", 128, 9, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(57, "IP_SW5", 128, 9, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(58, "IP_SW6", 128, 10, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(59, "IP_SW6", 128, 10, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(60, "IP_SW7", 128, 11, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(61, "IP_SW7", 128, 11, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(62, "IP_SW8", 128, 12, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(63, "IP_SW8", 128, 12, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(64, "IP_SW9", 128, 13, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(65, "IP_SW9", 128, 13, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(66, "IP_SW10", 128, 14, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(67, "IP_SW10", 128, 14, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(68, "IP_SW11", 128, 15, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(69, "IP_SW11", 128, 15, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(70, "IP_SW12", 128, 16, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(71, "IP_SW12", 128, 16, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(72, "IP_SW13", 128, 17, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(73, "IP_SW13", 128, 17, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(74, "IP_SW14", 128, 18, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(75, "IP_SW14", 128, 18, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(76, "IP_SW15", 128, 19, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(77, "IP_SW15", 128, 19, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
};

struct mhi_channel_config modem_qcom_mhi_lsn_600_vf_fapi_channels[] = {
	/* SW IP channels */
	MHI_CHANNEL_CONFIG_UL(48, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(49, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(50, "IP_SW2", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(51, "IP_SW2", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),

	/* HW channels */
	MHI_CHANNEL_CONFIG_UL(104, "IP_HW0", 2048, 8, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(105, "IP_HW0", 2048, 9, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, true,  false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(106, "IP_HW1", 2048, 10, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(107, "IP_HW1", 2048, 11, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, true, false, false,
			      false, false, 0, 0),
};

struct mhi_channel_config modem_qcom_mhi_lsn_601_swip_channels[] = {
	/* SW IP channels */
	MHI_CHANNEL_CONFIG_UL(48, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(49, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(50, "IP_SW2", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(51, "IP_SW2", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
};

/* FH LTE channel configuration for Lassen V2 */
struct mhi_channel_config modem_qcom_mhi_lsn_601_fh_lte_channels[] = {
	/* SW IP channels */
	MHI_CHANNEL_CONFIG_UL(48, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(49, "IP_SW1", 128, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(50, "IP_SW2", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(51, "IP_SW2", 128, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),

	/* HW channels for FH LTE */
	MHI_CHANNEL_CONFIG_UL(104, "IP_HW2", 2048, 8, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(105, "IP_HW2", 2048, 9, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(106, "IP_HW3", 2048, 10, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(107, "IP_HW3", 2048, 11, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(108, "IP_HW4", 2048, 12, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(109, "IP_HW4", 2048, 13, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(110, "IP_HW5", 2048, 14, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(111, "IP_HW5", 2048, 15, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(112, "IP_HW6", 2048, 16, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(113, "IP_HW6", 2048, 17, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(114, "IP_HW7", 2048, 18, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(115, "IP_HW7", 2048, 19, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(116, "IP_HW8", 2048, 20, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(117, "IP_HW8", 2048, 21, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(118, "IP_HW9", 2048, 22, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(119, "IP_HW9", 2048, 23, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(120, "IP_HW10", 2048, 24, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(121, "IP_HW10", 2048, 25, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(122, "IP_HW11", 2048, 26, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(123, "IP_HW11", 2048, 27, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(124, "IP_HW12", 2048, 28, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(125, "IP_HW12", 2048, 29, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(126, "IP_HW13", 2048, 30, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(127, "IP_HW13", 2048, 31, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, false,
			      false, false, 0, 0),
};

static struct mhi_event_config modem_qcom_mhi_lsn_600_pf_events[] = {
	MHI_EVENT_CONFIG(0, 1, MHI_ER_CTRL, 64, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(1, 2, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(2, 3, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(3, 4, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(4, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(5, 6, MHI_ER_DATA, 512, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(6, 7, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(7, 8, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
};

/* Lassen v2-601 PF events */
static struct mhi_event_config modem_qcom_mhi_lsn_601_pf_events[] = {
	MHI_EVENT_CONFIG(0, 1, MHI_ER_CTRL, 64, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(1, 2, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(2, 3, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(3, 4, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(4, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(5, 6, MHI_ER_DATA, 512, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(6, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(7, 6, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(8, 7, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(9, 8, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(10, 9, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(11, 10, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(12, 11, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(13, 12, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(14, 13, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(15, 14, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(16, 15, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(17, 16, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(18, 17, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(19, 18, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
};

static struct mhi_event_config modem_qcom_mhi_lsn_600_vf_events[] = {
	MHI_EVENT_CONFIG(0, 1, MHI_ER_CTRL, 64, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(1, 2, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(2, 3, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(3, 4, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(4, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(5, 6, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(6, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(7, 6, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(8, 7, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 104),
	MHI_EVENT_CONFIG(9, 8, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, true, false, 105),
	MHI_EVENT_CONFIG(10, 9, MHI_ER_DATA, 4096, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 106),
	MHI_EVENT_CONFIG(11, 10, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 107),
};

static struct mhi_event_config modem_qcom_mhi_lsn_601_vf_swip_events[] = {
	MHI_EVENT_CONFIG(0, 1, MHI_ER_CTRL, 64, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(1, 2, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(2, 3, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(3, 4, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(4, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(5, 6, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(6, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(7, 6, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
};

/* Lassen V2 event ring configuration FH LTE HWIP channels */
static struct mhi_event_config modem_qcom_mhi_lsn_601_vf_fh_lte_events[] = {
	MHI_EVENT_CONFIG(0, 1, MHI_ER_CTRL, 64, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(1, 2, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(2, 3, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(3, 4, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(4, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(5, 6, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(6, 5, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(7, 6, MHI_ER_DATA, 256, 0, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(8, 7, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 104),
	MHI_EVENT_CONFIG(9, 8, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 105),
	MHI_EVENT_CONFIG(10, 9, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 106),
	MHI_EVENT_CONFIG(11, 10, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 107),
	MHI_EVENT_CONFIG(12, 11, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 108),
	MHI_EVENT_CONFIG(13, 12, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 109),
	MHI_EVENT_CONFIG(14, 13, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 110),
	MHI_EVENT_CONFIG(15, 14, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 111),
	MHI_EVENT_CONFIG(16, 15, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 112),
	MHI_EVENT_CONFIG(17, 16, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 113),
	MHI_EVENT_CONFIG(18, 17, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 114),
	MHI_EVENT_CONFIG(19, 18, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 115),
	MHI_EVENT_CONFIG(20, 19, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 116),
	MHI_EVENT_CONFIG(21, 20, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 117),
	MHI_EVENT_CONFIG(22, 21, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 118),
	MHI_EVENT_CONFIG(23, 22, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 119),
	MHI_EVENT_CONFIG(24, 23, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 120),
	MHI_EVENT_CONFIG(25, 24, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 121),
	MHI_EVENT_CONFIG(26, 25, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 122),
	MHI_EVENT_CONFIG(27, 26, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 123),
	MHI_EVENT_CONFIG(28, 27, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 124),
	MHI_EVENT_CONFIG(29, 28, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 125),
	MHI_EVENT_CONFIG(30, 29, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 126),
	MHI_EVENT_CONFIG(31, 30, MHI_ER_DATA, 2048, 0, MHI_DB_BRST_DISABLE,
			 true, false, false, 127),
};

/* Lassen V1 mhi controller configs for PF */
static const struct mhi_controller_config modem_qcom_lsn_600_pf_config[] = {
	MHI_CONTROLLER_CONFIG(128, 120000, 32,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_pf_channels),
			      modem_qcom_mhi_lsn_600_pf_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_pf_events),
			      modem_qcom_mhi_lsn_600_pf_events),
};

/* Lassen V2 mhi controller configs for PF */
static const struct mhi_controller_config modem_qcom_lsn_601_pf_config[] = {
	MHI_CONTROLLER_CONFIG(128, 120000, 32,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_pf_channels),
			      modem_qcom_mhi_lsn_601_pf_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_pf_events),
			      modem_qcom_mhi_lsn_601_pf_events),
};

/* Lassen V1 mhi controller configs for VF's, V1 has 4 VF's */
static const struct mhi_controller_config modem_qcom_lsn_600_vf_config[] = {
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),
};

/**
 * Lassen V2 mhi controller configs for VF's, V2 has 16 VF's
 * First array controller index maps to VF-1, followed till 16 Vf's
 */
static const struct mhi_controller_config modem_qcom_lsn_601_vf_config[] = {
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_fapi_channels),
			      modem_qcom_mhi_lsn_600_vf_fapi_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_600_vf_events),
			      modem_qcom_mhi_lsn_600_vf_events),

	/* Lassen V2 VF:5/8/9 has both SWIP channels and FH LTE HWIP channels */
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_fh_lte_channels),
			      modem_qcom_mhi_lsn_601_fh_lte_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_fh_lte_events),
			      modem_qcom_mhi_lsn_601_vf_fh_lte_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_fh_lte_channels),
			      modem_qcom_mhi_lsn_601_fh_lte_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_fh_lte_events),
			      modem_qcom_mhi_lsn_601_vf_fh_lte_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_fh_lte_channels),
			      modem_qcom_mhi_lsn_601_fh_lte_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_fh_lte_events),
			      modem_qcom_mhi_lsn_601_vf_fh_lte_events),

	/* Lassen V2 VF:10 to 16 has only SWIP channels */
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
	MHI_CONTROLLER_CONFIG(128, 120000, 40,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_swip_channels),
			      modem_qcom_mhi_lsn_601_swip_channels,
			      ARRAY_SIZE(modem_qcom_mhi_lsn_601_vf_swip_events),
			      modem_qcom_mhi_lsn_601_vf_swip_events),
};

static struct mhi_pci_dev_info mhi_qcom_lassen_v1_info = {
	.name = "qcom-lassen",
	.fw = "qcom/lassen/xbl_s.melf",
	.edl = "qcom/lassen/edl.mbn",
	.config = modem_qcom_lsn_600_pf_config,
	.vf_config = modem_qcom_lsn_600_vf_config,
	.bar_num = MHI_PCI_DEFAULT_BAR_NUM,
	.sideband_wake = false,
	.max_vfs = ARRAY_SIZE(modem_qcom_lsn_600_vf_config),
	.allow_user_soc_reset = true,
	.reset_on_driver_unbind = true,
};

static struct mhi_pci_dev_info mhi_qcom_lassen_v2_info = {
	.name = "qcom-lassen",
	.fw = "qcom/lassen/xbl_s.melf",
	.edl = "qcom/lassen/edl.mbn",
	.config = modem_qcom_lsn_601_pf_config,
	.vf_config = modem_qcom_lsn_601_vf_config,
	.bar_num = MHI_PCI_DEFAULT_BAR_NUM,
	.sideband_wake = false,
	.max_vfs = ARRAY_SIZE(modem_qcom_lsn_601_vf_config),
	.allow_user_soc_reset = true,
	.reset_on_driver_unbind = true,
};

static const struct pci_device_id mhi_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_QCOM, LASSEN_V1_DEVICE_ID),
		.driver_data = (kernel_ulong_t) &mhi_qcom_lassen_v1_info },
	{ PCI_DEVICE(PCI_VENDOR_ID_QCOM, LASSEN_V2_DEVICE_ID),
		.driver_data = (kernel_ulong_t) &mhi_qcom_lassen_v2_info },
	{  }
};
MODULE_DEVICE_TABLE(pci, mhi_pci_id_table);

enum mhi_pci_device_status {
	MHI_PCI_DEV_STARTED,
	MHI_PCI_DEV_SUSPENDED,
};

struct mhi_pci_device {
	struct mhi_controller mhi_cntrl;
	struct pci_saved_state *pci_state;
	struct work_struct recovery_work;
	struct timer_list health_check_timer;
	unsigned long status;
};

static int mhi_pci_read_reg(struct mhi_controller *mhi_cntrl,
			    void __iomem *addr, u32 *out)
{
	*out = readl(addr);
	return 0;
}

static void mhi_pci_write_reg(struct mhi_controller *mhi_cntrl,
			      void __iomem *addr, u32 val)
{
	writel(val, addr);
}

static void mhi_pci_status_cb(struct mhi_controller *mhi_cntrl,
			      enum mhi_callback cb)
{
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);

	/* Nothing to do for now */
	switch (cb) {
	case MHI_CB_FATAL_ERROR:
	case MHI_CB_SYS_ERROR:
		dev_warn(&pdev->dev, "firmware crashed (%u)\n", cb);
	/* fallthrough */
	case MHI_CB_EE_SBL:
	case MHI_CB_EE_MISSION_MODE:
		pm_runtime_forbid(&pdev->dev);
		break;
	case MHI_CB_FW_DL_ERR:
		dev_warn(&pdev->dev, "error requesting firmware\n");
		break;
	default:
		break;
	}
}

static void mhi_pci_wake_get_nop(struct mhi_controller *mhi_cntrl, bool force)
{
	/* no-op */
}

static void mhi_pci_wake_put_nop(struct mhi_controller *mhi_cntrl, bool override)
{
	/* no-op */
}

static void mhi_pci_wake_toggle_nop(struct mhi_controller *mhi_cntrl)
{
	/* no-op */
}

static bool mhi_pci_is_alive(struct mhi_controller *mhi_cntrl)
{
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);
	u16 vendor = 0;

	if (pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor))
		return false;

	if (vendor == (u16) ~0 || vendor == 0)
		return false;

	return true;
}

static int mhi_pci_claim(struct mhi_controller *mhi_cntrl,
			 unsigned int bar_num, u64 dma_mask)
{
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int err;

	if (!pdev->is_virtfn) {
		err = pci_assign_resource(pdev, bar_num);
		if (err)
			return err;
	}
	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pci device: %d\n", err);
		return err;
	}

	err = pcim_iomap_regions(pdev, 1 << bar_num, pci_name(pdev));
	if (err) {
		dev_err(&pdev->dev, "failed to map pci region: %d\n", err);
		return err;
	}
	mhi_cntrl->regs = pcim_iomap_table(pdev)[bar_num];
	mhi_cntrl->reg_len = pci_resource_len(pdev, bar_num);

	err = pci_set_dma_mask(pdev, dma_mask);
	if (err) {
		dev_err(&pdev->dev, "Cannot set proper DMA mask\n");
		return err;
	}

	err = pci_set_consistent_dma_mask(pdev, dma_mask);
	if (err) {
		dev_err(&pdev->dev, "set consistent dma mask failed\n");
		return err;
	}

	pci_set_master(pdev);

	return 0;
}

static int mhi_pci_get_irqs(struct mhi_controller *mhi_cntrl,
			    const struct mhi_controller_config *mhi_cntrl_config)
{
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int nr_vectors, i;
	int *irq;

	/*
	 * Alloc one MSI vector for BHI + one vector per event ring, ideally...
	 * No explicit pci_free_irq_vectors required, done by pcim_release.
	 */
	mhi_cntrl->nr_irqs = 1 + mhi_cntrl_config->num_events;

	if (pdev->device == LASSEN_V1_DEVICE_ID)
		nr_vectors = pci_alloc_irq_vectors(pdev, 1, mhi_cntrl->nr_irqs,
						   PCI_IRQ_MSI);
	else
		nr_vectors = pci_alloc_irq_vectors(pdev, 1, mhi_cntrl->nr_irqs,
						   PCI_IRQ_MSI | PCI_IRQ_MSIX);

	if (nr_vectors < 0) {
		dev_err(&pdev->dev, "Error allocating MSI vectors %d\n",
			nr_vectors);
		return nr_vectors;
	}

	if (nr_vectors < mhi_cntrl->nr_irqs) {
		dev_warn(&pdev->dev, "using shared MSI\n");

		/* Patch msi vectors, use only one (shared) */
		for (i = 0; i < mhi_cntrl_config->num_events; i++)
			mhi_cntrl_config->event_cfg[i].irq = 0;
		mhi_cntrl->nr_irqs = 1;
	}

	irq = devm_kcalloc(&pdev->dev, mhi_cntrl->nr_irqs, sizeof(int), GFP_KERNEL);
	if (!irq)
		return -ENOMEM;

	for (i = 0; i < mhi_cntrl->nr_irqs; i++) {
		int vector = i >= nr_vectors ? (nr_vectors - 1) : i;

		irq[i] = pci_irq_vector(pdev, vector);
	}

	mhi_cntrl->irq = irq;

	return 0;
}

static int mhi_pci_runtime_get(struct mhi_controller *mhi_cntrl)
{
	/* The runtime_get() MHI callback means:
	 *    Do whatever is requested to leave M3.
	 */
	return pm_runtime_get(mhi_cntrl->cntrl_dev);
}

static void mhi_pci_runtime_put(struct mhi_controller *mhi_cntrl)
{
	/* The runtime_put() MHI callback means:
	 *    Device can be moved in M3 state.
	 */
	pm_runtime_mark_last_busy(mhi_cntrl->cntrl_dev);
	pm_runtime_put(mhi_cntrl->cntrl_dev);
}

static void mhi_pci_recovery_work(struct work_struct *work)
{
	struct mhi_pci_device *mhi_pdev = container_of(work, struct mhi_pci_device,
						       recovery_work);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int err;

	dev_warn(&pdev->dev, "device recovery started\n");

	del_timer(&mhi_pdev->health_check_timer);
	pm_runtime_forbid(&pdev->dev);

	/* Clean up MHI state */
	if (test_and_clear_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status)) {
		mhi_power_down(mhi_cntrl, false);
		mhi_unprepare_after_power_down(mhi_cntrl);
	}

	pci_set_power_state(pdev, PCI_D0);
	pci_load_saved_state(pdev, mhi_pdev->pci_state);
	pci_restore_state(pdev);

	if (!mhi_pci_is_alive(mhi_cntrl))
		goto err_try_reset;

	err = mhi_prepare_for_power_up(mhi_cntrl);
	if (err)
		goto err_try_reset;

	err = mhi_sync_power_up(mhi_cntrl);
	if (err)
		goto err_unprepare;

	dev_dbg(&pdev->dev, "Recovery completed\n");

	set_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status);
	mod_timer(&mhi_pdev->health_check_timer, jiffies + HEALTH_CHECK_PERIOD);
	return;

err_unprepare:
	mhi_unprepare_after_power_down(mhi_cntrl);
err_try_reset:
	if (pci_reset_function(pdev))
		dev_err(&pdev->dev, "Recovery failed\n");
}

static void health_check(struct timer_list *t)
{
	struct mhi_pci_device *mhi_pdev = from_timer(mhi_pdev, t, health_check_timer);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	if (!test_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status))
		return;

	/* If device is parked in d3hot, wake the device to check state */
	if (test_bit(MHI_PCI_DEV_SUSPENDED, &mhi_pdev->status)) {
		mhi_pci_runtime_get(mhi_cntrl);
		mhi_pci_runtime_put(mhi_cntrl);

		mod_timer(&mhi_pdev->health_check_timer,
			  jiffies + CRASH_CHECK_PERIOD);
		return;
	}

	if (!mhi_pci_is_alive(mhi_cntrl)) {
		dev_err(mhi_cntrl->cntrl_dev, "Device died\n");
		queue_work(system_long_wq, &mhi_pdev->recovery_work);
		return;
	}

	/* reschedule in two seconds */
	mod_timer(&mhi_pdev->health_check_timer, jiffies + HEALTH_CHECK_PERIOD);
}

static int mhi_pci_get_vf_num(struct mhi_controller *mhi_cntrl)
{
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);
	if (pdev->is_virtfn)
		return PCI_DEVFN(PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	return -EOPNOTSUPP;
}

static int mhi_pci_get_bus_num(struct mhi_controller *mhi_cntrl)
{
	return mhi_cntrl->index;
}

static ktime_t mhi_local_time_get(void)
{
	return ktime_get();
}

static ktime_t mhi_real_time_get(void)
{
	return ktime_get_real();
}

static int mhi_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	const struct mhi_pci_dev_info *info = (struct mhi_pci_dev_info *) id->driver_data;
	const struct mhi_controller_config *mhi_cntrl_config;
	struct mhi_pci_device *mhi_pdev, *pf_mhi_pdev = NULL;
	struct mhi_controller *mhi_cntrl, *pf_mhi_cntrl = NULL;
	struct pci_dev *pf_pdev = NULL;
	int err;

	dev_dbg(&pdev->dev, "MHI PCI device found: %s\n", info->name);

	/* mhi_pdev.mhi_cntrl must be zero-initialized */
	mhi_pdev = devm_kzalloc(&pdev->dev, sizeof(*mhi_pdev), GFP_KERNEL);
	if (!mhi_pdev)
		return -ENOMEM;

	INIT_WORK(&mhi_pdev->recovery_work, mhi_pci_recovery_work);

	/* Assign mhi_cntrl_config based on PF/VF */
	if (pdev->is_virtfn) {
		unsigned int vf_id = PCI_DEVFN(PCI_SLOT(pdev->devfn),
                                                    PCI_FUNC(pdev->devfn)) - 1;

		if (vf_id < info->max_vfs) {
			mhi_cntrl_config = &info->vf_config[vf_id];
		} else {
			dev_err(&pdev->dev, "MHI PCI VF ID is not an valid ID:%u\n",
				vf_id);
			return -ENODEV;
		}
	} else {
		mhi_cntrl_config = info->config;
		timer_setup(&mhi_pdev->health_check_timer, health_check, 0);
	}

	mhi_cntrl = &mhi_pdev->mhi_cntrl;

	mhi_cntrl->cntrl_dev = &pdev->dev;
	mhi_cntrl->iova_start = 0;
	mhi_cntrl->iova_stop = (dma_addr_t)DMA_BIT_MASK(mhi_cntrl_config->dma_data_width);
	mhi_cntrl->fw_image = info->fw;
	mhi_cntrl->edl_image = info->edl;

	mhi_cntrl->read_reg = mhi_pci_read_reg;
	mhi_cntrl->write_reg = mhi_pci_write_reg;
	mhi_cntrl->status_cb = mhi_pci_status_cb;
	mhi_cntrl->runtime_get = mhi_pci_runtime_get;
	mhi_cntrl->runtime_put = mhi_pci_runtime_put;

	/* Assign reset functionalities only for PF */
	if (!pdev->is_virtfn) {
		mhi_cntrl->allow_user_soc_reset = info->allow_user_soc_reset;
		mhi_cntrl->reset_on_driver_unbind = info->reset_on_driver_unbind;
	}

	if (info->sideband_wake) {
		mhi_cntrl->wake_get = mhi_pci_wake_get_nop;
		mhi_cntrl->wake_put = mhi_pci_wake_put_nop;
		mhi_cntrl->wake_toggle = mhi_pci_wake_toggle_nop;
	}

	if (info->auto_edl_load)
		mhi_cntrl->edl_download = true;

	err = mhi_pci_claim(mhi_cntrl, info->bar_num, DMA_BIT_MASK(mhi_cntrl_config->dma_data_width));
	if (err)
		return err;

	err = mhi_pci_get_irqs(mhi_cntrl, mhi_cntrl_config);
	if (err)
		return err;

	pci_set_drvdata(pdev, mhi_pdev);

	/* Have stored pci confspace at hand for restore in sudden PCI error.
	 * cache the state locally and discard the PCI core one.
	 */
	pci_save_state(pdev);
	mhi_pdev->pci_state = pci_store_saved_state(pdev);
	pci_load_saved_state(pdev, NULL);

	pci_enable_pcie_error_reporting(pdev);

	/* call backs to pass pci bus number and VF num if SR-IOV is enabled */
	mhi_cntrl->get_device_instance_id = mhi_pci_get_vf_num;
	mhi_cntrl->get_device_bus_number = mhi_pci_get_bus_num;

	mhi_cntrl->is_virtfn = pdev->is_virtfn;

	/* Assign parent/PF mhi_cntrl to VF mhi_cntrl*/
	if (pdev->is_virtfn) {
		pf_pdev = pdev->physfn;
		pf_mhi_pdev = pci_get_drvdata(pf_pdev);
		pf_mhi_cntrl = &pf_mhi_pdev->mhi_cntrl;

		mhi_cntrl->pf_mhi_cntrl = pf_mhi_cntrl;

		pdev->dev_flags |= PCI_DEV_FLAGS_NO_FLR_RESET;
	}

	err = mhi_register_controller(mhi_cntrl, mhi_cntrl_config);
	if (err)
		goto err_disable_reporting;

	err = mhi_controller_setup_timesync(mhi_cntrl, &mhi_local_time_get);
	if (err)
		goto err_disable_reporting;

	err = mhi_controller_setup_tsc_timesync(mhi_cntrl, &mhi_real_time_get);
	if (err)
		goto err_tsync_disable_reporting;

	/* MHI bus does not power up the controller by default */
	err = mhi_prepare_for_power_up(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to prepare MHI controller\n");
		goto err_unregister;
	}

	err = mhi_sync_power_up(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to power up MHI controller\n");
		goto err_unprepare;
	}

	set_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status);

	/* start health check only to PF and not for VF's */
	if (mhi_pdev->health_check_timer.function)
		mod_timer(&mhi_pdev->health_check_timer,
			  jiffies + HEALTH_CHECK_PERIOD);

	/* Only allow runtime-suspend if PME capable (for wakeup) */
	if (pci_pme_capable(pdev, PCI_D3hot)) {
		device_init_wakeup(&pdev->dev, 1);
		pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
		pm_runtime_use_autosuspend(&pdev->dev);
		pm_runtime_mark_last_busy(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);
	}

	return 0;

err_unprepare:
	mhi_unprepare_after_power_down(mhi_cntrl);
err_unregister:
	mhi_unregister_controller(mhi_cntrl);
	kfree(mhi_cntrl->tsc_timesync);
err_tsync_disable_reporting:
	kfree(mhi_cntrl->timesync);
err_disable_reporting:
	pci_disable_pcie_error_reporting(pdev);

	return err;
}

static void mhi_pci_resource_deinit( struct pci_dev *pdev)
{
	struct mhi_pci_device *mhi_pdev = pci_get_drvdata(pdev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	del_timer_sync(&mhi_pdev->health_check_timer);
	cancel_work_sync(&mhi_pdev->recovery_work);

	if (test_and_clear_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status)) {
		mhi_power_down(mhi_cntrl, true);
		mhi_unprepare_after_power_down(mhi_cntrl);
	}

	/* balancing probe put_noidle */
	if (pci_pme_capable(pdev, PCI_D3hot))
		pm_runtime_get_noresume(&pdev->dev);

}

static void mhi_pci_remove(struct pci_dev *pdev)
{
	struct mhi_pci_device *mhi_pdev = pci_get_drvdata(pdev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	mhi_pci_resource_deinit(pdev);

	pci_disable_sriov(pdev);

	if (mhi_cntrl->reset_on_driver_unbind) {
		dev_dbg(&pdev->dev, "perform SOC reset\n");
		mhi_soc_reset(mhi_cntrl);
	}

	mhi_unregister_controller(mhi_cntrl);
	pci_disable_pcie_error_reporting(pdev);
}

static void mhi_pci_shutdown(struct pci_dev *pdev)
{
	struct mhi_pci_device *mhi_pdev = pci_get_drvdata(pdev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	mhi_pci_resource_deinit(pdev);

	mhi_unregister_controller(mhi_cntrl);
	pci_disable_pcie_error_reporting(pdev);

	pci_set_power_state(pdev, PCI_D3hot);
}

static void mhi_pci_reset_prepare(struct pci_dev *pdev)
{
	struct mhi_pci_device *mhi_pdev = pci_get_drvdata(pdev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	dev_info(&pdev->dev, "reset\n");

	del_timer(&mhi_pdev->health_check_timer);

	/* Clean up MHI state */
	if (test_and_clear_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status)) {
		mhi_power_down(mhi_cntrl, false);
		mhi_unprepare_after_power_down(mhi_cntrl);
	}

	/* cause internal device reset */
	mhi_soc_reset(mhi_cntrl);

	/* Be sure device reset has been executed */
	msleep(MHI_POST_RESET_DELAY_MS);
}

static void mhi_pci_reset_done(struct pci_dev *pdev)
{
	struct mhi_pci_device *mhi_pdev = pci_get_drvdata(pdev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;
	int err;

	/* Restore initial known working PCI state */
	pci_load_saved_state(pdev, mhi_pdev->pci_state);
	pci_restore_state(pdev);

	/* Is device status available ? */
	if (!mhi_pci_is_alive(mhi_cntrl)) {
		dev_err(&pdev->dev, "reset failed\n");
		return;
	}

	err = mhi_prepare_for_power_up(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to prepare MHI controller\n");
		return;
	}

	err = mhi_sync_power_up(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to power up MHI controller\n");
		mhi_unprepare_after_power_down(mhi_cntrl);
		return;
	}

	set_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status);
	mod_timer(&mhi_pdev->health_check_timer, jiffies + HEALTH_CHECK_PERIOD);
}

static pci_ers_result_t mhi_pci_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct mhi_pci_device *mhi_pdev = pci_get_drvdata(pdev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	dev_err(&pdev->dev, "PCI error detected, state = %u\n", state);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	/* Clean up MHI state */
	if (test_and_clear_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status)) {
		mhi_power_down(mhi_cntrl, false);
		mhi_unprepare_after_power_down(mhi_cntrl);
	} else {
		/* Nothing to do */
		return PCI_ERS_RESULT_RECOVERED;
	}

	pci_disable_device(pdev);

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t mhi_pci_slot_reset(struct pci_dev *pdev)
{
	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return PCI_ERS_RESULT_RECOVERED;
}

static void mhi_pci_io_resume(struct pci_dev *pdev)
{
	struct mhi_pci_device *mhi_pdev = pci_get_drvdata(pdev);

	dev_err(&pdev->dev, "PCI slot reset done\n");

	queue_work(system_long_wq, &mhi_pdev->recovery_work);
}

static const struct pci_error_handlers mhi_pci_err_handler = {
	.error_detected = mhi_pci_error_detected,
	.slot_reset = mhi_pci_slot_reset,
	.resume = mhi_pci_io_resume,
	.reset_prepare = mhi_pci_reset_prepare,
	.reset_done = mhi_pci_reset_done,
};

static int  __maybe_unused mhi_pci_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mhi_pci_device *mhi_pdev = dev_get_drvdata(dev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;
	int err;

	if (test_and_set_bit(MHI_PCI_DEV_SUSPENDED, &mhi_pdev->status))
		return 0;

	del_timer(&mhi_pdev->health_check_timer);
	cancel_work_sync(&mhi_pdev->recovery_work);

	if (!test_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status) ||
			mhi_cntrl->ee != MHI_EE_AMSS)
		goto pci_suspend; /* Nothing to do at MHI level */

	/* Transition to M3 state */
	err = mhi_pm_suspend(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to suspend device: %d\n", err);
		clear_bit(MHI_PCI_DEV_SUSPENDED, &mhi_pdev->status);
		return -EBUSY;
	}

pci_suspend:
	pci_disable_device(pdev);
	err = pci_wake_from_d3(pdev, true);
	if (err)
		dev_dbg(&pdev->dev, "failed to set D3 wake-capable: %d\n", err);

	/* start crash check timer for D3hot to D0 transitions */
	mod_timer(&mhi_pdev->health_check_timer, jiffies + CRASH_CHECK_PERIOD);

	return 0;
}

static int __maybe_unused mhi_pci_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mhi_pci_device *mhi_pdev = dev_get_drvdata(dev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;
	int err;

	if (!test_and_clear_bit(MHI_PCI_DEV_SUSPENDED, &mhi_pdev->status))
		return 0;

	err = pci_enable_device(pdev);
	if (err)
		goto err_recovery;

	pci_set_master(pdev);
	pci_wake_from_d3(pdev, false);

	if (!test_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status) ||
			mhi_cntrl->ee != MHI_EE_AMSS)
		return 0; /* Nothing to do at MHI level */

	/* Exit M3, transition to M0 state */
	err = mhi_pm_resume(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to resume device: %d\n", err);
		goto err_recovery;
	}

	/* Resume health check */
	mod_timer(&mhi_pdev->health_check_timer, jiffies + HEALTH_CHECK_PERIOD);

	/* It can be a remote wakeup (no mhi runtime_get), update access time */
	pm_runtime_mark_last_busy(dev);

	return 0;

err_recovery:
	/* Do not fail to not mess up our PCI device state, the device likely
	 * lost power (d3cold) and we simply need to reset it from the recovery
	 * procedure, trigger the recovery asynchronously to prevent system
	 * suspend exit delaying.
	 */
	queue_work(system_long_wq, &mhi_pdev->recovery_work);
	pm_runtime_mark_last_busy(dev);

	return 0;
}

static int  __maybe_unused mhi_pci_suspend(struct device *dev)
{
	pm_runtime_disable(dev);
	return mhi_pci_runtime_suspend(dev);
}

static int __maybe_unused mhi_pci_resume(struct device *dev)
{
	int ret;

	/* Depending the platform, device may have lost power (d3cold), we need
	 * to resume it now to check its state and recover when necessary.
	 */
	ret = mhi_pci_runtime_resume(dev);
	pm_runtime_enable(dev);

	return ret;
}

static int __maybe_unused mhi_pci_freeze(struct device *dev)
{
	struct mhi_pci_device *mhi_pdev = dev_get_drvdata(dev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	flush_work(&mhi_pdev->recovery_work);
	/* We want to stop all operations, hibernation does not guarantee that
	 * device will be in the same state as before freezing, especially if
	 * the intermediate restore kernel reinitializes MHI device with new
	 * context.
	 */
	if (test_and_clear_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status)) {
		mhi_power_down(mhi_cntrl, true);
		mhi_unprepare_after_power_down(mhi_cntrl);
	}

	return 0;
}

static int __maybe_unused mhi_pci_restore(struct device *dev)
{
	struct mhi_pci_device *mhi_pdev = dev_get_drvdata(dev);
	struct mhi_controller *mhi_cntrl = &mhi_pdev->mhi_cntrl;

	/* Clean up MHI state */
	if (test_and_clear_bit(MHI_PCI_DEV_STARTED, &mhi_pdev->status)) {
		mhi_power_down(mhi_cntrl, true);
		mhi_unprepare_after_power_down(mhi_cntrl);
	}

	/* Reinitialize the device */
	queue_work(system_long_wq, &mhi_pdev->recovery_work);

	return 0;
}

static const struct dev_pm_ops mhi_pci_pm_ops = {
	SET_RUNTIME_PM_OPS(mhi_pci_runtime_suspend, mhi_pci_runtime_resume, NULL)
#ifdef CONFIG_PM_SLEEP
	.suspend = mhi_pci_suspend,
	.resume = mhi_pci_resume,
	.freeze = mhi_pci_freeze,
	.thaw = mhi_pci_restore,
	.poweroff = mhi_pci_freeze,
	.restore = mhi_pci_restore,
#endif
};

static struct pci_driver mhi_pci_driver = {
	.name		= "mhi-pci-generic",
	.id_table	= mhi_pci_id_table,
	.probe		= mhi_pci_probe,
	.remove		= mhi_pci_remove,
	.shutdown	= mhi_pci_shutdown,
	.err_handler	= &mhi_pci_err_handler,
	.driver.pm	= &mhi_pci_pm_ops,
	.sriov_configure = pci_sriov_configure_simple
};
module_pci_driver(mhi_pci_driver);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("Modem Host Interface (MHI) PCI controller driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(MHI_MODULE_VERSION);
