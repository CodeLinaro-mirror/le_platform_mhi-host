// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/termios.h>
#include <linux/wait.h>

struct dtr_ctrl_msg {
	u32 preamble;
	u32 msg_id;
	u32 dest_id;
	u32 size;
	u32 msg;
} __packed;

static struct dtr_info {
	struct completion completion;
	struct mhi_device *mhi_dev;
} *dtr_info;

#define CTRL_MAGIC (0x4C525443)
#define CTRL_MSG_DTR BIT(0)
#define CTRL_MSG_RTS BIT(1)
#define CTRL_MSG_DCD BIT(0)
#define CTRL_MSG_DSR BIT(1)
#define CTRL_MSG_RI BIT(3)
#define CTRL_HOST_STATE (0x10)
#define CTRL_DEVICE_STATE (0x11)
#define CTRL_GET_CHID(dtr) ((dtr)->dest_id & 0xFF)

static int mhi_dtr_tiocmset(struct mhi_device *mhi_dev, u32 tiocm)
{
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct device *dev = &mhi_dev->dev;
	struct dtr_ctrl_msg *dtr_msg = NULL;
	/* protects state changes for MHI device termios states */
	spinlock_t *res_lock = &mhi_dev->dev.devres_lock;
	u32 cur_tiocm;
	int ret = 0;

	cur_tiocm = mhi_dev->tiocm & ~(TIOCM_CD | TIOCM_DSR | TIOCM_RI);

	tiocm &= (TIOCM_DTR | TIOCM_RTS);

	/* state did not change */
	if (cur_tiocm == tiocm)
		return 0;

	dtr_msg = kzalloc(sizeof(*dtr_msg), GFP_KERNEL);
	if (!dtr_msg) {
		ret = -ENOMEM;
		goto tiocm_exit;
	}

	dtr_msg->preamble = CTRL_MAGIC;
	dtr_msg->msg_id = CTRL_HOST_STATE;
	dtr_msg->dest_id = mhi_dev->ul_chan_id;
	dtr_msg->size = sizeof(u32);
	if (tiocm & TIOCM_DTR)
		dtr_msg->msg |= CTRL_MSG_DTR;
	if (tiocm & TIOCM_RTS)
		dtr_msg->msg |= CTRL_MSG_RTS;

	reinit_completion(&dtr_info->completion);
	ret = mhi_queue_buf(dtr_info->mhi_dev, DMA_TO_DEVICE, dtr_msg,
			    sizeof(*dtr_msg), MHI_EOT);
	if (ret)
		goto tiocm_exit;

	ret = wait_for_completion_timeout(&dtr_info->completion,
				msecs_to_jiffies(mhi_cntrl->timeout_ms));
	if (!ret) {
		dev_err(dev, "Failed to receive UL transfer callback ACK\n");
		ret = -EIO;
		goto tiocm_exit;
	}

	ret = 0;
	spin_lock_irq(res_lock);
	mhi_dev->tiocm &= ~(TIOCM_DTR | TIOCM_RTS);
	mhi_dev->tiocm |= tiocm;
	spin_unlock_irq(res_lock);

	dev_info(dev, "DTR TIOCMSET updated as tiocm\n");

tiocm_exit:
	kfree(dtr_msg);

	return ret;
}

long mhi_dtr_ioctl(struct mhi_device *mhi_dev, unsigned int cmd,
		   unsigned long arg)
{
	struct device *dev = &mhi_dev->dev;
	int ret;

	/* ioctl not supported by this controller */
	if (!dtr_info->mhi_dev) {
		dev_err(dev, "Request denied. DTR channels not running\n");
		return -EIO;
	}

	switch (cmd) {
	case TIOCMGET:
		return mhi_dev->tiocm;
	case TIOCMSET:
	{
		u32 tiocm;

		ret = get_user(tiocm, (u32 __user *)arg);
		if (ret)
			return ret;

		return mhi_dtr_tiocmset(mhi_dev, tiocm);
	}
	default:
		break;
	}

	return -ENOIOCTLCMD;
}
EXPORT_SYMBOL(mhi_dtr_ioctl);

static void mhi_dtr_dl_xfer_cb(struct mhi_device *mhi_dev,
			       struct mhi_result *mhi_result)
{
	struct device *dev = &mhi_dev->dev;
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct dtr_ctrl_msg *dtr_msg = mhi_result->buf_addr;
	/* protects state changes for MHI device termios states */
	spinlock_t *res_lock;

	if (mhi_result->bytes_xferd != sizeof(*dtr_msg)) {
		dev_err(dev, "Unexpected length %zu received\n",
			mhi_result->bytes_xferd);
		return;
	}

	dev_dbg(dev, "preamble: 0x%x msg_id: %u dest_id: %u msg: 0x%x\n",
		dtr_msg->preamble, dtr_msg->msg_id, dtr_msg->dest_id,
		dtr_msg->msg);

	mhi_dev = mhi_get_device_for_channel(mhi_cntrl, CTRL_GET_CHID(dtr_msg));
	if (!mhi_dev)
		return;

	res_lock = &mhi_dev->dev.devres_lock;
	spin_lock_irq(res_lock);
	mhi_dev->tiocm &= ~(TIOCM_CD | TIOCM_DSR | TIOCM_RI);

	if (dtr_msg->msg & CTRL_MSG_DCD)
		mhi_dev->tiocm |= TIOCM_CD;

	if (dtr_msg->msg & CTRL_MSG_DSR)
		mhi_dev->tiocm |= TIOCM_DSR;

	if (dtr_msg->msg & CTRL_MSG_RI)
		mhi_dev->tiocm |= TIOCM_RI;
	spin_unlock_irq(res_lock);

	mhi_notify(mhi_dev, MHI_CB_DTR_SIGNAL);
}

static void mhi_dtr_ul_xfer_cb(struct mhi_device *mhi_dev,
			       struct mhi_result *mhi_result)
{
	struct device *dev = &mhi_dev->dev;

	dev_dbg(dev, "Received with status: %d\n",
		mhi_result->transaction_status);

	if (!mhi_result->transaction_status)
		complete(&dtr_info->completion);
}

static void mhi_dtr_remove(struct mhi_device *mhi_dev)
{
	dtr_info->mhi_dev = NULL;
}

static int mhi_dtr_probe(struct mhi_device *mhi_dev,
			 const struct mhi_device_id *id)
{
	struct device *dev = &mhi_dev->dev;
	int ret;

	ret = mhi_prepare_for_transfer(mhi_dev, 0);
	if (!ret)
		dtr_info->mhi_dev = mhi_dev;

	dev_dbg(dev, "Setup complete, ret: %d\n", ret);

	return ret;
}

static const struct mhi_device_id mhi_dtr_table[] = {
	{ .chan = "IP_CTRL" },
	{},
};

static struct mhi_driver mhi_dtr_driver = {
	.id_table = mhi_dtr_table,
	.remove = mhi_dtr_remove,
	.probe = mhi_dtr_probe,
	.ul_xfer_cb = mhi_dtr_ul_xfer_cb,
	.dl_xfer_cb = mhi_dtr_dl_xfer_cb,
	.driver = {
		.name = "MHI_DTR",
		.owner = THIS_MODULE,
	}
};

static int __init mhi_dtr_init(void)
{
	dtr_info = kzalloc(sizeof(*dtr_info), GFP_KERNEL);
	if (!dtr_info)
		return -ENOMEM;

	init_completion(&dtr_info->completion);

	return mhi_driver_register(&mhi_dtr_driver);
}
module_init(mhi_dtr_init);

static void __exit mhi_dtr_exit(void)
{
	mhi_driver_unregister(&mhi_dtr_driver);
	kfree(dtr_info);
}
module_exit(mhi_dtr_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("MHI_DTR");
MODULE_DESCRIPTION("MHI DTR Driver");
