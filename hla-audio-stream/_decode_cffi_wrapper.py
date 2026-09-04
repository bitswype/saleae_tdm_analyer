"""cffi wrapper providing the same interface as the Cython FastDecoder.

Usage:
    from _decode_cffi_wrapper import CffiDecoder
"""
from _decode_cffi import ffi, lib


class CffiDecoder:
    """Drop-in replacement for FastDecoder using cffi."""

    def __init__(self, slot_list, bit_depth, batch_size, frame_byte_size,
                 source_bit_depth=0):
        if source_bit_depth <= 0:
            source_bit_depth = bit_depth
        if not 2 <= source_bit_depth <= 64:
            raise ValueError("source_bit_depth must be 2-64")

        self._state = ffi.new('DecoderState*')
        self._slot_list_c = ffi.new('int[]', slot_list)
        self._n_slots = len(slot_list)
        self._slot_list = list(slot_list)

        lib.decoder_init(self._state, self._slot_list_c, len(slot_list),
                         bit_depth, batch_size, frame_byte_size,
                         source_bit_depth)

        # Python-owned batch buffer; share memory with C via ffi.from_buffer
        self._batch_buf_py = bytearray(batch_size * frame_byte_size)
        self._state.batch_buf = ffi.from_buffer(self._batch_buf_py)

    def set_sample_rate_known(self):
        lib.decoder_set_sample_rate_known(self._state)

    def set_source_bit_depth(self, src_bits):
        """Adopt a new LLA source width (from the 'format' frame)."""
        if not 2 <= src_bits <= 64:
            raise ValueError("source_bit_depth must be 2-64")
        lib.decoder_set_source_bit_depth(self._state, int(src_bits))

    @property
    def source_bit_depth(self):
        return self._state.src_bits

    @property
    def frame_count(self):
        return self._state.frame_count

    @property
    def batch_count(self):
        return self._state.batch_count

    @property
    def last_frame_num(self):
        if self._state.last_frame_num_valid:
            return self._state.last_frame_num
        return None

    def get_batch_data(self):
        return bytes(self._batch_buf_py[:self._state.batch_offset])

    def reset_batch(self):
        self._state.batch_offset = 0
        self._state.batch_count = 0

    def get_accum(self):
        result = {}
        for i in range(self._n_slots):
            s = self._slot_list[i]
            if self._state.accum_valid[s]:
                result[s] = self._state.accum[s]
        return result

    def process_frame(self, frame):
        """Process one frame. Returns 0, 1 (flush needed), or -1 (filtered)."""
        if frame.type != 'slot':
            return -1

        d = frame.data
        slot = d['slot']
        frame_num = d['frame_number']
        has_error = 1 if (d.get('short_slot') or d.get('bitclock_error')) else 0
        data = d.get('data', 0) if not has_error else 0

        return lib.decoder_process_frame(self._state, int(slot), int(frame_num),
                                         int(data), has_error)
