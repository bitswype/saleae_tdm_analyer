# cython: language_level=3, boundscheck=False, wraparound=False
"""Cython fast decode path for TdmAudioStream HLA.

Implements the hot loop of decode() in C-level code: slot filtering,
frame boundary detection, source-to-output width conversion, sample
accumulation, and PCM packing into a pre-allocated batch buffer.

The frame object itself comes from Logic 2 (Python), so dict access must
use Python operations. The win is in eliminating per-sample Python overhead
for sign conversion, accumulator management, and struct.pack.

Width conversion must match _tdm_utils.convert_sample exactly; the oracle
in tests/test_hla_decode.py checks this backend against it.
"""

cdef class FastDecoder:
    # Slot set as a C bitmap (max 256 slots)
    cdef int slot_set[256]
    cdef int slot_list[256]
    cdef int n_slots

    # Accumulator: C arrays indexed by slot number
    cdef long long accum[256]
    cdef bint accum_valid[256]

    # Output width
    cdef int bit_depth
    cdef int bytes_per_sample

    # Source width -> output width rescale (see _tdm_utils.conversion_params)
    cdef int src_bits
    cdef unsigned long long src_mask
    cdef unsigned long long src_sign_bit
    cdef int shift
    cdef long long out_max

    # Batch buffer
    cdef int batch_size
    cdef int frame_byte_size
    cdef bytearray _batch_buf_py
    cdef unsigned char* batch_buf
    cdef int _batch_count
    cdef int _batch_offset

    # Frame tracking
    cdef long long _last_frame_num
    cdef bint _last_frame_num_valid
    cdef long long _frame_count

    # Sample rate known flag (skip sample rate derivation)
    cdef bint _sample_rate_known

    def __init__(self, list slot_list, int bit_depth, int batch_size,
                 int frame_byte_size, int source_bit_depth=0):
        cdef int i, slot

        # Initialize slot set bitmap and slot list
        for i in range(256):
            self.slot_set[i] = 0
            self.accum[i] = 0
            self.accum_valid[i] = 0
            self.slot_list[i] = 0

        self.n_slots = len(slot_list)
        for i in range(self.n_slots):
            slot = slot_list[i]
            self.slot_list[i] = slot
            if 0 <= slot < 256:
                self.slot_set[slot] = 1

        # Output width parameters
        self.bit_depth = bit_depth
        self.bytes_per_sample = 4 if bit_depth > 16 else 2

        # Source width defaults to the output width (pre-v2.6.0 behavior)
        if source_bit_depth <= 0:
            source_bit_depth = bit_depth
        if source_bit_depth < 2 or source_bit_depth > 64:
            raise ValueError("source_bit_depth must be 2-64")
        self._configure_source(source_bit_depth)

        # Batch buffer
        self.batch_size = batch_size
        self.frame_byte_size = frame_byte_size
        self._batch_buf_py = bytearray(batch_size * frame_byte_size)
        self.batch_buf = <unsigned char*><bytearray>self._batch_buf_py
        self._batch_count = 0
        self._batch_offset = 0

        # Frame tracking
        self._last_frame_num = 0
        self._last_frame_num_valid = 0
        self._frame_count = 0

        # Sample rate not known yet
        self._sample_rate_known = 0

    cdef void _configure_source(self, int src_bits):
        self.src_bits = src_bits
        if src_bits >= 64:
            self.src_mask = ~(<unsigned long long>0)
        else:
            self.src_mask = (<unsigned long long>1 << src_bits) - 1
        self.src_sign_bit = <unsigned long long>1 << (src_bits - 1)
        self.shift = src_bits - self.bit_depth
        self.out_max = (<long long>1 << (self.bit_depth - 1)) - 1

    def set_source_bit_depth(self, int src_bits):
        """Adopt a new source width (from the LLA's 'format' frame)."""
        if src_bits < 2 or src_bits > 64:
            raise ValueError("source_bit_depth must be 2-64")
        self._configure_source(src_bits)

    cdef inline long long _convert(self, long long raw):
        """Interpret raw at the source width and rescale to the output width.

        Mirrors _tdm_utils.convert_sample. Sign extension is done with
        unsigned arithmetic to stay defined for 63- and 64-bit sources, the
        right shift rounds half up without an intermediate add (no overflow
        at the top of the range), and positive full scale is clamped.
        """
        cdef unsigned long long u = (<unsigned long long>raw) & self.src_mask
        cdef long long v
        cdef int sh = self.shift
        if u & self.src_sign_bit:
            u |= ~self.src_mask
        v = <long long>u
        if sh > 0:
            v = (v >> sh) + ((v >> (sh - 1)) & 1)
            if v > self.out_max:
                v = self.out_max
        elif sh < 0:
            v = <long long>((<unsigned long long>v) << (-sh))
        return v

    cdef int _pack_frame(self) except -2:
        """Pack accumulated samples into batch buffer.

        Writes little-endian PCM bytes directly. Returns 1 if batch is full
        after packing, 0 otherwise.
        """
        cdef int i, slot, offset
        cdef long long val
        cdef short s16
        cdef int s32
        cdef unsigned char* buf = self.batch_buf

        offset = self._batch_offset

        if self.bit_depth == 16:
            for i in range(self.n_slots):
                slot = self.slot_list[i]
                if 0 <= slot < 256 and self.accum_valid[slot]:
                    val = self.accum[slot]
                else:
                    val = 0
                s16 = <short>val
                buf[offset] = <unsigned char>(s16 & 0xFF)
                buf[offset + 1] = <unsigned char>((s16 >> 8) & 0xFF)
                offset += 2
        else:
            # 32-bit
            for i in range(self.n_slots):
                slot = self.slot_list[i]
                if 0 <= slot < 256 and self.accum_valid[slot]:
                    val = self.accum[slot]
                else:
                    val = 0
                s32 = <int>val
                buf[offset] = <unsigned char>(s32 & 0xFF)
                buf[offset + 1] = <unsigned char>((s32 >> 8) & 0xFF)
                buf[offset + 2] = <unsigned char>((s32 >> 16) & 0xFF)
                buf[offset + 3] = <unsigned char>((s32 >> 24) & 0xFF)
                offset += 4

        self._batch_offset = offset
        self._batch_count += 1
        self._frame_count += 1

        if self._batch_count >= self.batch_size:
            return 1
        return 0

    def process_frame(self, frame):
        """Process one FrameV2 from the upstream TdmAnalyzer LLA.

        Args:
            frame: AnalyzerFrame-like object with .type and .data dict.

        Returns:
            0 - normal (frame processed or filtered)
            1 - batch full, caller must flush
           -1 - filtered/skipped (non-slot frame type)
        """
        cdef dict d
        cdef int slot
        cdef long long frame_num
        cdef long long raw_val
        cdef int batch_full

        # Filter non-slot frames
        if frame.type != 'slot':
            return -1

        d = frame.data
        slot = d['slot']
        frame_num = d['frame_number']

        # Slot filtering
        if slot < 0 or slot >= 256 or self.slot_set[slot] == 0:
            return 0

        # Frame boundary detection: flush-before-accumulate
        if self._last_frame_num_valid and frame_num != self._last_frame_num:
            # New TDM frame started -- flush the completed accumulator
            if self._sample_rate_known:
                batch_full = self._pack_frame()
            else:
                batch_full = 0
            # Reset accumulator
            self._reset_accum()

            # Update last_frame_num BEFORE checking batch_full,
            # and accumulate current sample
            self._last_frame_num = frame_num

            # Accumulate current sample (if not error)
            if not (d.get('short_slot') or d.get('bitclock_error')):
                raw_val = d.get('data', 0)
                self.accum[slot] = self._convert(raw_val)
                self.accum_valid[slot] = 1

            if batch_full == 1:
                return 1
            return 0

        # Same frame or first frame
        self._last_frame_num = frame_num
        self._last_frame_num_valid = 1

        # Skip error frames
        if not (d.get('short_slot') or d.get('bitclock_error')):
            raw_val = d.get('data', 0)
            self.accum[slot] = self._convert(raw_val)
            self.accum_valid[slot] = 1

        return 0

    cdef void _reset_accum(self):
        """Reset accumulator arrays."""
        cdef int i
        for i in range(256):
            self.accum[i] = 0
            self.accum_valid[i] = 0

    @property
    def frame_count(self):
        return self._frame_count

    @property
    def batch_count(self):
        return self._batch_count

    @property
    def last_frame_num(self):
        if self._last_frame_num_valid:
            return self._last_frame_num
        return None

    @property
    def source_bit_depth(self):
        return self.src_bits

    def get_batch_data(self):
        """Return the batch buffer contents as bytes, up to current offset."""
        if self._batch_count == 0:
            return b''
        return bytes(self._batch_buf_py[:self._batch_offset])

    def reset_batch(self):
        """Reset batch buffer after flush."""
        self._batch_count = 0
        self._batch_offset = 0

    def get_accum(self):
        """Return accumulator as a Python dict {slot: signed_value}.

        Only includes slots that have been written to (accum_valid == True).
        """
        cdef int i
        cdef dict result = {}
        for i in range(256):
            if self.accum_valid[i]:
                result[i] = self.accum[i]
        return result

    def set_sample_rate_known(self):
        """Signal that sample rate has been derived; enable PCM packing."""
        self._sample_rate_known = 1
