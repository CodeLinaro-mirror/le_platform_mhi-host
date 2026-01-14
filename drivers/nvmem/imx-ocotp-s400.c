// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Kontron Electronics GmbH
 */

#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/firmware/imx/se_api.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

struct imx_s400_fuse_hw {
	const bool reverse_mac_address;
	const struct nvmem_keepout *keepout;
	unsigned int nkeepout;
};

struct imx_s400_fuse {
	const struct imx_s400_fuse_hw *hw;
	struct platform_device *se_dev;
	struct nvmem_config config;
	struct mutex lock;
	void *se_data;
};

static int imx_s400_fuse_read(void *priv, unsigned int offset, void *val,
			      size_t bytes)
{
	struct imx_s400_fuse *fuse = priv;
	u32 i, word, num_words;
	int ret;

	word = offset >> 2;
	num_words = bytes >> 2;

	mutex_lock(&fuse->lock);

	for (i = word; i < (word + num_words); i++) {
		ret = imx_se_read_fuse(fuse->se_data, i, ((u32 *)val) + i - word);
		if (ret) {
			mutex_unlock(&fuse->lock);
			return ret;
		}
	}

	mutex_unlock(&fuse->lock);
	return 0;
}

static int imx_s400_fuse_post_process(void *priv, const char *id, int index,
				      unsigned int offset, void *data,
				      size_t bytes)
{
	u8 *buf = data;
	int i;

	if (id && !strcmp(id, "mac-address")) {
		for (i = 0; i < bytes / 2; i++)
			swap(buf[i], buf[bytes - i - 1]);
	}

	return 0;
}

static int imx_s400_fuse_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct imx_s400_fuse *fuse = priv;
	u32 word = offset >> 2;
	u32 *buf = val;
	int ret;

	/* allow only writing one complete OTP word at a time */
	if (bytes != 4)
		return -EINVAL;

	/*
	 * The S400 API returns an error when writing an all-zero value. As
	 * OTP fuse bits can not be switched from 1 to 0 anyway, skip these
	 * values.
	 */
	if (!*buf)
		return 0;

	mutex_lock(&fuse->lock);
	ret = imx_se_write_fuse(fuse->se_data, word, *buf);
	mutex_unlock(&fuse->lock);

	return ret;
}

static void imx_s400_fuse_fixup_cell_info(struct nvmem_device *nvmem,
					  struct nvmem_cell_info *cell)
{
	cell->read_post_process = imx_s400_fuse_post_process;
}

static int imx_s400_fuse_probe(struct platform_device *pdev)
{
	struct imx_s400_fuse *fuse;
	struct nvmem_device *nvmem;
	struct device_node *np;

	fuse = devm_kzalloc(&pdev->dev, sizeof(*fuse), GFP_KERNEL);
	if (!fuse)
		return -ENOMEM;

	fuse->hw = of_device_get_match_data(&pdev->dev);

	fuse->config.dev = &pdev->dev;
	fuse->config.name = "imx_s400_fuse";
	fuse->config.id = NVMEM_DEVID_AUTO;
	fuse->config.owner = THIS_MODULE;
	fuse->config.size = 2048; /* 64 Banks of 8 Words */
	fuse->config.word_size = 4;
	fuse->config.add_legacy_fixed_of_cells = true;
	fuse->config.reg_read = imx_s400_fuse_read;
	fuse->config.reg_write = imx_s400_fuse_write;
	fuse->config.priv = fuse;
	fuse->config.keepout = fuse->hw->keepout;
	fuse->config.nkeepout = fuse->hw->nkeepout;

	if (fuse->hw->reverse_mac_address)
		fuse->config.fixup_dt_cell_info = &imx_s400_fuse_fixup_cell_info;

	dev_set_drvdata(&pdev->dev, fuse);

	mutex_init(&fuse->lock);

	nvmem = devm_nvmem_register(&pdev->dev, &fuse->config);
	if (IS_ERR(nvmem))
		return dev_err_probe(&pdev->dev, PTR_ERR(nvmem), "failed to register nvmem device\n");

	np = of_parse_phandle(pdev->dev.of_node, "secure-enclave", 0);
	if (!np)
		return dev_err_probe(&pdev->dev, -ENODEV, "missing or invalid secure-enclave handle\n");

	fuse->se_dev = of_find_device_by_node(np);
	of_node_put(np);
	if (!fuse->se_dev)
		return dev_err_probe(&pdev->dev, -ENODEV, "failed to find secure-enclave device\n");

	get_device(&fuse->se_dev->dev);
	fuse->se_data = platform_get_drvdata(fuse->se_dev);
	if (!fuse->se_data)
		return -EPROBE_DEFER;

	dev_info(&pdev->dev, "i.MX S400 OCOTP NVMEM device registered successfully\n");

	return 0;
}

static void imx_s400_fuse_remove(struct platform_device *pdev)
{
	struct imx_s400_fuse *fuse = platform_get_drvdata(pdev);
	put_device(&fuse->se_dev->dev);
}

static const struct nvmem_keepout imx93_s400_keepout[] = {
	{.start = 208, .end = 252},
	{.start = 256, .end = 512},
	{.start = 576, .end = 728},
	{.start = 732, .end = 752},
	{.start = 756, .end = 1248},
};

static const struct imx_s400_fuse_hw imx93_s400_fuse_hw = {
	.reverse_mac_address = true,
	.keepout = imx93_s400_keepout,
	.nkeepout = ARRAY_SIZE(imx93_s400_keepout),
};

static const struct of_device_id imx_s400_fuse_match[] = {
	{ .compatible = "fsl,imx93-ocotp-s400", .data = &imx93_s400_fuse_hw, },
	{},
};

static struct platform_driver imx_s400_fuse_driver = {
	.driver = {
		.name = "fsl-ocotp-s400",
		.of_match_table = imx_s400_fuse_match,
	},
	.probe = imx_s400_fuse_probe,
	.remove = imx_s400_fuse_remove,
};
MODULE_DEVICE_TABLE(of, imx_s400_fuse_match);
module_platform_driver(imx_s400_fuse_driver);

MODULE_AUTHOR("Frieder Schrempf <frieder.schrempf@kontron.de>");
MODULE_DESCRIPTION("i.MX S400 OCOTP Driver");
MODULE_LICENSE("GPL v2");
