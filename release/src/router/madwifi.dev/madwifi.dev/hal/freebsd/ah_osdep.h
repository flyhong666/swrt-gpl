/*-
 * Copyright (c) 2002-2006 Sam Leffler, Errno Consulting, Atheros
 * Communications, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the following conditions are met:
 * 1. The materials contained herein are unmodified and are used
 *    unmodified.
 * 2. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following NO
 *    ''WARRANTY'' disclaimer below (''Disclaimer''), without
 *    modification.
 * 3. Redistributions in binary form must reproduce at minimum a
 *    disclaimer similar to the Disclaimer below and any redistribution
 *    must be conditioned upon including a substantially similar
 *    Disclaimer requirement for further binary redistribution.
 * 4. Neither the names of the above-listed copyright holders nor the
 *    names of any contributors may be used to endorse or promote
 *    product derived from this software without specific prior written
 *    permission.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT,
 * MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 *
 * $Id: //depot/sw/branches/sam_hal/freebsd/ah_osdep.h#3 $
 */
#ifndef _ATH_AH_OSDEP_H_
#define _ATH_AH_OSDEP_H_
/*
 * Atheros Hardware Access Layer (HAL) OS Dependent Definitions.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <machine/bus.h>

/*
 * Delay n microseconds.
 */
extern	void ath_hal_delay(int);
#define	OS_DELAY(_n)	ath_hal_delay(_n)

#define	OS_INLINE	__inline
#define	OS_MEMZERO(_a, _n)	ath_hal_memzero((_a), (_n))
extern void ath_hal_memzero(void *, size_t);
#define	OS_MEMCPY(_d, _s, _n)	ath_hal_memcpy(_d,_s,_n)
extern void *ath_hal_memcpy(void *, const void *, size_t);

#define	abs(_a)		__builtin_abs(_a)

struct ath_hal;
extern	u_int32_t ath_hal_getuptime(struct ath_hal *);
#define	OS_GETUPTIME(_ah)	ath_hal_getuptime(_ah)

/*
 * Register read/write operations are either handled through
 * platform-dependent routines (or when debugging is enabled
 * with AH_DEBUG); or they are inline expanded using the macros
 * defined below.  For public builds we inline expand only for
 * platforms where it is certain what the requirements are to
 * read/write registers--typically they are memory-mapped and
 * no explicit synchronization or memory invalidation operations
 * are required (e.g. i386).
 */
#if defined(AH_DEBUG) || defined(AH_REGOPS_FUNC) || defined(AH_DEBUG_ALQ)
#define	OS_REG_WRITE(_ah, _reg, _val)	ath_hal_reg_write(_ah, _reg, _val)
#define	OS_REG_READ(_ah, _reg)		ath_hal_reg_read(_ah, _reg)

extern	void ath_hal_reg_write(struct ath_hal *ah, u_int reg, u_int32_t val);
extern	u_int32_t ath_hal_reg_read(struct ath_hal *ah, u_int reg);
#else
/*
 * The hardware registers are native little-endian byte order.
 * Big-endian hosts are handled by enabling hardware byte-swap
 * of register reads and writes at reset.  But the PCI clock
 * domain registers are not byte swapped!  Thus, on big-endian
 * platforms we have to explicitly byte-swap those registers.
 * Most of this code is collapsed at compile time because the
 * register values are constants.
 */
#define	AH_LITTLE_ENDIAN	1234
#define	AH_BIG_ENDIAN		4321

#ifndef AH_BYTE_ORDER
/*
 * When the .inc file is not available (e.g. when building
 * in a kernel source tree); look for some other way to
 * setup the host byte order.
 */
#ifdef __LITTLE_ENDIAN
#define	AH_BYTE_ORDER	AH_LITTLE_ENDIAN
#endif
#ifdef __BIG_ENDIAN
#define	AH_BYTE_ORDER	AH_BIG_ENDIAN
#endif
#ifndef AH_BYTE_ORDER
#error "Do not know host byte order"
#endif
#endif /* AH_BYTE_ORDER */

#if AH_BYTE_ORDER == AH_BIG_ENDIAN && !defined(AH_SUPPORT_AR7100)
/*
 * This could be optimized but since we only use it for
 * a few registers there's little reason to do so.
 */
static __inline__ u_int32_t
__bswap32(u_int32_t _x)
{
 	return ((u_int32_t)(
	      (((const u_int8_t *)(&_x))[0]    ) |
	      (((const u_int8_t *)(&_x))[1]<< 8) |
	      (((const u_int8_t *)(&_x))[2]<<16) |
	      (((const u_int8_t *)(&_x))[3]<<24))
	);
}

#define	OS_REG_UNSWAPPED(_reg) \
	(((_reg) >= 0x4000 && (_reg) < 0x5000) || \
	 ((_reg) >= 0x7000 && (_reg) < 0x8000))
#define SWAPREG(_reg, _val) \
	(OS_REG_UNSWAPPED(_reg) ? __bswap32(_val) : (_val))
#else
#define	OS_REG_UNSWAPPED(_reg)	(0)
#define SWAPREG(_reg, _val) (_val)
#define __bswap32(_x)	(_x)
#endif

#if _BYTE_ORDER == _BIG_ENDIAN
#define OS_REG_WRITE(_ah, _reg, _val) do {				\
	bus_space_write_stream_4((bus_space_tag_t)(_ah)->ah_st,	\
	    (bus_space_handle_t)(_ah)->ah_sh, (_reg), SWAPREG(_reg, (_val)));	\
} while (0)
#define OS_REG_READ(_ah, _reg)						\
	SWAPREG(_reg, bus_space_read_stream_4((bus_space_tag_t)(_ah)->ah_st,	\
	    (bus_space_handle_t)(_ah)->ah_sh, (_reg)))
#else /* _BYTE_ORDER == _LITTLE_ENDIAN */
#define	OS_REG_WRITE(_ah, _reg, _val)					\
	bus_space_write_4((bus_space_tag_t)(_ah)->ah_st,		\
	    (bus_space_handle_t)(_ah)->ah_sh, (_reg), (_val))
#define	OS_REG_READ(_ah, _reg)						\
	bus_space_read_4((bus_space_tag_t)(_ah)->ah_st,			\
	    (bus_space_handle_t)(_ah)->ah_sh, (_reg))
#endif /* _BYTE_ORDER */
#endif /* AH_DEBUG || AH_REGFUNC || AH_DEBUG_ALQ */

#ifdef AH_DEBUG_ALQ
extern	void OS_MARK(struct ath_hal *, u_int id, u_int32_t value);
#else
#define	OS_MARK(_ah, _id, _v)
#endif

#endif /* _ATH_AH_OSDEP_H_ */
