/*
 * fec_adapter.cpp
 *
 *  Created on: 2026-03-12
 */

#include "fec/fec_adapter.h"
#include "log.h"

fec_config_t g_fec_config;
char fec_rs_par_str[rs_str_len] = "20:10";

static int random_between_int(int a, int b) {
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    if (a == b) return a;
    return a + (int)(get_true_random_number() % (u32_t)(b - a + 1));
}

static void fec_delay_timer_cb(struct ev_loop *loop, struct ev_timer *watcher, int revents) {
    (void)loop;
    (void)revents;
    if (!watcher || !watcher->data) return;
    fec_context_t *ctx = (fec_context_t *)watcher->data;
    ctx->delay_manager.check();
}

static void fec_encode_timer_cb(struct ev_loop *loop, struct ev_timer *watcher, int revents) {
    (void)loop;
    (void)revents;
    if (!watcher || !watcher->data) return;
    fec_context_t *ctx = (fec_context_t *)watcher->data;

    int out_n = 0;
    char **out_arr = 0;
    int *out_len = 0;
    my_time_t *out_delay = 0;

    fec_encode_input(*ctx, 0, 0, out_n, out_arr, out_len, out_delay);
    fec_send_outputs(*ctx, out_n, out_arr, out_len, out_delay);
}

void fec_init_context(fec_context_t &ctx, struct ev_loop *loop, fec_send_cb cb, void *send_ctx) {
    ctx.config = g_fec_config;
    ctx.send_cb = cb;
    ctx.send_ctx = send_ctx;

    ctx.delay_manager.set_capacity(ctx.config.delay_capacity);
    ctx.delay_manager.set_loop_and_cb(loop, fec_delay_timer_cb);
    ctx.delay_manager.get_timer().data = &ctx;

    ctx.fec_encode.set_loop_and_cb(loop, fec_encode_timer_cb);
    ctx.fec_encode.set_data(&ctx);
}

void fec_destroy_context(fec_context_t &ctx) {
    ctx.fec_encode.clear_all();
    ctx.send_cb = 0;
    ctx.send_ctx = 0;
}

void fec_delay_manager_check(fec_context_t &ctx) {
    ctx.delay_manager.check();
}

int fec_encode_input(fec_context_t &ctx, char *data, int len, int &out_n, char **&out_arr, int *&out_len, my_time_t *&out_delay) {
    static my_time_t out_delay_buf[max_fec_packet_num + 100] = {0};
    out_delay = out_delay_buf;

    fec_inner_stat_t &inner_stat = ctx.stat.normal_to_fec;

    if (ctx.config.disable_fec || ctx.config.enable == 0) {
        if (data == 0) {
            out_n = 0;
            return 0;
        }
        inner_stat.input_packet_num++;
        inner_stat.input_packet_size += len;
        inner_stat.output_packet_num++;
        inner_stat.output_packet_size += len;

        out_n = 1;
        static char *data_static;
        data_static = data;
        static int len_static;
        len_static = len;
        out_arr = &data_static;
        out_len = &len_static;
        out_delay[0] = 0;
        return 0;
    }

    if (data != 0) {
        inner_stat.input_packet_num++;
        inner_stat.input_packet_size += len;
    }

    ctx.fec_encode.input(data, len);
    ctx.fec_encode.output(out_n, out_arr, out_len);

    if (out_n > 0) {
        my_time_t common_latency = 0;
        my_time_t first_packet_time = ctx.fec_encode.get_first_packet_time();

        if (ctx.config.fix_latency == 1 && ctx.fec_encode.get_type() == 0) {
            my_time_t current_time = get_current_time_us();
            my_time_t tmp;
            if (first_packet_time != 0 && (my_time_t)ctx.fec_encode.get_pending_time() >= (current_time - first_packet_time)) {
                tmp = (my_time_t)ctx.fec_encode.get_pending_time() - (current_time - first_packet_time);
            } else {
                tmp = 0;
            }
            common_latency += tmp;
        }

        common_latency += random_between_int(ctx.config.jitter_min, ctx.config.jitter_max);
        out_delay_buf[0] = common_latency;

        for (int i = 1; i < out_n; i++) {
            if (out_n == 1) {
                out_delay_buf[i] = out_delay_buf[i - 1];
            } else {
                out_delay_buf[i] = out_delay_buf[i - 1] + (my_time_t)(random_between_int(ctx.config.interval_min, ctx.config.interval_max) / (out_n - 1));
            }
        }
    }

    if (out_n > 0) {
        log_bare(log_trace, "seq= %u ", read_u32(out_arr[0]));
    }
    for (int i = 0; i < out_n; i++) {
        inner_stat.output_packet_num++;
        inner_stat.output_packet_size += out_len[i];
        log_bare(log_trace, "%d ", out_len[i]);
    }
    if (out_n > 0) log_bare(log_trace, "\n");

    mylog(log_trace, "from_normal_to_fec input_len=%d,output_n=%d\n", len, out_n);
    return 0;
}

int fec_decode_input(fec_context_t &ctx, char *data, int len, int &out_n, char **&out_arr, int *&out_len, my_time_t *&out_delay) {
    static my_time_t out_delay_buf[max_blob_packet_num + 100] = {0};
    out_delay = out_delay_buf;

    fec_inner_stat_t &inner_stat = ctx.stat.fec_to_normal;

    if (ctx.config.disable_fec || ctx.config.enable == 0) {
        if (data == 0) {
            out_n = 0;
            return 0;
        }
        inner_stat.input_packet_num++;
        inner_stat.input_packet_size += len;
        inner_stat.output_packet_num++;
        inner_stat.output_packet_size += len;

        out_n = 1;
        static char *data_static;
        data_static = data;
        static int len_static;
        len_static = len;
        out_arr = &data_static;
        out_len = &len_static;
        out_delay[0] = 0;
        return 0;
    }

    if (data != 0) {
        inner_stat.input_packet_num++;
        inner_stat.input_packet_size += len;
    }

    ctx.fec_decode.input(data, len);
    ctx.fec_decode.output(out_n, out_arr, out_len);
    for (int i = 0; i < out_n; i++) {
        out_delay_buf[i] = 0;
        inner_stat.output_packet_num++;
        inner_stat.output_packet_size += out_len[i];
    }

    if (data != 0) {
        mylog(log_trace, "from_fec_to_normal input_len=%d,output_n=%d,input_seq=%u\n", len, out_n, read_u32(data));
    }
    return 0;
}

int fec_send_outputs(fec_context_t &ctx, int out_n, char **out_arr, int *out_len, my_time_t *out_delay) {
    if (out_n <= 0) return 0;
    if (ctx.send_cb == 0) {
        mylog(log_warn, "fec_send_outputs without send_cb\n");
        return -1;
    }

    for (int i = 0; i < out_n; i++) {
        ctx.delay_manager.add(out_delay[i], ctx.send_cb, ctx.send_ctx, out_arr[i], out_len[i]);
    }
    return 0;
}
