
/******************************************************
* Change log:
*    11/07/2008: initial version
******************************************************/

#ifndef _MLAN_DECL_H_
#define _MLAN_DECL_H_

/** MLAN release version */
#define MLAN_RELEASE_VERSION		 "C204"


/** Constants below */

#ifdef __GNUC__
/** Structure packing begins */
#define MLAN_PACK_START
/** Structure packeing end */
#define MLAN_PACK_END  __attribute__((packed))
#else /* !__GNUC__ */
#ifdef PRAGMA_PACK
/** Structure packing begins */
#define MLAN_PACK_START
/** Structure packeing end */
#define MLAN_PACK_END
#else /* !PRAGMA_PACK */
/** Structure packing begins */
#define MLAN_PACK_START   __packed
/** Structure packing end */
#define MLAN_PACK_END
#endif /* PRAGMA_PACK */
#endif /* __GNUC__ */

#ifndef INLINE
#ifdef __GNUC__
/** inline directive */
#define	INLINE	inline
#else
/** inline directive */
#define	INLINE	inline
#endif
#endif

#define MNULL NULL
#define MLAN_STATUS_FAILURE (-1)
#define MLAN_STATUS_SUCCESS (0)

/** MLAN TRUE */
#define MTRUE                    (1)
/** MLAN FALSE */
#define MFALSE                   (0)

/** BIT value */
#define MBIT(x)    (((unsigned int)1) << (x))


#endif
