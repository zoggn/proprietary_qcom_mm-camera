/*============================================================================

  Copyright (c) 2013 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/
#ifndef __PPROC_MODULE_H__
#define __PPROC_MODULE_H__

#include "mct_module.h"

/* macros for unpacking identity */
#define PPROC_GET_STREAM_ID(identity) ((identity) & 0xFFFF)
#define PPROC_GET_SESSION_ID(identity) (((identity) & 0xFFFF0000) >> 16)

// Debug mask
#define PPROC_DEBUG_MASK_PPROC                 (1<<0)

#ifdef CDBG
#undef CDBG
#endif
#define CDBG(fmt, args...) ALOGD_IF(gCamPprocLogLevel >= 2, fmt, ##args)

#ifdef CDBG_LOW
#undef CDBG_LOW
#endif //#ifdef CDBG_LOW
#define CDBG_LOW(fmt, args...) ALOGD_IF(gCamPprocLogLevel >= 3, fmt, ##args)

#ifdef CDBG_HIGH
#undef CDBG_HIGH
#endif //#ifdef CDBG_HIGH
#define CDBG_HIGH(fmt, args...) ALOGD_IF(gCamPprocLogLevel >= 1, fmt, ##args)

extern volatile uint32_t gCamPprocLogLevel;
mct_module_t* pproc_module_get_sub_mod(mct_module_t *module, const char *name);
static void get_pproc_loglevel();
#endif
