"""Build script for cffi fast decode extension.

Usage:
    cd hla-audio-stream
    python _decode_cffi_build.py
"""
import cffi

ffi = cffi.FFI()

ffi.cdef("""
    typedef struct {
        int slot_set[256];
        int slot_list[256];
        int n_slots;

        long long sign_mask;
        long long sign_threshold;
        long long sign_subtract;

        long long last_frame_num;
        int last_frame_num_valid;

        long long accum[256];
        int accum_valid[256];

        unsigned char* batch_buf;
        int batch_offset;
        int batch_count;
        int batch_size;
        int frame_byte_size;
        int bytes_per_sample;

        long long frame_count;
        int sample_rate_known;
    } DecoderState;

    void decoder_init(DecoderState* state, int* slot_list, int n_slots,
                      int bit_depth, int batch_size, int frame_byte_size);
    int decoder_process_frame(DecoderState* state, int slot, long long frame_num,
                              long long data, int has_error);
    void decoder_set_sample_rate_known(DecoderState* state);
""")

ffi.set_source("_decode_cffi", r"""
#include <string.h>

typedef struct {
    int slot_set[256];
    int slot_list[256];
    int n_slots;

    long long sign_mask;
    long long sign_threshold;
    long long sign_subtract;

    long long last_frame_num;
    int last_frame_num_valid;

    long long accum[256];
    int accum_valid[256];

    unsigned char* batch_buf;
    int batch_offset;
    int batch_count;
    int batch_size;
    int frame_byte_size;
    int bytes_per_sample;

    long long frame_count;
    int sample_rate_known;
} DecoderState;

static void pack_frame(DecoderState* state) {
    int i, s;
    long long sample;
    int offset = state->batch_offset;
    unsigned char* buf = state->batch_buf;

    for (i = 0; i < state->n_slots; i++) {
        s = state->slot_list[i];
        sample = state->accum_valid[s] ? state->accum[s] : 0;

        if (state->bytes_per_sample == 2) {
            short s16 = (short)sample;
            buf[offset]     = (unsigned char)(s16 & 0xFF);
            buf[offset + 1] = (unsigned char)((s16 >> 8) & 0xFF);
            offset += 2;
        } else {
            int s32 = (int)sample;
            buf[offset]     = (unsigned char)(s32 & 0xFF);
            buf[offset + 1] = (unsigned char)((s32 >> 8) & 0xFF);
            buf[offset + 2] = (unsigned char)((s32 >> 16) & 0xFF);
            buf[offset + 3] = (unsigned char)((s32 >> 24) & 0xFF);
            offset += 4;
        }
    }
    state->batch_offset = offset;
    state->batch_count++;
    state->frame_count++;
}

void decoder_init(DecoderState* state, int* slot_list, int n_slots,
                  int bit_depth, int batch_size, int frame_byte_size) {
    int i;
    memset(state, 0, sizeof(DecoderState));

    state->n_slots = n_slots;
    for (i = 0; i < n_slots; i++) {
        state->slot_list[i] = slot_list[i];
        state->slot_set[slot_list[i]] = 1;
    }

    state->bytes_per_sample = (bit_depth > 16) ? 4 : 2;
    state->sign_mask = ((long long)1 << bit_depth) - 1;
    state->sign_threshold = (long long)1 << (bit_depth - 1);
    state->sign_subtract = (long long)1 << bit_depth;

    state->batch_size = batch_size;
    state->frame_byte_size = frame_byte_size;
}

int decoder_process_frame(DecoderState* state, int slot, long long frame_num,
                          long long data, int has_error) {
    int i;
    int flush_needed = 0;

    /* Slot filter */
    if (slot < 0 || slot > 255 || !state->slot_set[slot])
        return -1;

    /* Frame boundary detection */
    if (state->last_frame_num_valid) {
        if (frame_num != state->last_frame_num) {
            if (state->sample_rate_known) {
                pack_frame(state);
                if (state->batch_count >= state->batch_size)
                    flush_needed = 1;
            }
            /* Reset accumulator */
            for (i = 0; i < 256; i++)
                state->accum_valid[i] = 0;
        }
    }

    state->last_frame_num = frame_num;
    state->last_frame_num_valid = 1;

    /* Accumulate sample (skip if error) */
    if (!has_error) {
        long long v = data & state->sign_mask;
        if (v >= state->sign_threshold)
            v -= state->sign_subtract;
        state->accum[slot] = v;
        state->accum_valid[slot] = 1;
    }

    return flush_needed ? 1 : 0;
}

void decoder_set_sample_rate_known(DecoderState* state) {
    state->sample_rate_known = 1;
}
""")

if __name__ == '__main__':
    ffi.compile(verbose=True)
