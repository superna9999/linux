/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_AUXVEC_H
#define _ASM_X86_AUXVEC_H

/* entries in ARCH_DLINFO: */
#if defined(CONFIG_IA32_EMULATION) || !defined(CONFIG_X86_64)
# define AT_VECTOR_SIZE_ARCH 3
#else /* else it's non-compat x86-64 */
# define AT_VECTOR_SIZE_ARCH 2
#endif

#endif /* _ASM_X86_AUXVEC_H */
