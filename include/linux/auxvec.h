/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_AUXVEC_H
#define _LINUX_AUXVEC_H

#include <uapi/asm/auxvec.h>
#include <uapi/linux/auxvec.h>
#include <asm/auxvec.h>

#define AT_VECTOR_SIZE_BASE 24 /* NEW_AUX_ENT entries in auxiliary table */
  /* number of "#define AT_.*" above, minus {AT_NULL, AT_IGNORE, AT_NOTELF} */

#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

#endif /* _LINUX_AUXVEC_H */
