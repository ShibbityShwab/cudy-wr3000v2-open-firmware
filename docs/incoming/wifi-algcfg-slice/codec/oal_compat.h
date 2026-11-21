/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * Port-compat shim for the alg_cfg codec (original text, GPL-2.0).
 * Provides the small oal/securec surface the codec core uses so the same
 * translation unit builds host-side (harness) and in-kernel (6.18 Kbuild).
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef OAL_COMPAT_H
#define OAL_COMPAT_H

#ifdef OAL_COMPAT_KERNEL
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

/* ---- basic types ---- */
typedef uint8_t   oal_bool_enum_uint8;
typedef uint16_t  mac_alg_cfg_enum_uint16;
typedef uint8_t   alg_cfg_type_enum_uint8;
typedef uint8_t   alg_cfg_pkt_type_enum_uint8;
typedef uint8_t   alg_cfg_param_tbl_size_enum_uint8;

#define OAL_TRUE   1
#define OAL_FALSE  0
#define OAL_SUCC   0
#define OAL_FAIL   0x8b2dU   /* 35629: verified in the hi5622 binary (movw r0,#35629) */
#define OAL_ERR_CODE_PTR_NULL                  100    /* 0x64  verified */
#define OAL_ERR_CODE_ARRAY_OVERFLOW            101    /* 0x65  verified */
#define OAL_ERR_CODE_CONFIG_ARGS_OVER          1009   /* 0x3f1 verified */
#define OAL_ERR_CODE_ALG_PROCESS_INFO_NOT_FOUND 1014  /* 0x3f6 verified */
#define OAL_ERR_CODE_SUB_ALG_ANALYSIS_ENTRY_NOT_FOUND 1017 /* 0x3f9 verified */

#define OAL_STATIC static
#define oal_likely(x)   (x)
#define oal_unlikely(x) (x)
#define oal_array_size(a) (sizeof(a) / sizeof((a)[0]))
#define OAL_STRLEN(s)   strlen(s)

/* ---- logging: harness no-ops; kernel build maps to pr_* ---- */
#ifdef OAL_COMPAT_KERNEL
#include <linux/printk.h>
#define OAM_SF_ANY 0
#define OAM_SF_CFG 0
#define OAM_SF_WATERFILLING 0
#define oam_error_log0(...)   pr_err(__VA_ARGS__)
#define oam_error_log1(...)   pr_err(__VA_ARGS__)
#define oam_error_log2(...)   pr_err(__VA_ARGS__)
#define oam_warning_log0(...) pr_warn(__VA_ARGS__)
#define oam_warning_log1(...) pr_warn(__VA_ARGS__)
#else
#include <stdio.h>
#define oam_error_log0(...)   do { } while (0)
#define oam_error_log1(...)   do { } while (0)
#define oam_error_log2(...)   do { } while (0)
#define oam_warning_log0(...) do { } while (0)
#define oam_warning_log1(...) do { } while (0)
#define OAM_SF_ANY 0
#define OAM_SF_CFG 0
#define OAM_SF_WATERFILLING 0
#endif

/* ---- securec surface (kernel build uses the local kernel_util.c) ---- */
#ifdef OAL_COMPAT_KERNEL
extern int memcpy_s(void *dest, size_t dmax, const void *src, size_t slen);
extern int memset_s(void *dest, size_t dmax, int ch, size_t count);
extern int sprintf_s(char *str, size_t sizeOfBuffer, const char *format, ...);
extern int snprintf_s(char *strDest, size_t destMax, size_t count, const char *format, ...);
#define EOK 0
#else
extern int memcpy_s(void *dest, size_t dmax, const void *src, size_t slen);
extern int memset_s(void *dest, size_t dmax, int ch, size_t count);
extern int sprintf_s(char *str, size_t sizeOfBuffer, const char *format, ...);
extern int snprintf_s(char *strDest, size_t destMax, size_t count, const char *format, ...);
#define EOK 0
#endif

/* ---- small helpers (harness provides; kernel links oal) ---- */
extern int oal_atoi(const char *s);
extern int oal_strtoaddr(const char *mac_str, unsigned int mac_str_len,
                         unsigned char *mac_addr, unsigned int mac_addr_len);

#endif /* OAL_COMPAT_H */
