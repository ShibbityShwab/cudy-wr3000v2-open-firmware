/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * Kernel-side shims for the alg_cfg codec module (original text, GPL-2.0):
 * securec-style helpers + oal_atoi/oal_strtoaddr. Self-contained so the
 * codec core links without the full oal/libsec layers.
 * SPDX-License-Identifier: GPL-2.0
 */
#define OAL_COMPAT_KERNEL 1
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include "oal_compat.h"

int memcpy_s(void *dest, size_t dmax, const void *src, size_t slen)
{
    if (!dest || !src) {
        return -1;
    }
    if (dmax < slen) {
        return -1;
    }
    memcpy(dest, src, slen);
    return 0;
}

int memset_s(void *dest, size_t dmax, int ch, size_t count)
{
    if (!dest) {
        return -1;
    }
    if (dmax < count) {
        return -1;
    }
    memset(dest, ch, count);
    return 0;
}

int sprintf_s(char *str, size_t sizeOfBuffer, const char *format, ...)
{
    va_list ap;
    int ret;
    if (!str || sizeOfBuffer == 0) {
        return -1;
    }
    va_start(ap, format);
    ret = vsnprintf(str, sizeOfBuffer, format, ap);
    va_end(ap);
    return (ret < 0 || (size_t)ret >= sizeOfBuffer) ? -1 : ret;
}

int snprintf_s(char *strDest, size_t destMax, size_t count, const char *format, ...)
{
    va_list ap;
    int ret;
    size_t n = (count + 1) < destMax ? (count + 1) : destMax;
    if (!strDest || destMax == 0) {
        return -1;
    }
    va_start(ap, format);
    ret = vsnprintf(strDest, n, format, ap);
    va_end(ap);
    return ret < 0 ? -1 : ret;
}

int oal_atoi(const char *s)
{
    int val = 0;
    int sign = 1;
    if (!s) {
        return 0;
    }
    while (*s == ' ') {
        s++;
    }
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return sign * val;
}

int oal_strtoaddr(const char *mac_str, unsigned int mac_str_len,
                  unsigned char *mac_addr, unsigned int mac_addr_len)
{
    unsigned int i;
    if (!mac_str || !mac_addr || mac_addr_len < 6) {
        return -1;
    }
    for (i = 0; i < 6; i++) {
        mac_addr[i] = (unsigned char)simple_strtoul(mac_str + i * 3, NULL, 16);
    }
    return 0;
}
