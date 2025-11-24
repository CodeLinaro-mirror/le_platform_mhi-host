// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/kernel.h>
#include <linux/local_mhi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/version.h>

#define MHI_PHC_DRIVER_NAME "mhi_phc"
#define NSEC 1000000000ULL

/**
 * struct mhi_phc_dev - MHI PHC device
 * @ptp_clock: associated PTP clock
 * @ptp_clock_info: PTP clock information
 * @mhi_dev: associated mhi device object
 * @enabled: Flag to track the state of the MHI device
 */
struct mhi_phc_dev {
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info  ptp_clock_info;
	struct mhi_device *mhi_dev;
	bool enabled;
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0))
static int qcom_ptp_adjfine(struct ptp_clock_info *ptp, long ppb)
{
       pr_debug("%s: dummy\n", __func__);
       return 0;
}
#else
static int qcom_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
       pr_debug("%s: dummy\n", __func__);
       return 0;
}
#endif

static int qcom_ptp_gettimex64(struct ptp_clock_info *ptp, struct timespec64 *ts,
                          struct ptp_system_timestamp *sts)
{
	struct mhi_phc_dev *udev = container_of(ptp, struct mhi_phc_dev, ptp_clock_info);
	u32 mhi_tsc_nsec, mhi_tsc_sec;
	ktime_t ktime_pre, ktime_curr, ktime_post;
	int ret;

	if (!udev->enabled)
		return -ENODEV;

	ret = mhi_get_remote_time_ptp_sync(udev->mhi_dev, &mhi_tsc_nsec, &mhi_tsc_sec,
								&ktime_pre, &ktime_post);
	if (ret)
		return ret;

	ktime_curr = mhi_tsc_sec * NSEC + mhi_tsc_nsec;
	*ts = ktime_to_timespec64(ktime_curr);

	pr_debug("%s: MHI-TSC_sec:%u MHI-TSC_nsec:%u current:%lld\n", __func__, mhi_tsc_sec,
			mhi_tsc_nsec, ktime_curr);
	/* PTP_SYS_OFFSET or PTP_SYS_OFFSET2 does not support pre and post timestamps */
	if (sts != NULL) {
		sts->pre_ts = ktime_to_timespec64(ktime_pre);
		sts->post_ts = ktime_to_timespec64(ktime_post);
		pr_debug("%s: pre:%lld post:%lld\n", __func__, ktime_pre, ktime_post);
	}

	return 0;
}

static struct ptp_clock_info qcom_ptp_clock_info = {
	.owner    = THIS_MODULE,
	.name     = "MHI_PHC",
	.max_adj  = 999999999,
	.gettimex64 =  qcom_ptp_gettimex64,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0))
	.adjfine  = qcom_ptp_adjfine,
#else
	.adjfreq  = qcom_ptp_adjfreq,
#endif

};

/* Dummy transfer call-backs */
static void mhi_phc_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	return;
}

static void mhi_phc_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	return;
}

static int mhi_phc_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	struct mhi_phc_dev *udev;
	int ret;

	udev = devm_kzalloc(&mhi_dev->dev, sizeof(*udev), GFP_KERNEL);
	if (!udev)
		return -ENOMEM;

	udev->mhi_dev = mhi_dev;

	udev->ptp_clock_info = qcom_ptp_clock_info;

	udev->ptp_clock = ptp_clock_register(&udev->ptp_clock_info, &mhi_dev->dev);
	if (IS_ERR(udev->ptp_clock)) {
		ret = PTR_ERR(udev->ptp_clock);
		dev_err(&mhi_dev->dev, "Failed to register PTP clock\n");
		udev->ptp_clock = NULL;
		return ret;
	}

	udev->enabled = true;
	dev_set_drvdata(&mhi_dev->dev, udev);

	dev_info(&mhi_dev->dev, "probed MHI dev: %s\n", id->chan);

	return 0;
};

static void mhi_phc_remove(struct mhi_device *mhi_dev)
{
	struct mhi_phc_dev *udev = dev_get_drvdata(&mhi_dev->dev);

	/* disable the node */
	ptp_clock_unregister(udev->ptp_clock);
	udev->enabled = false;
}

/* .driver_data stores max mtu */
static const struct mhi_device_id mhi_phc_match_table[] = {
	{ .chan = "MHI_PHC", .driver_data = 0x1000},
	{},
};
MODULE_DEVICE_TABLE(phc, mhi_phc_match_table);

static struct mhi_driver mhi_phc_driver = {
	.id_table = mhi_phc_match_table,
	.remove = mhi_phc_remove,
	.probe = mhi_phc_probe,
	.ul_xfer_cb = mhi_phc_ul_xfer_cb,
	.dl_xfer_cb = mhi_phc_dl_xfer_cb,
	.driver = {
		.name = MHI_PHC_DRIVER_NAME,
	},
};

static int __init mhi_phc_init(void)
{
	return mhi_driver_register(&mhi_phc_driver);
}

static void __exit mhi_phc_exit(void)
{
	mhi_driver_unregister(&mhi_phc_driver);
}

module_init(mhi_phc_init);
module_exit(mhi_phc_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MHI PHC Driver");
