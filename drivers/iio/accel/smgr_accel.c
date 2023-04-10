// SPDX-License-Identifier: GPL-2.0-only
/* 
 * Qualcomm Sensor Manager accelerometer driver
 * 
 * Copyright (c) 2022, Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/common/qcom_smgr.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>

static int smgr_accel_read_raw(struct iio_dev *iio_dev,
			       struct iio_chan_spec const *chan, int *val,
			       int *val2, long mask)
{
	struct smgr_iio_priv *priv = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = priv->sensor->data_types[0].cur_sample_rate;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * TODO: Find out if the scale is standard across devices or
		 * find a way to get the correct scale for a device
		 *
		 * Device reports around 640000 when axis is aligned with
		 * gravity, therefore scale is 9.81m/s^2 / 640000.
		 */
		*val = 0;
		*val2 = 15328; /* scale * 10^9 */
		return IIO_VAL_INT_PLUS_NANO;
	}

	return -EINVAL;
}

static int smgr_accel_write_raw(struct iio_dev *iio_dev,
				struct iio_chan_spec const *chan, int val,
				int val2, long mask)
{
	struct smgr_iio_priv *priv = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		priv->sensor->data_types[0].cur_sample_rate = val;

		/*
		 * Send new SMGR buffering request with updated rates
		 * if buffer is enabled
		 */
		if (iio_buffer_enabled(iio_dev))
			return iio_dev->setup_ops->postenable(iio_dev);

		return 0;
	}

	return -EINVAL;
}

static int smgr_accel_read_avail(struct iio_dev *iio_dev,
				 struct iio_chan_spec const *chan,
				 const int **vals, int *type, int *length,
				 long mask)
{
	struct smgr_iio_priv *priv = iio_priv(iio_dev);
	const int samp_freq_vals[3] = {
		1, 1, priv->sensor->data_types[0].max_sample_rate
	};

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT;
		*vals = samp_freq_vals;
		*length = ARRAY_SIZE(samp_freq_vals);
		return IIO_AVAIL_RANGE;
	}

	return -EINVAL;
}

static const struct iio_info smgr_accel_iio_info = {
	.read_raw = smgr_accel_read_raw,
	.write_raw = smgr_accel_write_raw,
	.read_avail = smgr_accel_read_avail,
};

/* TODO: Get mount matrix from SSC or read it from the device tree */
static const struct iio_mount_matrix qcom_ssc_mount_matrix = {
	.rotation = {
		"0", "-1", "0",
		"-1", "0", "0",
		"0", "0", "1"
	}
};

static const struct iio_mount_matrix *
smgr_accel_get_mount_matrix(const struct iio_dev *iio_dev,
			    const struct iio_chan_spec *chan)
{
	return &qcom_ssc_mount_matrix;
}

static const struct iio_chan_spec_ext_info smgr_accel_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, smgr_accel_get_mount_matrix),
	{}
};

static const struct iio_chan_spec smgr_accel_iio_channels[] = {
	{
		.type = IIO_ACCEL,
		.modified = true,
		.channel2 = IIO_MOD_X,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					    BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = smgr_accel_ext_info
	},
	{
		.type = IIO_ACCEL,
		.modified = true,
		.channel2 = IIO_MOD_Y,
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					    BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = smgr_accel_ext_info
	},
	{
		.type = IIO_ACCEL,
		.modified = true,
		.channel2 = IIO_MOD_Z,
		.scan_index = 2,
		.scan_type = {
			.sign = 's',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					    BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = smgr_accel_ext_info
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

static int smgr_accel_probe(struct platform_device *pdev)
{
	struct iio_dev *iio_dev;
	struct smgr_iio_priv *priv;
	int ret;

	iio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*priv));
	if (!iio_dev)
		return -ENOMEM;

	priv = iio_priv(iio_dev);
	priv->sensor = *(struct smgr_sensor **)pdev->dev.platform_data;
	priv->sensor->iio_dev = iio_dev;

	iio_dev->name = "qcom-smgr-accel";
	iio_dev->info = &smgr_accel_iio_info;
	iio_dev->channels = smgr_accel_iio_channels;
	iio_dev->num_channels = ARRAY_SIZE(smgr_accel_iio_channels);

	ret = devm_iio_kfifo_buffer_setup(&pdev->dev, iio_dev,
					  &smgr_buffer_ops);
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

	return 0;
}

static void smgr_accel_remove(struct platform_device *pdev)
{
	struct iio_dev *iio_dev = platform_get_drvdata(pdev);
	struct smgr_iio_priv *priv = iio_priv(iio_dev);

	priv->sensor->iio_dev = NULL;
}

static const struct platform_device_id smgr_accel_ids[] = {
	{ .name = "qcom-smgr-accel" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, smgr_accel_ids);

static struct platform_driver smgr_accel_driver = {
	.probe = smgr_accel_probe,
	.remove = smgr_accel_remove,
	.driver	= {
		.name = "smgr_accel",
	},
	.id_table = smgr_accel_ids,
};
module_platform_driver(smgr_accel_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm Sensor Manager accelerometer driver");
MODULE_LICENSE("GPL");
