/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ACPI_AEST_H__
#define __ACPI_AEST_H__

#include <linux/acpi.h>

#ifdef CONFIG_ACPI_AEST
void __init acpi_aest_init(void);
#else
static inline void acpi_aest_init(void) {}
#endif
#endif /* __ACPI_AEST_H__ */
