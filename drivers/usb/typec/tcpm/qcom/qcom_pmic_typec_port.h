/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */
#ifndef __QCOM_PMIC_TYPEC_PORT_H__
#define __QCOM_PMIC_TYPEC_PORT_H__

#include <linux/platform_device.h>
#include <linux/usb/tcpm.h>

/* Resources */
#define PMIC_TYPEC_MAX_IRQS				0x08

struct pmic_typec_port_irq_params {
	int				virq;
	char				*irq_name;
};

/**
 * struct pmic_typec_port_resources - per PMIC Type-C port description
 * @nr_irqs: number of entries in @irq_params
 * @irq_params: interrupts used by the port
 * @disable_from_snk_only: the Type-C block must be switched to sink only
 *			   mode before being disabled
 * @detach_on_start: force a detach when the port is started
 */
struct pmic_typec_port_resources {
	unsigned int				nr_irqs;
	const struct pmic_typec_port_irq_params	irq_params[PMIC_TYPEC_MAX_IRQS];
	bool					disable_from_snk_only;
	bool					detach_on_start;
};

/* API */

extern const struct pmic_typec_port_resources pm8150b_port_res;
extern const struct pmic_typec_port_resources pm8550b_port_res;

int qcom_pmic_typec_port_probe(struct platform_device *pdev,
			       struct pmic_typec *tcpm,
			       const struct pmic_typec_port_resources *res,
			       struct regmap *regmap,
			       u32 base);

#endif /* __QCOM_PMIC_TYPE_C_PORT_H__ */
