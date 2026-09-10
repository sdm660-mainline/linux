// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synaptics TouchComm (TCM) I2C touchscreen driver.
 *
 * Covers TouchComm generation-1 controllers that boot into application mode out
 * of their own flash, i.e. the ones that need no host firmware download at
 * probe time. The Synaptics S3908 fitted to the OnePlus 9RT (oneplus,martini)
 * is one of those.
 *
 * Protocol, as implemented by the vendor driver:
 *
 *   A command is one plain I2C write of
 *
 *      [ cmd | len_lo | len_hi | payload... ]
 *
 *   A message -- both command responses and asynchronous reports -- is one
 *   plain I2C read of
 *
 *      [ 0xa5 | code | len_lo | len_hi | payload... | 0x5a ]
 *
 *   code <= 0x0f (and 0xff) is a status, so the message is a command response;
 *   code >= 0x10 is a report id.
 *
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>

#define SYNA_MESSAGE_MARKER		0xa5
#define SYNA_MESSAGE_PADDING		0x5a
#define SYNA_HEADER_SIZE		4

/*
 * Read length limit of the controller (RD_CHUNK_SIZE in the vendor driver).
 * One read may not exceed this, including the header.
 */
#define SYNA_RD_CHUNK_SIZE		64

/* Commands. */
#define SYNA_CMD_IDENTIFY		0x02
#define SYNA_CMD_ENABLE_REPORT		0x05
#define SYNA_CMD_DISABLE_REPORT		0x06
#define SYNA_CMD_GET_APP_INFO		0x20
#define SYNA_CMD_GET_DYNAMIC_CONFIG	0x23
#define SYNA_CMD_SET_DYNAMIC_CONFIG	0x24
#define SYNA_CMD_GET_TOUCH_REPORT_CONFIG 0x25
#define SYNA_CMD_SET_TOUCH_REPORT_CONFIG 0x26
#define SYNA_CMD_ENTER_DEEP_SLEEP	0x2c
#define SYNA_CMD_EXIT_DEEP_SLEEP	0x2d

/* Dynamic config IDs. */
#define SYNA_DC_IN_WAKEUP_GESTURE_MODE	0x09
#define SYNA_DC_GESTURE_MASK		0xfe

/* Status codes, i.e. header codes of a command response. */
#define SYNA_STATUS_IDLE		0x00
#define SYNA_STATUS_OK			0x01
#define SYNA_STATUS_BUSY		0x02
#define SYNA_STATUS_CONTINUED_READ	0x03
#define SYNA_STATUS_ERROR		0x0f
#define SYNA_STATUS_INVALID		0xff

/* Report ids. Anything >= SYNA_REPORT_IDENTIFY is a report, not a status. */
#define SYNA_REPORT_IDENTIFY		0x10
#define SYNA_REPORT_TOUCH		0x11
#define SYNA_REPORT_LOG			0x1d

#define SYNA_MODE_APPLICATION		0x01

/* Gesture opcodes & IDs. */
#define SYNA_GESTURE_DTAP		0x01

/* Opcodes of the touch report config. */
enum syna_touch_report_code {
	SYNA_TOUCH_END				= 0,
	SYNA_TOUCH_FOREACH_ACTIVE_OBJECT	= 1,
	SYNA_TOUCH_FOREACH_OBJECT		= 2,
	SYNA_TOUCH_FOREACH_END			= 3,
	SYNA_TOUCH_PAD_TO_NEXT_BYTE		= 4,
	SYNA_TOUCH_TIMESTAMP			= 5,
	SYNA_TOUCH_OBJECT_N_INDEX		= 6,
	SYNA_TOUCH_OBJECT_N_CLASSIFICATION	= 7,
	SYNA_TOUCH_OBJECT_N_X_POSITION		= 8,
	SYNA_TOUCH_OBJECT_N_Y_POSITION		= 9,
	SYNA_TOUCH_OBJECT_N_Z			= 10,
	SYNA_TOUCH_OBJECT_N_X_WIDTH		= 11,
	SYNA_TOUCH_OBJECT_N_Y_WIDTH		= 12,
	SYNA_TOUCH_GESTURE_DOUBLE_TAP		= 16,
	SYNA_TOUCH_NUM_OF_ACTIVE_OBJECTS	= 24,
	SYNA_TOUCH_REPORT_GESTURE_SWIPE		= 193,
	SYNA_TOUCH_REPORT_GESTURE_CIRCLE	= 194,
	SYNA_TOUCH_REPORT_GESTURE_UNICODE	= 195,
	SYNA_TOUCH_REPORT_GESTURE_VEE		= 196,
	SYNA_TOUCH_REPORT_GESTURE_TRIANGLE	= 197,
	SYNA_TOUCH_REPORT_GESTURE_INFO		= 198,
	SYNA_TOUCH_REPORT_GESTURE_COORDINATE	= 199,
};

/* Object classification. Anything other than LIFT counts as a contact. */
#define SYNA_OBJECT_LIFT		0

#define SYNA_MAX_OBJECTS		16
#define SYNA_MAX_CONFIG_SIZE		128
#define SYNA_MAX_MESSAGE_SIZE		1024

/* Vendor driver: POWERUP_TO_RESET_TIME and RESET_TO_NORMAL_TIME. */
#define SYNA_POWERUP_TO_RESET_MS	10
#define SYNA_RESET_TO_NORMAL_MS		1000

#define SYNA_RESPONSE_TIMEOUT_MS	500

struct syna_tcm_app_info {
	__le16 version;
	__le16 status;
	__le16 static_config_size;
	__le16 dynamic_config_size;
	__le16 app_config_start_write_block;
	__le16 app_config_size;
	__le16 max_touch_report_config_size;
	__le16 max_touch_report_payload_size;
	u8 customer_config_id[16];
	__le16 max_x;
	__le16 max_y;
	__le16 max_objects;
	__le16 num_of_buttons;
	__le16 num_of_image_rows;
	__le16 num_of_image_cols;
	__le16 has_hybrid_data;
} __packed;

struct syna_object {
	u32 status;
	u32 x;
	u32 y;
	u32 z;
	u32 wx;
	u32 wy;
};

struct syna_tcm {
	struct i2c_client *client;
	struct input_dev *input;
	struct touchscreen_properties prop;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];

	/* Protects hardware I2C transfers and internal state */
	struct mutex lock;
	bool suspended;
	bool gesture_enabled;
	bool powered;
	u32 gesture_type;

	/* Scratch for one I2C read, and the message assembled out of them. */
	u8 rxbuf[SYNA_RD_CHUNK_SIZE];
	u8 payload[SYNA_MAX_MESSAGE_SIZE];
	unsigned int payload_len;
	u8 code;

	u8 cmdbuf[SYNA_MAX_CONFIG_SIZE + 3];

	u8 config[SYNA_MAX_CONFIG_SIZE];
	unsigned int config_len;

	u8 normal_config[SYNA_MAX_CONFIG_SIZE];
	unsigned int normal_config_len;

	unsigned int max_objects;
	struct syna_object objects[SYNA_MAX_OBJECTS];
};

/*
 * Low-power gesture report config matching the stock vendor driver:
 * Double-tap ID, gesture info, coordinates, and active object loop.
 */
static const u8 syna_gesture_config[] = {
	SYNA_TOUCH_GESTURE_DOUBLE_TAP,		8,
	SYNA_TOUCH_REPORT_GESTURE_INFO,		48,
	SYNA_TOUCH_REPORT_GESTURE_COORDINATE,	192,
	SYNA_TOUCH_FOREACH_ACTIVE_OBJECT,
	SYNA_TOUCH_OBJECT_N_INDEX,		4,
	SYNA_TOUCH_OBJECT_N_CLASSIFICATION,	4,
	SYNA_TOUCH_OBJECT_N_X_POSITION,		16,
	SYNA_TOUCH_OBJECT_N_Y_POSITION,		16,
	SYNA_TOUCH_FOREACH_END,
	SYNA_TOUCH_END,
};

/*
 * Receive one whole message. On success ts->code holds the header code and the
 * first ts->payload_len bytes of ts->payload hold its payload.
 *
 * Must be called with ts->lock held.
 */
static int syna_recv_message(struct syna_tcm *ts)
{
	struct device *dev = &ts->client->dev;
	unsigned int total, copied, stream_remaining;
	int ret;

	ret = i2c_master_recv(ts->client, ts->rxbuf, SYNA_RD_CHUNK_SIZE);
	if (ret < 0)
		return ret;

	if (ret < SYNA_HEADER_SIZE) {
		dev_warn_ratelimited(dev, "i2c recv short read: %d\n", ret);
		return -EIO;
	}

	if (ts->rxbuf[0] != SYNA_MESSAGE_MARKER) {
		dev_dbg(dev, "bad message marker 0x%02x\n", ts->rxbuf[0]);
		return -EPROTO;
	}

	ts->code = ts->rxbuf[1];
	ts->payload_len = get_unaligned_le16(&ts->rxbuf[2]);

	/*
	 * An idle or busy controller answers with a header and nothing behind
	 * it, whatever the length field happens to say.
	 */
	if (ts->code == SYNA_STATUS_IDLE || ts->code == SYNA_STATUS_BUSY ||
	    ts->code == SYNA_STATUS_CONTINUED_READ) {
		ts->payload_len = 0;
		return 0;
	}

	if (ts->payload_len > sizeof(ts->payload)) {
		dev_err(dev, "message payload of %u bytes is too long\n",
			ts->payload_len);
		return -EMSGSIZE;
	}

	/* Total bytes on wire for the message: header + payload + padding. */
	total = SYNA_HEADER_SIZE + ts->payload_len + 1;
	copied = min(ts->payload_len, SYNA_RD_CHUNK_SIZE - SYNA_HEADER_SIZE);
	memcpy(ts->payload, &ts->rxbuf[SYNA_HEADER_SIZE], copied);

	if (total <= SYNA_RD_CHUNK_SIZE) {
		if (ts->rxbuf[total - 1] != SYNA_MESSAGE_PADDING) {
			dev_dbg(dev, "bad message padding 0x%02x\n",
				ts->rxbuf[total - 1]);
			return -EPROTO;
		}
		return 0;
	}

	/*
	 * Continued read: remaining stream bytes (payload + trailing 0x5a
	 * padding) arrive in chunks with a 2-byte header (marker + code 0x03).
	 * Each continued-read transfer can carry up to SYNA_RD_CHUNK_SIZE - 2
	 * bytes of stream data.
	 */
	stream_remaining = total - SYNA_RD_CHUNK_SIZE;
	while (stream_remaining) {
		unsigned int chunk = min(stream_remaining,
					 (unsigned int)(SYNA_RD_CHUNK_SIZE - 2));
		unsigned int read_len = chunk + 2;
		unsigned int payload_chunk;

		ret = i2c_master_recv(ts->client, ts->rxbuf, read_len);
		if (ret < 0)
			return ret;

		if (ret < read_len)
			return -EIO;

		if (ts->rxbuf[0] != SYNA_MESSAGE_MARKER ||
		    ts->rxbuf[1] != SYNA_STATUS_CONTINUED_READ) {
			dev_err(dev, "bad continued-read header 0x%02x 0x%02x\n",
				ts->rxbuf[0], ts->rxbuf[1]);
			return -EPROTO;
		}

		payload_chunk = min(chunk, ts->payload_len - copied);
		if (payload_chunk) {
			memcpy(&ts->payload[copied], &ts->rxbuf[2], payload_chunk);
			copied += payload_chunk;
		}

		stream_remaining -= chunk;

		if (!stream_remaining &&
		    ts->rxbuf[2 + chunk - 1] != SYNA_MESSAGE_PADDING) {
			dev_dbg(dev, "bad continued-read padding 0x%02x\n",
				ts->rxbuf[2 + chunk - 1]);
			return -EPROTO;
		}
	}

	return 0;
}

/**
 * syna_exec_command() - Issue a command and collect its response
 * @ts: Synaptics TCM device context
 * @cmd: Command opcode (SYNA_CMD_*)
 * @payload: Command payload buffer, or NULL
 * @len: Length of @payload in bytes
 *
 * Issue a command and collect its response into ts->payload. Reports that
 * arrive while waiting are dropped.
 *
 * Context: Must be called with @ts->lock held (except during probe before
 *          the interrupt handler is registered).
 * Return: 0 on success, or a negative errno.
 */
static int syna_exec_command(struct syna_tcm *ts, u8 cmd,
			     const u8 *payload, unsigned int len)
{
	struct device *dev = &ts->client->dev;
	unsigned long deadline;
	u8 *buf = ts->cmdbuf;
	int ret;

	if (len + 3 > sizeof(ts->cmdbuf))
		return -EINVAL;

	buf[0] = cmd;
	put_unaligned_le16(len, &buf[1]);
	if (len)
		memcpy(&buf[3], payload, len);

	ret = i2c_master_send(ts->client, buf, len + 3);
	if (ret < 0) {
		dev_err(dev, "failed to write command 0x%02x: %d\n", cmd, ret);
		return ret;
	}

	deadline = jiffies + msecs_to_jiffies(SYNA_RESPONSE_TIMEOUT_MS);
	do {
		usleep_range(15000, 20000);

		ret = syna_recv_message(ts);
		if (ret == -EPROTO)
			continue;	/* controller not talking yet */
		if (ret < 0)
			return ret;

		if (ts->code == SYNA_REPORT_IDENTIFY && cmd == SYNA_CMD_IDENTIFY)
			return 0;

		if (ts->code >= SYNA_REPORT_IDENTIFY)
			continue;	/* a report, not our response */

		switch (ts->code) {
		case SYNA_STATUS_OK:
			return 0;
		case SYNA_STATUS_IDLE:
		case SYNA_STATUS_BUSY:
			continue;
		default:
			dev_err(dev, "command 0x%02x failed with status 0x%02x\n",
				cmd, ts->code);
			return -EIO;
		}
	} while (time_before(jiffies, deadline));

	dev_err(dev, "command 0x%02x timed out\n", cmd);
	return -ETIMEDOUT;
}

static int syna_set_dynamic_config(struct syna_tcm *ts, u8 id, u16 value)
{
	u8 buf[3];

	buf[0] = id;
	put_unaligned_le16(value, &buf[1]);

	return syna_exec_command(ts, SYNA_CMD_SET_DYNAMIC_CONFIG, buf, sizeof(buf));
}

/* Extract a big field of @bits from the report's bit stream at @offset. */
static u32 syna_get_bits(const u8 *buf, unsigned int buf_len,
			 unsigned int offset, unsigned int bits)
{
	unsigned int byte, bit, done = 0;
	u32 out = 0;

	if (!bits || bits > 32 || bits > buf_len * 8 ||
	    offset > buf_len * 8 - bits)
		return 0;

	byte = offset / 8;
	bit = offset % 8;

	while (done < bits) {
		unsigned int avail = 8 - bit;
		unsigned int take = min(avail, bits - done);
		u8 val = (buf[byte] >> bit) & (0xff >> (8 - take));

		out |= (u32)val << done;
		done += take;
		bit = 0;
		byte++;
	}

	return out;
}

/*
 * Walk the touch report config and unpack the report accordingly.
 */
static int syna_parse_touch_report(struct syna_tcm *ts)
{
	const u8 *cfg = ts->config;
	unsigned int idx = 0, loop_start = 0, loop_bits = 0;
	unsigned int offset = 0;	/* bit offset into the payload */
	unsigned int obj = 0;
	unsigned int seen = 0, active = 0;
	bool active_only = false, have_active_count = false;
	bool done = false;
	unsigned int i;

	memset(ts->objects, 0, sizeof(ts->objects));
	ts->gesture_type = 0;

	while (idx < ts->config_len && !done) {
		u8 code = cfg[idx++];
		unsigned int bits;
		u32 data;

		switch (code) {
		case SYNA_TOUCH_END:
			done = true;
			continue;

		case SYNA_TOUCH_FOREACH_ACTIVE_OBJECT:
		case SYNA_TOUCH_FOREACH_OBJECT:
			active_only = code == SYNA_TOUCH_FOREACH_ACTIVE_OBJECT;
			loop_start = idx;
			obj = 0;
			seen = 0;
			loop_bits = 0;
			for (i = idx; i < ts->config_len; i++) {
				if (cfg[i] == SYNA_TOUCH_FOREACH_END ||
				    cfg[i] == SYNA_TOUCH_END)
					break;
				if (cfg[i] == SYNA_TOUCH_PAD_TO_NEXT_BYTE)
					continue;
				if (++i < ts->config_len)
					loop_bits += cfg[i];
			}
			if (active_only && !have_active_count &&
			    offset + loop_bits > ts->payload_len * 8) {
				/* Not enough payload for even one object */
				if (i < ts->config_len && cfg[i] == SYNA_TOUCH_FOREACH_END)
					idx = i + 1;
				else
					idx = i;
			}
			continue;

		case SYNA_TOUCH_FOREACH_END:
			if (!active_only) {
				if (++obj < ts->max_objects)
					idx = loop_start;
			} else if (have_active_count) {
				if (++seen < active)
					idx = loop_start;
			} else if (++seen < ts->max_objects &&
				   offset + loop_bits <= ts->payload_len * 8) {
				idx = loop_start;
			}
			continue;

		case SYNA_TOUCH_PAD_TO_NEXT_BYTE:
			offset = ALIGN(offset, 8);
			continue;
		}

		/* Everything else is (opcode, bit width). */
		if (idx >= ts->config_len || offset >= ts->payload_len * 8)
			break;

		bits = cfg[idx++];
		data = syna_get_bits(ts->payload, ts->payload_len, offset, bits);
		offset += bits;

		switch (code) {
		case SYNA_TOUCH_OBJECT_N_INDEX:
			if (data >= ts->max_objects || data >= SYNA_MAX_OBJECTS) {
				dev_dbg(&ts->client->dev,
					"object index %u out of range\n", data);
				return -ERANGE;
			}
			obj = data;
			break;
		case SYNA_TOUCH_NUM_OF_ACTIVE_OBJECTS:
			active = min_t(unsigned int, data, ts->max_objects);
			have_active_count = true;
			if (!active)
				done = true;
			break;
		case SYNA_TOUCH_OBJECT_N_CLASSIFICATION:
			ts->objects[obj].status = data;
			break;
		case SYNA_TOUCH_OBJECT_N_X_POSITION:
			ts->objects[obj].x = data;
			break;
		case SYNA_TOUCH_OBJECT_N_Y_POSITION:
			ts->objects[obj].y = data;
			break;
		case SYNA_TOUCH_OBJECT_N_Z:
			ts->objects[obj].z = data;
			break;
		case SYNA_TOUCH_OBJECT_N_X_WIDTH:
			ts->objects[obj].wx = data;
			break;
		case SYNA_TOUCH_OBJECT_N_Y_WIDTH:
			ts->objects[obj].wy = data;
			break;
		case SYNA_TOUCH_GESTURE_DOUBLE_TAP:
		case SYNA_TOUCH_REPORT_GESTURE_SWIPE:
		case SYNA_TOUCH_REPORT_GESTURE_CIRCLE:
		case SYNA_TOUCH_REPORT_GESTURE_UNICODE:
		case SYNA_TOUCH_REPORT_GESTURE_VEE:
		case SYNA_TOUCH_REPORT_GESTURE_TRIANGLE:
			ts->gesture_type = data;
			break;
		default:
			/* Timestamp, grip info, coordinates: skipped. */
			break;
		}
	}

	return 0;
}

static void syna_report_touch(struct syna_tcm *ts)
{
	unsigned int i;

	for (i = 0; i < ts->max_objects; i++) {
		struct syna_object *obj = &ts->objects[i];
		bool down = obj->status != SYNA_OBJECT_LIFT;

		if (down)
			dev_dbg(&ts->client->dev,
				"slot %u: %u,%u z=%u w=%ux%u class=%u\n",
				i, obj->x, obj->y, obj->z, obj->wx, obj->wy,
				obj->status);

		input_mt_slot(ts->input, i);
		if (!input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, down))
			continue;

		touchscreen_report_pos(ts->input, &ts->prop, obj->x, obj->y, true);
		input_report_abs(ts->input, ABS_MT_PRESSURE, obj->z);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR,
				 max(obj->wx, obj->wy));
		input_report_abs(ts->input, ABS_MT_TOUCH_MINOR,
				 min(obj->wx, obj->wy));
	}

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t syna_irq(int irq, void *data)
{
	struct syna_tcm *ts = data;

	mutex_lock(&ts->lock);

	if (syna_recv_message(ts) < 0) {
		mutex_unlock(&ts->lock);
		return IRQ_HANDLED;
	}

	if (ts->code != SYNA_REPORT_TOUCH) {
		mutex_unlock(&ts->lock);
		return IRQ_HANDLED;
	}

	if (syna_parse_touch_report(ts) == 0) {
		if (ts->suspended) {
			if (ts->gesture_type == SYNA_GESTURE_DTAP) {
				dev_dbg(&ts->client->dev,
					"wakeup gesture detected (type=0x%02x)\n",
					ts->gesture_type);

				input_report_key(ts->input, KEY_WAKEUP, 1);
				input_sync(ts->input);
				input_report_key(ts->input, KEY_WAKEUP, 0);
				input_sync(ts->input);

				pm_wakeup_event(&ts->client->dev, 0);
			}
		} else {
			syna_report_touch(ts);
		}
	}

	mutex_unlock(&ts->lock);
	return IRQ_HANDLED;
}

static int syna_power_on(struct syna_tcm *ts)
{
	int ret;

	if (ts->powered)
		return 0;

	/* Held in reset across power-up; the DT flag makes this drive low. */
	gpiod_set_value_cansleep(ts->reset_gpio, 1);

	ret = regulator_bulk_enable(ARRAY_SIZE(ts->supplies), ts->supplies);
	if (ret)
		return ret;

	ts->powered = true;

	msleep(SYNA_POWERUP_TO_RESET_MS);
	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	msleep(SYNA_RESET_TO_NORMAL_MS);

	return 0;
}

static void syna_power_off(struct syna_tcm *ts)
{
	if (!ts->powered)
		return;

	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ts->supplies), ts->supplies);
	ts->powered = false;
}

static void syna_power_off_action(void *data)
{
	struct syna_tcm *ts = data;

	syna_power_off(ts);
}

/*
 * Fallback touch report config installed only if the firmware comes up
 * without coordinates.
 */
static const u8 syna_default_config[] = {
	SYNA_TOUCH_FOREACH_ACTIVE_OBJECT,
	SYNA_TOUCH_OBJECT_N_INDEX,		4,
	SYNA_TOUCH_OBJECT_N_CLASSIFICATION,	4,
	SYNA_TOUCH_OBJECT_N_X_POSITION,		16,
	SYNA_TOUCH_OBJECT_N_Y_POSITION,		16,
	SYNA_TOUCH_OBJECT_N_Z,			8,
	SYNA_TOUCH_OBJECT_N_X_WIDTH,		12,
	SYNA_TOUCH_OBJECT_N_Y_WIDTH,		12,
	SYNA_TOUCH_FOREACH_END,
	SYNA_TOUCH_END,
};

static bool syna_config_has_coords(const u8 *cfg, unsigned int len)
{
	bool x = false, y = false;
	unsigned int i;

	for (i = 0; i < len; i++) {
		switch (cfg[i]) {
		case SYNA_TOUCH_END:
			return x && y;
		case SYNA_TOUCH_FOREACH_ACTIVE_OBJECT:
		case SYNA_TOUCH_FOREACH_OBJECT:
		case SYNA_TOUCH_FOREACH_END:
		case SYNA_TOUCH_PAD_TO_NEXT_BYTE:
			break;
		default:
			if (cfg[i] == SYNA_TOUCH_OBJECT_N_X_POSITION)
				x = true;
			else if (cfg[i] == SYNA_TOUCH_OBJECT_N_Y_POSITION)
				y = true;
			i++;	/* skip the bit width */
			break;
		}
	}

	return x && y;
}

static int syna_read_touch_report_config(struct syna_tcm *ts)
{
	struct device *dev = &ts->client->dev;
	int ret;

	ret = syna_exec_command(ts, SYNA_CMD_GET_TOUCH_REPORT_CONFIG, NULL, 0);
	if (ret)
		return ret;

	if (!ts->payload_len || ts->payload_len > sizeof(ts->config)) {
		dev_err(dev, "implausible touch report config size %u\n",
			ts->payload_len);
		return -EPROTO;
	}

	ts->config_len = ts->payload_len;
	memcpy(ts->config, ts->payload, ts->config_len);

	dev_dbg(dev, "touch report config: %*ph\n", (int)ts->config_len, ts->config);

	return 0;
}

static int syna_setup(struct syna_tcm *ts)
{
	struct device *dev = &ts->client->dev;
	struct syna_tcm_app_info *app_info;
	unsigned int max_x = 0, max_y = 0;
	u8 report = SYNA_REPORT_TOUCH;
	int ret;

	ret = syna_exec_command(ts, SYNA_CMD_IDENTIFY, NULL, 0);
	if (ret)
		return ret;

	if (ts->payload_len < 18)
		return -EPROTO;

	dev_info(dev, "TouchComm v%u, mode 0x%02x, part %.16s\n",
		 ts->payload[0], ts->payload[1], &ts->payload[2]);

	if (ts->payload[1] != SYNA_MODE_APPLICATION) {
		dev_err(dev,
			"controller is in mode 0x%02x, not application mode; this driver cannot download firmware\n",
			ts->payload[1]);
		return -ENODEV;
	}

	ret = syna_exec_command(ts, SYNA_CMD_GET_APP_INFO, NULL, 0);
	if (ret)
		return ret;

	if (ts->payload_len < sizeof(*app_info))
		return -EPROTO;

	app_info = (struct syna_tcm_app_info *)ts->payload;
	max_x = le16_to_cpu(app_info->max_x);
	max_y = le16_to_cpu(app_info->max_y);
	ts->max_objects = le16_to_cpu(app_info->max_objects);

	if (!ts->max_objects)
		ts->max_objects = 10;

	if (ts->max_objects > SYNA_MAX_OBJECTS) {
		dev_warn(dev, "clamping %u objects to %u\n",
			 ts->max_objects, SYNA_MAX_OBJECTS);
		ts->max_objects = SYNA_MAX_OBJECTS;
	}

	if (!max_x || !max_y) {
		dev_err(dev, "touchscreen size not reported by firmware\n");
		return -EINVAL;
	}

	dev_info(dev, "%ux%u, %u touch objects\n", max_x + 1, max_y + 1,
		 ts->max_objects);

	ret = syna_read_touch_report_config(ts);
	if (ret)
		return ret;

	if (!syna_config_has_coords(ts->config, ts->config_len)) {
		dev_warn(dev,
			 "firmware touch report config carries no coordinates, installing our own\n");

		ret = syna_exec_command(ts, SYNA_CMD_SET_TOUCH_REPORT_CONFIG,
					syna_default_config,
					sizeof(syna_default_config));
		if (ret)
			return ret;

		ret = syna_read_touch_report_config(ts);
		if (ret)
			return ret;

		if (!syna_config_has_coords(ts->config, ts->config_len)) {
			dev_err(dev, "controller kept a config we cannot parse\n");
			return -ENODEV;
		}
	}

	/* Cache the normal report config for restoring after gesture wakeup */
	ts->normal_config_len = ts->config_len;
	memcpy(ts->normal_config, ts->config, ts->normal_config_len);

	ret = syna_exec_command(ts, SYNA_CMD_ENABLE_REPORT, &report, 1);
	if (ret)
		return ret;

	ts->input->name = "Synaptics TouchComm Touchscreen";
	ts->input->id.bustype = BUS_I2C;

	input_set_capability(ts->input, EV_KEY, KEY_WAKEUP);

	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, max_x, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
	/*
	 * ABS_MT_PRESSURE (8-bit, 0-255) and ABS_MT_TOUCH_MAJOR/MINOR (12-bit,
	 * 0-4095) ranges are tailored for the S3908 / generation-1 family with
	 * the default touch report layout (syna_default_config).
	 */
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 4095, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 4095, 0, 0);

	touchscreen_parse_properties(ts->input, true, &ts->prop);

	return input_mt_init_slots(ts->input, ts->max_objects,
				   INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
}

static int syna_reinit_hw(struct syna_tcm *ts)
{
	struct device *dev = &ts->client->dev;
	u8 report = SYNA_REPORT_TOUCH;
	int ret;

	syna_power_off(ts);
	ret = syna_power_on(ts);
	if (ret)
		return ret;

	ret = syna_exec_command(ts, SYNA_CMD_IDENTIFY, NULL, 0);
	if (ret)
		goto err_power_off;

	if (ts->payload_len < 18 || ts->payload[1] != SYNA_MODE_APPLICATION) {
		dev_err(dev, "controller not in application mode after recovery\n");
		ret = -ENODEV;
		goto err_power_off;
	}

	ret = syna_exec_command(ts, SYNA_CMD_SET_TOUCH_REPORT_CONFIG,
				ts->normal_config, ts->normal_config_len);
	if (ret)
		goto err_power_off;

	ret = syna_read_touch_report_config(ts);
	if (ret)
		goto err_power_off;

	ret = syna_exec_command(ts, SYNA_CMD_ENABLE_REPORT, &report, 1);
	if (ret)
		goto err_power_off;

	return 0;

err_power_off:
	syna_power_off(ts);
	return ret;
}

static int syna_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct syna_tcm *ts;
	int ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	ret = devm_mutex_init(dev, &ts->lock);
	if (ret)
		return ret;

	i2c_set_clientdata(client, ts);

	ts->input = devm_input_allocate_device(dev);
	if (!ts->input)
		return -ENOMEM;

	ts->supplies[0].supply = "vdd";
	ts->supplies[1].supply = "avdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ts->supplies),
				      ts->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get supplies\n");

	ts->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->reset_gpio),
				     "failed to get reset GPIO\n");

	ret = syna_power_on(ts);
	if (ret)
		return dev_err_probe(dev, ret, "failed to power up\n");

	ret = devm_add_action_or_reset(dev, syna_power_off_action, ts);
	if (ret)
		return ret;

	ret = syna_setup(ts);
	if (ret)
		return ret;

	ret = input_register_device(ts->input);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register input device\n");

	if (device_property_read_bool(dev, "wakeup-source")) {
		ret = devm_device_init_wakeup(dev);
		if (ret)
			return ret;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL, syna_irq,
					IRQF_ONESHOT, "synaptics-tcm", ts);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ %d\n",
				     client->irq);

	return 0;
}

static int syna_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct syna_tcm *ts = i2c_get_clientdata(client);
	int ret;

	disable_irq(client->irq);
	mutex_lock(&ts->lock);

	if (device_may_wakeup(dev)) {
		/* Exit deep sleep if the controller was in sleep mode */
		ret = syna_exec_command(ts, SYNA_CMD_EXIT_DEEP_SLEEP, NULL, 0);
		if (ret && ret != -ETIMEDOUT)
			dev_dbg(dev, "exit deep sleep status: %d\n", ret);

		/* Switch to gesture touch report layout */
		ret = syna_exec_command(ts, SYNA_CMD_SET_TOUCH_REPORT_CONFIG,
					syna_gesture_config,
					sizeof(syna_gesture_config));
		if (ret)
			dev_warn(dev, "failed to set gesture report config: %d\n", ret);

		ret = syna_read_touch_report_config(ts);
		if (ret)
			dev_warn(dev, "failed to read touch report config: %d\n", ret);

		/* Enable Low Power Wakeup Gesture Mode */
		ret = syna_set_dynamic_config(ts, SYNA_DC_IN_WAKEUP_GESTURE_MODE, 1);
		if (ret)
			dev_warn(dev, "failed to enable wakeup gesture mode: %d\n", ret);

		/* Set gesture mask: bit 0 = DTAP (double-tap to wake) */
		ret = syna_set_dynamic_config(ts, SYNA_DC_GESTURE_MASK, BIT(0));
		if (ret)
			dev_warn(dev, "failed to set gesture mask: %d\n", ret);

		enable_irq_wake(client->irq);
		enable_irq(client->irq);
		ts->gesture_enabled = true;
	} else {
		/* Deep sleep mode when wakeup is disabled */
		ret = syna_exec_command(ts, SYNA_CMD_ENTER_DEEP_SLEEP, NULL, 0);
		if (ret)
			dev_warn(dev, "failed to enter deep sleep: %d\n", ret);

		ts->gesture_enabled = false;
	}

	ts->suspended = true;
	mutex_unlock(&ts->lock);

	return 0;
}

static int syna_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct syna_tcm *ts = i2c_get_clientdata(client);
	int ret;

	if (ts->gesture_enabled) {
		disable_irq(client->irq);
		mutex_lock(&ts->lock);
		disable_irq_wake(client->irq);

		/* Exit Wakeup Gesture Mode */
		ret = syna_set_dynamic_config(ts, SYNA_DC_IN_WAKEUP_GESTURE_MODE, 0);
		if (ret)
			dev_warn(dev, "failed to disable wakeup gesture mode: %d\n", ret);

		/* Restore normal touch report layout */
		ret = syna_exec_command(ts, SYNA_CMD_SET_TOUCH_REPORT_CONFIG,
					ts->normal_config,
					ts->normal_config_len);
		if (ret)
			dev_warn(dev, "failed to restore normal report config: %d\n", ret);

		ret = syna_read_touch_report_config(ts);
		if (ret)
			dev_warn(dev, "failed to read touch report config: %d\n", ret);

		ts->gesture_enabled = false;
		ts->suspended = false;
		enable_irq(client->irq);
	} else {
		mutex_lock(&ts->lock);

		/* Wake up controller from deep sleep */
		ret = syna_exec_command(ts, SYNA_CMD_EXIT_DEEP_SLEEP, NULL, 0);
		if (ret) {
			dev_warn(dev, "failed to exit deep sleep (%d), recovering...\n", ret);
			ret = syna_reinit_hw(ts);
			if (ret) {
				dev_err(dev, "failed to recover controller: %d\n", ret);
				mutex_unlock(&ts->lock);
				return ret;
			}
		}

		ts->suspended = false;
		enable_irq(client->irq);
	}

	mutex_unlock(&ts->lock);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(syna_tcm_pm_ops, syna_suspend, syna_resume);

static const struct i2c_device_id syna_tcm_i2c_id[] = {
	{ "synaptics-tcm" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, syna_tcm_i2c_id);

static const struct of_device_id syna_tcm_of_match[] = {
	{ .compatible = "syna,s3908" },
	{ }
};
MODULE_DEVICE_TABLE(of, syna_tcm_of_match);

static struct i2c_driver syna_tcm_i2c_driver = {
	.driver = {
		.name = "synaptics-tcm-i2c",
		.of_match_table = syna_tcm_of_match,
		.pm = pm_sleep_ptr(&syna_tcm_pm_ops),
	},
	.probe = syna_probe,
	.id_table = syna_tcm_i2c_id,
};
module_i2c_driver(syna_tcm_i2c_driver);

MODULE_DESCRIPTION("Synaptics TouchComm I2C touchscreen driver");
MODULE_AUTHOR("Vsevolod Nevorotov <sevanevorotov29@gmail.com>");
MODULE_LICENSE("GPL");
