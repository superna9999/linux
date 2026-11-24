#ifndef __UAPI_ASMSPARC_AUXVEC_H
#define __UAPI_ASMSPARC_AUXVEC_H

#define AT_SYSINFO_EHDR		33

/* Avoid overlap with other AT_* values since they are consolidated in
 * glibc and any overlaps can cause problems
 */
#define AT_ADI_BLKSZ	48
#define AT_ADI_NBITS	49
#define AT_ADI_UEONADI	50

#endif /* !(__UAPI_ASMSPARC_AUXVEC_H) */
