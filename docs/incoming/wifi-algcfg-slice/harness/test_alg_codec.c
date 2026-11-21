/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * alg_cfg codec - 17-twin oracle unit harness (original text, GPL-2.0).
 *
 * Builds twice from the same source (harness/Makefile):
 *   oracle_test  - links the local hi1105 reference wal_config_alg_cfg.c
 *                  (the ORACLE; reference tree stays out of the repo)
 *   port_test    - links codec/wal_config_alg_cfg.c (the committed port)
 * Run both, then:  diff <(./oracle_test) <(./port_test)   -> empty = pass.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <stdio.h>
#include <string.h>
#include "alg_cfg.h"
#include "wal_config_alg_cfg.h"

static int g_fails;

#define CHECK(cond, name) do { \
    if (!(cond)) { printf("FAIL %-38s %s\n", name, #cond); g_fails++; } \
} while (0)

static void dump_bytes(const char *tag, const uint8_t *p, uint16_t n)
{
    unsigned int i;
    printf("%s[%u]=", tag, n);
    for (i = 0; i < n && i < 64; i++) {
        printf("%02x", p[i]);
    }
    printf("\n");
}

static void dump_str(const char *tag, const uint8_t *p)
{
    if (p == NULL) { printf("%s=<null>\n", tag); return; }
    printf("%s=\"%s\"\n", tag, (const char *)p);
}

static void t1_get_cmd_one_arg(void)
{
    char arg[64]; uint32_t off = 0; uint32_t ret;

    memset(arg, 0x55, sizeof(arg));
    ret = alg_cfg_get_cmd_one_arg("txmode_mode_sw 1", arg, sizeof(arg), &off);
    CHECK(ret == OAL_SUCC, "T01 get_cmd_one_arg basic ret");
    CHECK(strcmp(arg, "txmode_mode_sw") == 0, "T01 basic arg");
    CHECK(off == 14, "T01 basic offset"); /* "txmode_mode_sw" is 14 chars */

    memset(arg, 0x55, sizeof(arg));
    ret = alg_cfg_get_cmd_one_arg("   abc  def", arg, sizeof(arg), &off);
    CHECK(ret == OAL_SUCC && strcmp(arg, "abc") == 0 && off == 6, "T01 leading spaces");

    memset(arg, 0x55, sizeof(arg));
    ret = alg_cfg_get_cmd_one_arg("abc", arg, sizeof(arg), &off);
    CHECK(ret == OAL_SUCC && strcmp(arg, "abc") == 0 && off == 3, "T01 no trailing");

    memset(arg, 0x55, sizeof(arg));
    ret = alg_cfg_get_cmd_one_arg("   ", arg, sizeof(arg), &off);
    CHECK(ret == OAL_ERR_CODE_CONFIG_ARGS_OVER && off == 0, "T01 empty");

    ret = alg_cfg_get_cmd_one_arg(NULL, arg, sizeof(arg), &off);
    CHECK(ret == OAL_ERR_CODE_PTR_NULL, "T01 null cmd");

    memset(arg, 0x55, sizeof(arg));
    ret = alg_cfg_get_cmd_one_arg("123456789012345678901234567890123456789012345678901234567890", arg, sizeof(arg), &off);
    CHECK(ret == OAL_ERR_CODE_ARRAY_OVERFLOW, "T01 overflow");
}

static void t2_search_by_name(void)
{
    alg_cfg_process_info_stru *e;
    e = alg_cfg_search_process_info_by_cfg_name("txmode_mode_sw");
    CHECK(e != NULL && e->en_alg_cfg_id == MAC_ALG_CFG_TXMODE_MODE_SW && e->en_cfg_type == ALG_CFG_TYPE_SET, "T02 set entry");

    e = alg_cfg_search_process_info_by_cfg_name(" get_txmode_mode_sw x");
    CHECK(e != NULL && e->en_cfg_type == ALG_CFG_TYPE_GET, "T02 get entry w/ leading space");

    e = alg_cfg_search_process_info_by_cfg_name("bogus_cmd");
    CHECK(e == NULL, "T02 miss");
    e = alg_cfg_search_process_info_by_cfg_name(NULL);
    CHECK(e == NULL, "T02 null");
}

static void t3_search_by_name_normal(void)
{
    alg_cfg_process_info_stru *e = (alg_cfg_process_info_stru *)0x1;
    alg_cfg_search_process_info_by_cfg_name_normal("txmode_mode_sw", sizeof("txmode_mode_sw"), &e);
    CHECK(e != NULL && e->en_cfg_type == ALG_CFG_TYPE_SET, "T03 hit");
    alg_cfg_search_process_info_by_cfg_name_normal("nope", sizeof("nope"), &e);
    CHECK(e == NULL, "T03 miss");
}

static void t4_search_by_id(void)
{
    alg_cfg_process_info_stru *e;
    e = alg_cfg_search_process_info_by_cfg_id(MAC_ALG_CFG_TXMODE_USER_FIX_MODE, ALG_CFG_TYPE_SET);
    CHECK(e != NULL && e->st_process_info.en_need_device_process == OAL_TRUE, "T04 set hit");
    e = alg_cfg_search_process_info_by_cfg_id(MAC_ALG_CFG_TXMODE_USER_FIX_MODE, ALG_CFG_TYPE_GET);
    CHECK(e != NULL && e->en_cfg_type == ALG_CFG_TYPE_GET, "T04 get hit");
    e = alg_cfg_search_process_info_by_cfg_id(0x7fff, ALG_CFG_TYPE_SET);
    CHECK(e == NULL, "T04 miss");
}

static void t5_search_by_id_normal(void)
{
    alg_cfg_process_info_stru *e = (alg_cfg_process_info_stru *)0x1;
    alg_cfg_search_process_info_by_cfg_id_normal(MAC_ALG_CFG_WATERFILLING_ENABLE, ALG_CFG_TYPE_SET, &e);
    CHECK(e != NULL, "T05 hit");
    alg_cfg_search_process_info_by_cfg_id_normal(MAC_ALG_CFG_WATERFILLING_ENABLE, ALG_CFG_TYPE_GET, &e);
    CHECK(e != NULL && e->en_cfg_type == ALG_CFG_TYPE_GET, "T05 get hit");
}

static void t6_get_pkt_type(void)
{
    static const char *names[] = { "ucast_data", "mcast_data", "bcast_data", "mumimo_data",
        "ofdma_data", "ucast_mgmt", "mcast_mgmt", "bcast_mgmt", "prot_ctrl", "ndp",
        "ndpa", "resp_ctrl", "other_ctrl" };
    unsigned int i;
    for (i = 0; i < 13; i++) {
        if (alg_cfg_get_pkt_type(names[i]) != (alg_cfg_pkt_type_enum_uint8)i) {
            printf("FAIL T06 pkt %s\n", names[i]); g_fails++;
        }
    }
    CHECK(alg_cfg_get_pkt_type("garbage") == ALG_CFG_PKT_TYPE_IRRELEVANT, "T06 miss");
    CHECK(alg_cfg_get_pkt_type(NULL) == ALG_CFG_PKT_TYPE_IRRELEVANT, "T06 null");
}

static void t7_analysis_args_head(void)
{
    char *args[WLAN_CFG_MAX_ARGS_NUM] = { 0 };
    alg_cfg_process_info_stru *info = NULL;
    uint8_t off = 0;
    alg_cfg_pkt_type_enum_uint8 pkt = 0;
    uint32_t ret;

    args[0] = "txmode_mode_sw"; args[1] = "1";
    ret = alg_cfg_analysis_args_head(args, &info, &off, &pkt);
    CHECK(ret == OAL_SUCC && off == 1 && pkt == ALG_CFG_PKT_TYPE_IRRELEVANT &&
          info->en_cfg_type == ALG_CFG_TYPE_SET, "T07 plain");

    args[0] = "ucast_data"; args[1] = "txmode_mode_sw"; args[2] = "1";
    ret = alg_cfg_analysis_args_head(args, &info, &off, &pkt);
    CHECK(ret == OAL_SUCC && off == 2 && pkt == ALG_CFG_PKT_TYPE_UCAST_DATA, "T07 pkt prefix");

    args[0] = "bogus";
    ret = alg_cfg_analysis_args_head(args, &info, &off, &pkt);
    CHECK(ret == OAL_ERR_CODE_ALG_PROCESS_INFO_NOT_FOUND, "T07 not found");

    ret = alg_cfg_analysis_args_head(NULL, &info, &off, &pkt);
    CHECK(ret == OAL_ERR_CODE_PTR_NULL, "T07 null args");
}

static void t8_analysis_args(void)
{
    char *args[WLAN_CFG_MAX_ARGS_NUM] = { 0 };
    alg_cfg_param_stru param;
    alg_cfg_hdr_stru hdr;
    wlan_cfg_process_info_stru pi;
    uint32_t ret;

    memset(&param, 0, sizeof(param)); memset(&hdr, 0xee, sizeof(hdr)); memset(&pi, 0, sizeof(pi));
    args[0] = "txmode_user_fix_mode"; args[1] = "00:11:22:33:44:55"; args[2] = "stbc_fix";
    ret = alg_cfg_analysis_args(args, 3, &param, &hdr, &pi);
    CHECK(ret == OAL_SUCC, "T08 ret");
    CHECK(hdr.en_cfg_id == MAC_ALG_CFG_TXMODE_USER_FIX_MODE, "T08 id");
    CHECK(hdr.en_cfg_type == ALG_CFG_TYPE_SET, "T08 type");
    CHECK(hdr.en_pkt_type == ALG_CFG_PKT_TYPE_IRRELEVANT, "T08 pkt");
    CHECK(param.uc_param_num == 2, "T08 param num");
    CHECK(param.apuc_param_args[0] != NULL &&
          strcmp((const char *)param.apuc_param_args[0], "00:11:22:33:44:55") == 0, "T08 arg0");
    CHECK(strcmp((const char *)param.apuc_param_args[1], "stbc_fix") == 0, "T08 arg1");
    CHECK(pi.en_need_device_process == OAL_TRUE && pi.en_need_w4_device_return == OAL_TRUE, "T08 flags");
}

static void t9_get_match_value(void)
{
    uint32_t v = 0;
    uint32_t ret;
    ret = alg_cfg_get_match_value(g_alg_cfg_tx_mode_tbl, (uint16_t)oal_array_size(g_alg_cfg_tx_mode_tbl),
                                  (const uint8_t *)"auto", &v);
    CHECK(ret == OAL_SUCC && v == ALG_CFG_TX_MODE_AUTO, "T09 auto");
    ret = alg_cfg_get_match_value(g_alg_cfg_tx_mode_tbl, (uint16_t)oal_array_size(g_alg_cfg_tx_mode_tbl),
                                  (const uint8_t *)"stbc_fix", &v);
    CHECK(ret == OAL_SUCC && v == ALG_CFG_TX_MODE_STBC2_FIX, "T09 stbc");
    ret = alg_cfg_get_match_value(g_alg_cfg_tx_mode_tbl, (uint16_t)oal_array_size(g_alg_cfg_tx_mode_tbl),
                                  (const uint8_t *)"bogus", &v);
    CHECK(ret == OAL_FAIL, "T09 miss");
    ret = alg_cfg_get_match_value(NULL, 1, (const uint8_t *)"auto", &v);
    CHECK(ret == OAL_FAIL, "T09 null tbl");
}

static void t10_param_get_tbl_size(void)
{
    CHECK(alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_PKT_TYPE) == 13, "T10 pkt");
    CHECK(alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_BOOL) == 2, "T10 bool");
    CHECK(alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_TX_MODE) == 13, "T10 txmode");
    CHECK(alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_FEC_CODING) == 0, "T10 fec (not in twin subset)");
    CHECK(alg_cfg_param_get_tbl_size(ALG_CFG_PARAM_HI5622_MAX) == 0, "T10 range max");
    CHECK(alg_cfg_param_get_tbl_size((alg_cfg_param_tbl_size_enum_uint8)255) == 0, "T10 out of range");
}

static void run_parser_case(const char *name, char **args, uint8_t args_num,
    uint8_t *mem_block, uint16_t *len, uint8_t *out_buf, alg_cfg_hdr_stru *hdr,
    alg_cfg_sub_alg_args_analysis_func fn)
{
    alg_cfg_param_stru param;
    uint32_t ret;
    uint8_t i;

    memset(&param, 0, sizeof(param));
    param.puc_output_str = out_buf;
    param.uc_param_num = args_num;
    for (i = 0; i < args_num; i++) {
        param.apuc_param_args[i] = (uint8_t *)args[i];
    }
    ret = fn(&param, mem_block, len);
    printf("%s: ret=%u len=%u\n", name, ret, *len);
    dump_bytes(name, mem_block, *len);
    dump_str(name, out_buf);
    if (ret != OAL_SUCC) { g_fails++; printf("FAIL %s ret\n", name); }
}

static void run_parser_case_expect_fail(const char *name, char **args, uint8_t args_num,
    uint8_t *mem_block, uint16_t *len, uint8_t *out_buf, alg_cfg_hdr_stru *hdr,
    alg_cfg_sub_alg_args_analysis_func fn)
{
    alg_cfg_param_stru param;
    uint32_t ret;
    uint8_t i;

    memset(&param, 0, sizeof(param));
    param.puc_output_str = out_buf;
    param.uc_param_num = args_num;
    for (i = 0; i < args_num; i++) {
        param.apuc_param_args[i] = (uint8_t *)args[i];
    }
    ret = fn(&param, mem_block, len);
    printf("%s: ret=%u\n", name, ret);
    dump_str(name, out_buf);
    if (ret == OAL_SUCC) { g_fails++; printf("FAIL %s expected failure\n", name); }
}

static void setup_hdr(alg_cfg_hdr_stru *hdr, mac_alg_cfg_enum_uint16 id, alg_cfg_type_enum_uint8 type)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->en_cfg_id = id;
    hdr->en_cfg_type = type;
    hdr->en_pkt_type = ALG_CFG_PKT_TYPE_IRRELEVANT;
    hdr->dev_ret = OAL_SUCC;
    hdr->en_need_w4_dev_return = OAL_TRUE;
}

static void t11_txmode_user_fix_mode(void)
{
    char *args[4]; uint8_t mem[128]; uint16_t len = 0; uint8_t out[512];
    alg_cfg_hdr_stru hdr;

    setup_hdr(&hdr, MAC_ALG_CFG_TXMODE_USER_FIX_MODE, ALG_CFG_TYPE_SET);
    memset(out, 0, sizeof(out));
    memcpy(mem, &hdr, sizeof(hdr));
    args[0] = "00:11:22:33:44:55"; args[1] = "stbc_fix";
    run_parser_case("T11 set-valid", args, 2, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_txmode_user_fix_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "00:11:22:33:44:55"; args[1] = "bogus_mode";
    run_parser_case_expect_fail("T11 set-badmode", args, 2, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_txmode_user_fix_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "00:11:22"; args[1] = "auto";
    run_parser_case_expect_fail("T11 set-badmac", args, 2, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_txmode_user_fix_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "00:11:22:33:44:55";
    run_parser_case_expect_fail("T11 set-few-args", args, 1, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_txmode_user_fix_mode);

    setup_hdr(&hdr, MAC_ALG_CFG_TXMODE_USER_FIX_MODE, ALG_CFG_TYPE_GET);
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "00:11:22:33:44:55";
    run_parser_case("T11 get-valid", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_txmode_user_fix_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case_expect_fail("T11 get-badcount", args, 0, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_txmode_user_fix_mode);
}

static void t12_txmode_all_user_fix_mode(void)
{
    char *args[2]; uint8_t mem[128]; uint16_t len = 0; uint8_t out[512];
    alg_cfg_hdr_stru hdr;

    setup_hdr(&hdr, MAC_ALG_CFG_TXMODE_ALL_USER_FIX_MODE, ALG_CFG_TYPE_SET);
    memset(out, 0, sizeof(out));
    memcpy(mem, &hdr, sizeof(hdr));
    args[0] = "chain2_fix";
    run_parser_case("T12 set-valid", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_txmode_all_user_fix_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "nope";
    run_parser_case_expect_fail("T12 set-badmode", args, 1, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_txmode_all_user_fix_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case_expect_fail("T12 set-few-args", args, 0, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_txmode_all_user_fix_mode);

    setup_hdr(&hdr, MAC_ALG_CFG_TXMODE_ALL_USER_FIX_MODE, ALG_CFG_TYPE_GET);
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case("T12 get-valid", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_txmode_all_user_fix_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case_expect_fail("T12 get-badcount", args, 0, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_txmode_all_user_fix_mode);
}

static void t13_waterfilling_mode(void)
{
    char *args[1]; uint8_t mem[128]; uint16_t len = 0; uint8_t out[512];
    alg_cfg_hdr_stru hdr;

    setup_hdr(&hdr, MAC_ALG_CFG_WATERFILLING_MODE, ALG_CFG_TYPE_SET);
    memset(out, 0, sizeof(out));
    memcpy(mem, &hdr, sizeof(hdr));
    args[0] = "1";
    run_parser_case("T13 set-1", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_waterfilling_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "7";
    run_parser_case_expect_fail("T13 set-7", args, 1, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_waterfilling_mode);

    setup_hdr(&hdr, MAC_ALG_CFG_WATERFILLING_MODE, ALG_CFG_TYPE_GET);
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case("T13 get-valid", args, 0, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_waterfilling_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "1";
    run_parser_case_expect_fail("T13 get-extra-arg", args, 1, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_waterfilling_mode);
}

static void t14_waterfilling_enable(void)
{
    char *args[1]; uint8_t mem[128]; uint16_t len = 0; uint8_t out[512];
    alg_cfg_hdr_stru hdr;

    setup_hdr(&hdr, MAC_ALG_CFG_WATERFILLING_ENABLE, ALG_CFG_TYPE_SET);
    memset(out, 0, sizeof(out));
    memcpy(mem, &hdr, sizeof(hdr));
    args[0] = "1";
    run_parser_case("T14 set-1", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_waterfilling_enable);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "2";
    run_parser_case_expect_fail("T14 set-2", args, 1, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_waterfilling_enable);

    setup_hdr(&hdr, MAC_ALG_CFG_WATERFILLING_ENABLE, ALG_CFG_TYPE_GET);
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case("T14 get-valid", args, 0, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_waterfilling_enable);
}

static void t15_hardamard_mode(void)
{
    char *args[1]; uint8_t mem[128]; uint16_t len = 0; uint8_t out[512];
    alg_cfg_hdr_stru hdr;

    setup_hdr(&hdr, MAC_ALG_CFG_HARDAMARDTX_MODE, ALG_CFG_TYPE_SET);
    memset(out, 0, sizeof(out));
    memcpy(mem, &hdr, sizeof(hdr));
    args[0] = "1";
    run_parser_case("T15 set-1", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_hardamard_mode);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "3";
    run_parser_case_expect_fail("T15 set-3", args, 1, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_hardamard_mode);

    setup_hdr(&hdr, MAC_ALG_CFG_HARDAMARDTX_MODE, ALG_CFG_TYPE_GET);
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case("T15 get-valid", args, 0, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_hardamard_mode);
}

static void t16_hardamard_enable(void)
{
    char *args[1]; uint8_t mem[128]; uint16_t len = 0; uint8_t out[512];
    alg_cfg_hdr_stru hdr;

    setup_hdr(&hdr, MAC_ALG_CFG_HARDAMARDTX_ENABLE, ALG_CFG_TYPE_SET);
    memset(out, 0, sizeof(out));
    memcpy(mem, &hdr, sizeof(hdr));
    args[0] = "1";
    run_parser_case("T16 set-1", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_hardamard_enable);

    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "2";
    run_parser_case_expect_fail("T16 set-2", args, 1, mem, &len, out, &hdr,
                                alg_cfg_args_analysis_hardamard_enable);

    setup_hdr(&hdr, MAC_ALG_CFG_HARDAMARDTX_ENABLE, ALG_CFG_TYPE_GET);
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case("T16 get-valid", args, 0, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_hardamard_enable);
}

static void t17_waterfilling_limit(void)
{
    char *args[1]; uint8_t mem[128]; uint16_t len = 0; uint8_t out[512];
    alg_cfg_hdr_stru hdr;

    setup_hdr(&hdr, MAC_ALG_CFG_WATERFILLING_LIMIT_ASNR, ALG_CFG_TYPE_SET);
    memset(out, 0, sizeof(out));
    memcpy(mem, &hdr, sizeof(hdr));
    args[0] = "127";
    run_parser_case("T17 set-127", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_waterfilling_limit);

    /* int8 wrap quirk (identical in oracle and port): 128 -> (int8_t)-128,
     * which passes the >127 check - the reference check is dead code for
     * positive decimal inputs; the port reproduces it faithfully. */
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    args[0] = "128";
    run_parser_case("T17 set-128-wrap", args, 1, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_waterfilling_limit);

    setup_hdr(&hdr, MAC_ALG_CFG_WATERFILLING_LIMIT_ASNR, ALG_CFG_TYPE_GET);
    memcpy(mem, &hdr, sizeof(hdr)); len = 0; memset(out, 0, sizeof(out));
    run_parser_case("T17 get-valid", args, 0, mem, &len, out, &hdr,
                    alg_cfg_args_analysis_waterfilling_limit);
}

int main(void)
{
    printf("== alg_cfg codec 17-twin harness ==\n");
    t1_get_cmd_one_arg();
    t2_search_by_name();
    t3_search_by_name_normal();
    t4_search_by_id();
    t5_search_by_id_normal();
    t6_get_pkt_type();
    t7_analysis_args_head();
    t8_analysis_args();
    t9_get_match_value();
    t10_param_get_tbl_size();
    t11_txmode_user_fix_mode();
    t12_txmode_all_user_fix_mode();
    t13_waterfilling_mode();
    t14_waterfilling_enable();
    t15_hardamard_mode();
    t16_hardamard_enable();
    t17_waterfilling_limit();
    printf("== %s: %d failures ==\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
