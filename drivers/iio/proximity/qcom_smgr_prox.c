// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Sensor Manager proximity sensor driver
 *
 * Copyright (c) 2022, Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/common/qcom_smgr.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>

static const struct iio_chan_spec qcom_smgr_prox_iio_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.info_mask_separate = BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ)
	},
	{
		.type = IIO_TIMESTAMP,
		.channel = -1,
		.scan_index = 3,
		.scan_type = {
			.sign = 'u',
			.realbits = 32,
			.storagebits = 64,
			.endianness = IIO_LE,
		},
	},
};

static int qcom_smgr_prox_probe(struct platform_device *pdev)
{
	struct iio_dev *iio_dev;
	struct qcom_smgr_iio_priv *priv;
	int ret;

	iio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*priv));
	if (!iio_dev)
		return -ENOMEM;

	priv = iio_priv(iio_dev);
	priv->sensor = *(struct qcom_smgr_sensor **)pdev->dev.platform_data;
	priv->sensor->iio_dev = iio_dev;

	iio_dev->name = "qcom-smgr-prox";
	iio_dev->info = &qcom_smgr_iio_info;
	iio_dev->channels = qcom_smgr_prox_iio_channels;
	iio_dev->num_channels = ARRAY_SIZE(qcom_smgr_prox_iio_channels);

	ret = devm_iio_kfifo_buffer_setup(&pdev->dev, iio_dev,
					  &qcom_smgr_buffer_ops);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup buffer: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	ret = devm_iio_device_register(&pdev->dev, iio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register IIO device: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	platform_set_drvdata(pdev, priv->sensor);

	return 0;
}

static void qcom_smgr_prox_remove(struct platform_device *pdev)
{
	struct qcom_smgr_sensor *sensor = platform_get_drvdata(pdev);

	sensor->iio_dev = NULL;
}

static const struct platform_device_id qcom_smgr_prox_ids[] = {
	{ .name = "qcom-smgr-prox-light" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, qcom_smgr_prox_ids);

static struct platform_driver qcom_smgr_prox_driver = {
	.probe = qcom_smgr_prox_probe,
	.remove = qcom_smgr_prox_remove,
	.driver	= {
		.name = "qcom_smgr_prox",
	},
	.id_table = qcom_smgr_prox_ids,
};
module_platform_driver(qcom_smgr_prox_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm Sensor Manager proximity sensor driver");
MODULE_LICENSE("GPL");
