/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * Harness shims: securec-like helpers + oal_atoi/oal_strtoaddr for the
 * host-side codec unit tests (original text, GPL-2.0).
 * SPDX-License-Identifier: GPL-2.0
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int memcpy_s(void *dest, size_t dmax, const void *src, size_t slen)
{
    if (dest == NULL || src == NULL) { return -1; }
    if (dmax < slen) { return -1; }
    memcpy(dest, src, slen);
    return 0;
}

int memset_s(void *dest, size_t dmax, int ch, size_t count)
{
    if (dest == NULL) { return -1; }
    if (dmax < count) { return -1; }
    memset(dest, ch, count);
    return 0;
}

int sprintf_s(char *str, size_t sizeOfBuffer, const char *format, ...)
{
    va_list ap;
    int ret;
    if (str == NULL || sizeOfBuffer == 0) { return -1; }
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
    if (strDest == NULL || destMax == 0) { return -1; }
    va_start(ap, format);
    ret = vsnprintf(strDest, n, format, ap);
    va_end(ap);
    return (ret < 0) ? -1 : ret;
}

int oal_atoi(const char *s)
{
    return (s == NULL) ? 0 : atoi(s);
}

/* xx:xx:xx:xx:xx:xx -> 6 bytes */
int oal_strtoaddr(const char *mac_str, unsigned int mac_str_len,
                  unsigned char *mac_addr, unsigned int mac_addr_len)
{
    unsigned int i;
    if (mac_str == NULL || mac_addr == NULL) { return -1; }
    if (mac_addr_len < 6) { return -1; }
    for (i = 0; i < 6; i++) {
        mac_addr[i] = (unsigned char)strtoul(mac_str + i * 3, NULL, 16);
    }
    return 0;
}
