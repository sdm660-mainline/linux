// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm PM660 USBIN Type-C port backend
 *
 * PM660 predates the standalone PM8150B-style Type-C register block.
 * Its CC/role controls live in the USBIN peripheral, while the PD PHY
 * is a separate PMIC peripheral.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/tcpm.h>

#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_port.h"
#include "qcom_pmic_typec_port_pm660.h"

/* PM660 peripheral bases */
#define PM660_USBIN_BASE				0x1300
#define PM660_MISC_BASE					0x1600

/* USBIN Type-C status */
#define PM660_TYPEC_STATUS_1_REG				0x0b
#define PM660_UFP_TYPEC_RDSTD				BIT(7)
#define PM660_UFP_TYPEC_RD1P5				BIT(6)
#define PM660_UFP_TYPEC_RD3P0				BIT(5)

#define PM660_TYPEC_STATUS_2_REG				0x0c
#define PM660_DFP_TYPEC_MASK				GENMASK(3, 0)
#define PM660_DFP_RD_OPEN				BIT(3)
#define PM660_DFP_RD_RA_VCONN				BIT(2)
#define PM660_DFP_RD_RD					BIT(1)
#define PM660_DFP_RA_RA					BIT(0)

#define PM660_TYPEC_STATUS_4_REG				0x0e
#define PM660_UFP_DFP_MODE_STATUS			BIT(7)
#define PM660_TYPEC_VBUS_STATUS				BIT(6)
#define PM660_CC_ORIENTATION				BIT(1)
#define PM660_CC_ATTACHED				BIT(0)

/* USBIN Type-C configuration */
#define PM660_TYPEC_CFG_REG				0x58
#define PM660_TYPEC_OR_U_USB				BIT(0)

#define PM660_TYPEC_CFG_2_REG				0x59
#define PM660_EN_TRY_SOURCE_MODE				BIT(3)
#define PM660_EN_80UA_180UA_CUR_SOURCE			BIT(0)

#define PM660_TYPEC_CFG_3_REG				0x5a
#define PM660_EN_TRYSINK_MODE				BIT(2)

#define PM660_TYPEC_INTRPT_ENB_REG			0x67
#define PM660_CCSTATE_CHANGE_INT_EN			BIT(2)

#define PM660_TYPEC_SW_CTRL_REG				0x68
#define PM660_VCONN_EN_ORIENTATION			BIT(6)
#define PM660_VCONN_EN_SRC				BIT(4)
#define PM660_VCONN_EN_VALUE				BIT(3)
#define PM660_POWER_ROLE_CMD_MASK			GENMASK(2, 0)
#define PM660_UFP_EN_CMD				BIT(2)
#define PM660_DFP_EN_CMD				BIT(1)
#define PM660_TYPEC_DISABLE_CMD				BIT(0)

/* PM660 TYPEC_PBS_WA_BIT workaround used by the downstream SMB2 driver. */
#define PM660_TM_IO_DTEST4_SEL				0xe9
#define PM660_PBS_CRUDE_SENSOR_ENABLE			0xa5

struct pm660_typec_port {
	struct device		*dev;
	struct tcpm_port	*tcpm_port;
	struct regmap		*regmap;
	u32			base;
	int			irq;

	struct regulator	*vdd_vbus;
	bool			vbus_enabled;
	bool			vbus_present;
	bool			vbus_valid;
	struct mutex		lock;
};

const struct pmic_typec_port_resources pm660_port_res = {
	.nr_irqs = 1,
	.irq_params = {
		{
			.virq = 0,
			.irq_name = "type-c-change",
		},
	},
};

static int pm660_typec_read(struct pm660_typec_port *port,
			    unsigned int reg, unsigned int *val)
{
	return regmap_read(port->regmap, port->base + reg, val);
}

static int pm660_typec_pbs_wa(struct pm660_typec_port *port, bool sink)
{
	unsigned int reg = PM660_MISC_BASE + PM660_TM_IO_DTEST4_SEL;
	unsigned int val = sink ? 0 : PM660_PBS_CRUDE_SENSOR_ENABLE;
	int ret;

	ret = regmap_write(port->regmap, reg, val);
	if (ret)
		dev_warn(port->dev, "failed to update PM660 Type-C PBS workaround: %d\n",
			 ret);

	/*
	 * The downstream driver treats failure of this workaround write as
	 * non-fatal and continues with the role transition.
	 */
	return 0;
}

static int pm660_typec_set_power_role(struct pm660_typec_port *port,
				      unsigned int role)
{
	int ret;

	pm660_typec_pbs_wa(port, role == PM660_UFP_EN_CMD);

	ret = regmap_update_bits(port->regmap,
				 port->base + PM660_TYPEC_SW_CTRL_REG,
				 PM660_POWER_ROLE_CMD_MASK, role);
	if (ret)
		dev_err(port->dev, "failed to set Type-C power role: %d\n", ret);

	return ret;
}

static int pm660_typec_get_vbus(struct tcpc_dev *tcpc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pm660_typec_port *port = tcpm->port_priv;
	unsigned int status;
	int ret;

	mutex_lock(&port->lock);
	ret = pm660_typec_read(port, PM660_TYPEC_STATUS_4_REG, &status);
	if (!ret)
		ret = port->vbus_enabled || !!(status & PM660_TYPEC_VBUS_STATUS);
	mutex_unlock(&port->lock);

	return ret;
}

static int pm660_typec_set_vbus(struct tcpc_dev *tcpc, bool on, bool sink)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pm660_typec_port *port = tcpm->port_priv;
	bool changed = false;
	int ret = 0;

	(void)sink;

	mutex_lock(&port->lock);

	if (port->vbus_enabled == on)
		goto out;

	if (on)
		ret = regulator_enable(port->vdd_vbus);
	else
		ret = regulator_disable(port->vdd_vbus);

	if (!ret) {
		port->vbus_enabled = on;
		changed = true;
	}

out:
	mutex_unlock(&port->lock);

	/*
	 * set_vbus() may run during tcpm_register_port() before port_start()
	 * provides tcpm_port. Once registered, notify TCPM whenever the
	 * source regulator actually changes state. get_vbus() also accounts
	 * for vbus_enabled when PM660 does not reflect sourced VBUS in
	 * TYPEC_VBUS_STATUS.
	 */
	if (changed && port->tcpm_port)
		tcpm_vbus_change(port->tcpm_port);

	return ret;
}

static int pm660_typec_get_cc(struct tcpc_dev *tcpc,
			      enum typec_cc_status *cc1,
			      enum typec_cc_status *cc2)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pm660_typec_port *port = tcpm->port_priv;
	enum typec_cc_status active = TYPEC_CC_OPEN;
	unsigned int status1, status2, status4;
	bool orientation_cc2;
	int ret;

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;

	mutex_lock(&port->lock);

	ret = pm660_typec_read(port, PM660_TYPEC_STATUS_4_REG, &status4);
	if (ret)
		goto out;

	if (!(status4 & PM660_CC_ATTACHED))
		goto out;

	ret = pm660_typec_read(port, PM660_TYPEC_STATUS_1_REG, &status1);
	if (ret)
		goto out;

	ret = pm660_typec_read(port, PM660_TYPEC_STATUS_2_REG, &status2);
	if (ret)
		goto out;

	orientation_cc2 = !!(status4 & PM660_CC_ORIENTATION);

	if (status4 & PM660_UFP_DFP_MODE_STATUS) {
		/* Local port is DFP/source; decode the partner's Rd/Ra. */
		switch (status2 & PM660_DFP_TYPEC_MASK) {
		case PM660_DFP_RA_RA:
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			goto out;
		case PM660_DFP_RD_RD:
			*cc1 = TYPEC_CC_RD;
			*cc2 = TYPEC_CC_RD;
			goto out;
		case PM660_DFP_RD_RA_VCONN:
			active = TYPEC_CC_RD;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		case PM660_DFP_RD_OPEN:
			active = TYPEC_CC_RD;
			break;
		default:
			dev_dbg(port->dev, "unhandled PM660 DFP status %#x\n",
				status2);
			goto out;
		}
	} else {
		/* Local port is UFP/sink; decode the partner's advertised Rp. */
		switch (status1) {
		case PM660_UFP_TYPEC_RDSTD:
			active = TYPEC_CC_RP_DEF;
			break;
		case PM660_UFP_TYPEC_RD1P5:
			active = TYPEC_CC_RP_1_5;
			break;
		case PM660_UFP_TYPEC_RD3P0:
			active = TYPEC_CC_RP_3_0;
			break;
		default:
			dev_dbg(port->dev, "unhandled PM660 UFP status %#x\n",
				status1);
			goto out;
		}
	}

	if (orientation_cc2)
		*cc2 = active;
	else
		*cc1 = active;

out:
	mutex_unlock(&port->lock);
	return ret;
}

static int pm660_typec_set_cc(struct tcpc_dev *tcpc,
			      enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pm660_typec_port *port = tcpm->port_priv;
	unsigned int role;
	unsigned int rp_current = 0;
	int ret;

	switch (cc) {
	case TYPEC_CC_OPEN:
		role = PM660_TYPEC_DISABLE_CMD;
		break;
	case TYPEC_CC_RD:
		role = PM660_UFP_EN_CMD;
		break;
	case TYPEC_CC_RP_DEF:
		role = PM660_DFP_EN_CMD;
		rp_current = 0;
		break;
	case TYPEC_CC_RP_1_5:
		role = PM660_DFP_EN_CMD;
		rp_current = PM660_EN_80UA_180UA_CUR_SOURCE;
		break;
	case TYPEC_CC_RP_3_0:
		/*
		 * The downstream PM660 role setter only distinguishes the
		 * default and 1.5 A source-current modes. Do not invent a
		 * 3.0 A encoding here.
		 */
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

	mutex_lock(&port->lock);

	if (role == PM660_DFP_EN_CMD) {
		ret = regmap_update_bits(port->regmap,
					 port->base + PM660_TYPEC_CFG_2_REG,
					 PM660_EN_80UA_180UA_CUR_SOURCE,
					 rp_current);
		if (ret)
			goto out;
	}

	ret = pm660_typec_set_power_role(port, role);

out:
	mutex_unlock(&port->lock);
	return ret;
}

static int pm660_typec_set_polarity(struct tcpc_dev *tcpc,
				    enum typec_cc_polarity polarity)
{
	(void)tcpc;
	(void)polarity;

	/* USB PHY orientation is handled outside this PMIC Type-C block. */
	return 0;
}

static int pm660_typec_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pm660_typec_port *port = tcpm->port_priv;
	unsigned int status4, orientation, mask, val;
	int ret;

	mutex_lock(&port->lock);

	ret = pm660_typec_read(port, PM660_TYPEC_STATUS_4_REG, &status4);
	if (ret)
		goto out;

	/* VCONN is driven on the inactive CC pin. */
	orientation = (status4 & PM660_CC_ORIENTATION) ?
		      0 : PM660_VCONN_EN_ORIENTATION;

	if (on) {
		mask = PM660_VCONN_EN_ORIENTATION | PM660_VCONN_EN_VALUE;
		val = orientation | PM660_VCONN_EN_VALUE;
	} else {
		mask = PM660_VCONN_EN_VALUE;
		val = 0;
	}

	ret = regmap_update_bits(port->regmap,
				 port->base + PM660_TYPEC_SW_CTRL_REG,
				 mask, val);

out:
	mutex_unlock(&port->lock);
	return ret;
}

static int pm660_typec_start_toggling(struct tcpc_dev *tcpc,
				      enum typec_port_type port_type,
				      enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct pm660_typec_port *port = tcpm->port_priv;
	unsigned int role;
	unsigned int try_src = 0;
	unsigned int try_snk = 0;
	int ret;

	(void)cc;

	switch (port_type) {
	case TYPEC_PORT_SRC:
		role = PM660_DFP_EN_CMD;
		break;
	case TYPEC_PORT_SNK:
		role = PM660_UFP_EN_CMD;
		break;
	case TYPEC_PORT_DRP:
		role = 0;
		/*
		 * Bonito's downstream policy is sink-preferred. Keep hardware
		 * Try.SNK enabled while TCPM owns the policy state machine.
		 */
		try_snk = PM660_EN_TRYSINK_MODE;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&port->lock);

	ret = regmap_update_bits(port->regmap,
				 port->base + PM660_TYPEC_CFG_2_REG,
				 PM660_EN_TRY_SOURCE_MODE, try_src);
	if (ret)
		goto out;

	ret = regmap_update_bits(port->regmap,
				 port->base + PM660_TYPEC_CFG_3_REG,
				 PM660_EN_TRYSINK_MODE, try_snk);
	if (ret)
		goto out;

	ret = pm660_typec_set_power_role(port, role);

out:
	mutex_unlock(&port->lock);
	return ret;
}

static irqreturn_t pm660_typec_irq(int irq, void *data)
{
	struct pm660_typec_port *port = data;
	unsigned int status4;
	bool vbus;
	bool vbus_changed = false;

	(void)irq;

	/*
	 * PM660 exposes a single aggregate Type-C change interrupt. Always
	 * notify TCPM of CC changes. Only generate a VBUS event when the
	 * hardware VBUS state changes, or on the first observation so TCPM
	 * can establish the initial vSafe0V state.
	 */
	mutex_lock(&port->lock);

	if (!pm660_typec_read(port, PM660_TYPEC_STATUS_4_REG, &status4)) {
		vbus = !!(status4 & PM660_TYPEC_VBUS_STATUS);

		if (!port->vbus_valid || vbus != port->vbus_present) {
			port->vbus_present = vbus;
			port->vbus_valid = true;
			vbus_changed = true;
		}
	}

	mutex_unlock(&port->lock);

	if (port->tcpm_port) {
		if (vbus_changed)
			tcpm_vbus_change(port->tcpm_port);

		tcpm_cc_change(port->tcpm_port);
	}

	return IRQ_HANDLED;
}

static int pm660_typec_port_start(struct pmic_typec *tcpm,
				  struct tcpm_port *tcpm_port)
{
	struct pm660_typec_port *port = tcpm->port_priv;
	unsigned int irq_mask;
	int ret;

	/*
	 * TYPE_C_OR_U_USB == 0 selects the Type-C state machine on PM660.
	 */
	ret = regmap_update_bits(port->regmap,
				 port->base + PM660_TYPEC_CFG_REG,
				 PM660_TYPEC_OR_U_USB, 0);
	if (ret)
		return ret;

	/*
	 * PM660 exposes a single aggregate type-c-change interrupt.
	 * The downstream SMB2 implementation enables only CC state
	 * changes as the trigger for this interrupt.
	 */
	irq_mask = PM660_CCSTATE_CHANGE_INT_EN;

	ret = regmap_write(port->regmap,
			   port->base + PM660_TYPEC_INTRPT_ENB_REG,
			   irq_mask);
	if (ret)
		return ret;

	/* Select software control for VCONN, initially disabled. */
	ret = regmap_update_bits(port->regmap,
				 port->base + PM660_TYPEC_SW_CTRL_REG,
				 PM660_VCONN_EN_SRC | PM660_VCONN_EN_VALUE,
				 PM660_VCONN_EN_SRC);
	if (ret)
		return ret;

	port->tcpm_port = tcpm_port;
	enable_irq(port->irq);

	return 0;
}

static void pm660_typec_port_stop(struct pmic_typec *tcpm)
{
	struct pm660_typec_port *port = tcpm->port_priv;

	disable_irq(port->irq);
	port->tcpm_port = NULL;
}

int qcom_pmic_typec_pm660_port_probe(struct platform_device *pdev,
				      struct pmic_typec *tcpm,
				      const struct pmic_typec_port_resources *res,
				      struct regmap *regmap,
				      u32 base)
{
	struct device *dev = &pdev->dev;
	struct pm660_typec_port *port;
	struct fwnode_handle *connector;
	int ret;

	if (!res || res->nr_irqs != 1)
		return -EINVAL;

	if (base != PM660_USBIN_BASE)
		dev_warn(dev, "unexpected PM660 USBIN base %#x\n", base);

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	connector = device_get_named_child_node(dev, "connector");
	if (!connector)
		return -EINVAL;

	port->vdd_vbus = devm_of_regulator_get_optional(dev,
							 to_of_node(connector),
							 "vbus");
	fwnode_handle_put(connector);

	if (port->vdd_vbus == ERR_PTR(-ENODEV))
		port->vdd_vbus = devm_regulator_get(dev, "vdd-vbus");
	if (IS_ERR(port->vdd_vbus))
		return PTR_ERR(port->vdd_vbus);

	port->irq = platform_get_irq_byname(pdev,
					    res->irq_params[0].irq_name);
	if (port->irq < 0)
		return port->irq;

	port->dev = dev;
	port->regmap = regmap;
	port->base = base;
	mutex_init(&port->lock);

	ret = devm_request_threaded_irq(dev, port->irq, NULL,
					pm660_typec_irq,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					res->irq_params[0].irq_name, port);
	if (ret)
		return ret;

	tcpm->port_priv = port;

	tcpm->tcpc.get_vbus = pm660_typec_get_vbus;
	tcpm->tcpc.set_vbus = pm660_typec_set_vbus;
	tcpm->tcpc.get_cc = pm660_typec_get_cc;
	tcpm->tcpc.set_cc = pm660_typec_set_cc;
	tcpm->tcpc.set_polarity = pm660_typec_set_polarity;
	tcpm->tcpc.set_vconn = pm660_typec_set_vconn;
	tcpm->tcpc.start_toggling = pm660_typec_start_toggling;

	tcpm->port_start = pm660_typec_port_start;
	tcpm->port_stop = pm660_typec_port_stop;

	return 0;
}
