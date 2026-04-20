/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_PM8550VX_ACD5_GEN3_H
#define _QCOM_PM8550VX_ACD5_GEN3_H

#include "qcom,adc5-gen3-channels.h"

/* ADC channels for PM8550VX_ADC for PMIC5 Gen3 */
#define PM8550VS_ADC5_GEN3_REF_GND(sid)			((sid) << 8 | ADC5_GEN3_REF_GND)
#define PM8550VS_ADC5_GEN3_1P25VREF(sid)			((sid) << 8 | ADC5_GEN3_1P25VREF)
#define PM8550VS_ADC5_GEN3_VREF_VADC(sid)			((sid) << 8 | ADC5_GEN3_VREF_VADC)
#define PM8550VS_ADC5_GEN3_DIE_TEMP(sid)			((sid) << 8 | ADC5_GEN3_DIE_TEMP)

#define PM8550VE_ADC5_GEN3_OFFSET_REF(sid)			((sid) << 8 | ADC5_GEN3_REF_GND)
#define PM8550VE_ADC5_GEN3_1P25VREF(sid)			((sid) << 8 | ADC5_GEN3_1P25VREF)
#define PM8550VE_ADC5_GEN3_VREF_VADC(sid)			((sid) << 8 | ADC5_GEN3_VREF_VADC)
#define PM8550VE_ADC5_GEN3_DIE_TEMP(sid)		((sid) << 8 | ADC5_GEN3_DIE_TEMP)

#endif /* _QCOM_PM8550VX_ACD5_GEN3_H */
