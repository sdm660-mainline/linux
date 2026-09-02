/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_PMIC_TYPEC_PORT_PM660_H__
#define __QCOM_PMIC_TYPEC_PORT_PM660_H__

#include "qcom_pmic_typec_port.h"

extern const struct pmic_typec_port_resources pm660_port_res;

int qcom_pmic_typec_pm660_port_probe(struct platform_device *pdev,
				      struct pmic_typec *tcpm,
				      const struct pmic_typec_port_resources *res,
				      struct regmap *regmap,
				      u32 base);

#endif /* __QCOM_PMIC_TYPEC_PORT_PM660_H__ */
