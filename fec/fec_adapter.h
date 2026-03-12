/*
 * fec_adapter.h
 *
 *  Created on: 2026-03-12
 */

#ifndef UDP2RAW_FEC_ADAPTER_H_
#define UDP2RAW_FEC_ADAPTER_H_

#include "common.h"
#include "fec/fec_manager.h"
#include "fec/delay_manager.h"

struct fec_inner_stat_t {
    u64_t input_packet_num = 0;
    u64_t input_packet_size = 0;
    u64_t output_packet_num = 0;
    u64_t output_packet_size = 0;
};

struct fec_stat_t {
    u64_t last_report_time = 0;
    fec_inner_stat_t normal_to_fec;
    fec_inner_stat_t fec_to_normal;
};

struct fec_config_t {
    int enable = 0;
    int allow_fallback = 0;

    int disable_fec = 0;
    int disable_checksum = 0;

    int jitter_min = 0;   // us
    int jitter_max = 0;   // us
    int interval_min = 0; // us
    int interval_max = 0; // us

    int fix_latency = 0;
    int delay_capacity = 0;
    int report_interval = 0; // seconds
};

struct fec_context_t {
    fec_encode_manager_t fec_encode;
    fec_decode_manager_t fec_decode;
    delay_manager_t delay_manager;

    fec_send_cb send_cb = 0;
    void *send_ctx = 0;

    fec_stat_t stat;
    fec_config_t config;
};

extern fec_config_t g_fec_config;
extern char fec_rs_par_str[rs_str_len];

void fec_init_context(fec_context_t &ctx, struct ev_loop *loop, fec_send_cb cb, void *send_ctx);
void fec_destroy_context(fec_context_t &ctx);
void fec_delay_manager_check(fec_context_t &ctx);

int fec_encode_input(fec_context_t &ctx, char *data, int len, int &out_n, char **&out_arr, int *&out_len, my_time_t *&out_delay);
int fec_decode_input(fec_context_t &ctx, char *data, int len, int &out_n, char **&out_arr, int *&out_len, my_time_t *&out_delay);
int fec_send_outputs(fec_context_t &ctx, int out_n, char **out_arr, int *out_len, my_time_t *out_delay);

#endif /* UDP2RAW_FEC_ADAPTER_H_ */
