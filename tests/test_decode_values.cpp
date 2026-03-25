// Decoded data value tests for the TDM analyzer.
//
// Verifies decoded data values using a counting pattern signal generator,
// covering all setting combinations (shift order, alignment, DSP mode,
// FS polarity, sampling edge, bit widths, channel counts) plus boundary
// conditions (non-power-of-2 widths, extreme padding, U8 slot boundary,
// counter wrapping).

#include "tdm_test_helpers.h"

// ===================================================================
// Happy Path Tests — Value Correctness
// ===================================================================

// Stereo 16-bit MSB-first left-aligned (baseline)
void test_stereo_16bit_baseline()
{
    Config c = DefaultConfig( "stereo-16bit", TEST_FRAMES );
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// LSB-first decode
void test_lsb_first()
{
    Config c = DefaultConfig( "lsb-first", TEST_FRAMES );
    c.shift_order = AnalyzerEnums::LsbFirst;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Right-aligned 24-bit data in 32-bit slot
void test_right_aligned_24in32()
{
    Config c = DefaultConfig( "right-aligned-24in32", TEST_FRAMES );
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 24;
    c.data_alignment = RIGHT_ALIGNED;
    c.sample_rate = U64( 48000 ) * 2 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Left-aligned 24-bit data in 32-bit slot
void test_left_aligned_24in32()
{
    Config c = DefaultConfig( "left-aligned-24in32", TEST_FRAMES );
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 24;
    c.data_alignment = LEFT_ALIGNED;
    c.sample_rate = U64( 48000 ) * 2 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// 8-channel slot numbering
void test_8channel_slot_numbering()
{
    Config c = DefaultConfig( "8channel", TEST_FRAMES );
    c.slots_per_frame = 8;
    c.sample_rate = U64( 48000 ) * 8 * 16 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// DSP Mode B
void test_dsp_mode_b()
{
    Config c = DefaultConfig( "dsp-mode-b", TEST_FRAMES );
    c.bit_alignment = DSP_MODE_B;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Frame sync inverted
void test_frame_sync_inverted()
{
    Config c = DefaultConfig( "fs-inverted", TEST_FRAMES );
    c.fs_inverted = FS_INVERTED;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Negative edge sampling
void test_neg_edge_sampling()
{
    Config c = DefaultConfig( "neg-edge", TEST_FRAMES );
    c.data_valid_edge = AnalyzerEnums::NegEdge;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// 32-bit data
void test_32bit_data()
{
    Config c = DefaultConfig( "32bit", TEST_FRAMES );
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 32;
    c.sample_rate = U64( 48000 ) * 2 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// 64-bit data
void test_64bit_data()
{
    Config c = DefaultConfig( "64bit", TEST_FRAMES );
    c.bits_per_slot = 64;
    c.data_bits_per_slot = 64;
    c.sample_rate = U64( 48000 ) * 2 * 64 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Mono (single slot per frame)
void test_mono_single_slot()
{
    Config c = DefaultConfig( "mono", TEST_FRAMES );
    c.slots_per_frame = 1;
    c.sample_rate = U64( 48000 ) * 1 * 16 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// ===================================================================
// Combination Tests — Multiple Settings Varied Together
// ===================================================================

// 8-channel, 24-in-32, right-aligned, LSB-first
void test_combo_8ch_24in32_right_lsb()
{
    Config c = DefaultConfig( "combo-8ch-24in32-right-lsb", TEST_FRAMES );
    c.slots_per_frame = 8;
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 24;
    c.data_alignment = RIGHT_ALIGNED;
    c.shift_order = AnalyzerEnums::LsbFirst;
    c.sample_rate = U64( 48000 ) * 8 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// DSP Mode B + frame sync inverted + negative edge
void test_combo_modeb_fsinv_negedge()
{
    Config c = DefaultConfig( "combo-modeb-fsinv-negedge", TEST_FRAMES );
    c.bit_alignment = DSP_MODE_B;
    c.fs_inverted = FS_INVERTED;
    c.data_valid_edge = AnalyzerEnums::NegEdge;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// 32-channel, 32-bit, left-aligned, LSB-first, DSP Mode B
void test_combo_32ch_32bit_lsb_modeb()
{
    Config c = DefaultConfig( "combo-32ch-32bit-lsb-modeb", TEST_FRAMES );
    c.slots_per_frame = 32;
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 32;
    c.shift_order = AnalyzerEnums::LsbFirst;
    c.bit_alignment = DSP_MODE_B;
    c.sample_rate = U64( 48000 ) * 32 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Mono, 24-in-32, right-aligned, FS inverted, 96 kHz
void test_combo_mono_24in32_right_fsinv_96k()
{
    Config c = DefaultConfig( "combo-mono-24in32-right-fsinv-96k", TEST_FRAMES );
    c.slots_per_frame = 1;
    c.frame_rate = 96000;
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 24;
    c.data_alignment = RIGHT_ALIGNED;
    c.fs_inverted = FS_INVERTED;
    c.sample_rate = U64( 96000 ) * 1 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Stereo, 16-bit, LSB-first, FS inverted, DSP Mode B, neg edge
void test_combo_all_nondefault()
{
    Config c = DefaultConfig( "combo-all-nondefault", TEST_FRAMES );
    c.shift_order = AnalyzerEnums::LsbFirst;
    c.fs_inverted = FS_INVERTED;
    c.bit_alignment = DSP_MODE_B;
    c.data_valid_edge = AnalyzerEnums::NegEdge;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// 4-channel, 24-in-32, left-aligned, with advanced analysis
void test_combo_4ch_24in32_advanced()
{
    Config c = DefaultConfig( "combo-4ch-24in32-advanced", TEST_FRAMES );
    c.slots_per_frame = 4;
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 24;
    c.data_alignment = LEFT_ALIGNED;
    c.advanced_analysis = true;
    c.sample_rate = U64( 48000 ) * 4 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// ===================================================================
// Boundary Value Tests
// ===================================================================

// 8-bit counter wraps through all 256 values (0x00-0xFF)
void test_counter_wrap_8bit()
{
    Config c = DefaultConfig( "counter-wrap-8bit", 200 );
    c.bits_per_slot = 8;
    c.data_bits_per_slot = 8;
    c.sample_rate = U64( 48000 ) * 2 * 8 * 4;
    auto frames = RunAndCollect( c );
    // 200 frames * 2 slots = 400 values, counter wraps at 256
    // Exercises 0xFF(255), 0x80(128), 0xAA(170), 0x55(85)
    VerifyCountingPattern( frames, c, 200 );
}

// 3-bit slots (non-power-of-2)
void test_3bit_slots()
{
    Config c = DefaultConfig( "3bit-slots", TEST_FRAMES );
    c.bits_per_slot = 3;
    c.data_bits_per_slot = 3;
    c.sample_rate = U64( 48000 ) * 2 * 3 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// 8-bit slots (byte-aligned, common in practice)
void test_8bit_slots()
{
    Config c = DefaultConfig( "8bit-slots", TEST_FRAMES );
    c.bits_per_slot = 8;
    c.data_bits_per_slot = 8;
    c.sample_rate = U64( 48000 ) * 2 * 8 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// LSB-first + right-aligned (8 data in 16-bit slot)
void test_lsb_right_aligned_8in16()
{
    Config c = DefaultConfig( "lsb-right-8in16", TEST_FRAMES );
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 8;
    c.data_alignment = RIGHT_ALIGNED;
    c.shift_order = AnalyzerEnums::LsbFirst;
    c.sample_rate = U64( 48000 ) * 2 * 16 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// LSB-first + left-aligned (8 data in 16-bit slot)
void test_lsb_left_aligned_8in16()
{
    Config c = DefaultConfig( "lsb-left-8in16", TEST_FRAMES );
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 8;
    c.data_alignment = LEFT_ALIGNED;
    c.shift_order = AnalyzerEnums::LsbFirst;
    c.sample_rate = U64( 48000 ) * 2 * 16 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Extreme padding, 2 data bits in 64-bit slot, right-aligned
void test_extreme_padding_right_2in64()
{
    Config c = DefaultConfig( "extreme-pad-right-2in64", TEST_FRAMES );
    c.bits_per_slot = 64;
    c.data_bits_per_slot = 2;
    c.data_alignment = RIGHT_ALIGNED;
    c.sample_rate = U64( 48000 ) * 2 * 64 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Extreme padding, 2 data bits in 64-bit slot, left-aligned
void test_extreme_padding_left_2in64()
{
    Config c = DefaultConfig( "extreme-pad-left-2in64", TEST_FRAMES );
    c.bits_per_slot = 64;
    c.data_bits_per_slot = 2;
    c.data_alignment = LEFT_ALIGNED;
    c.sample_rate = U64( 48000 ) * 2 * 64 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// 63 data bits in 64-bit slot (1 bit of padding)
void test_63in64_right()
{
    Config c = DefaultConfig( "63in64-right", 20 );
    c.bits_per_slot = 64;
    c.data_bits_per_slot = 63;
    c.data_alignment = RIGHT_ALIGNED;
    c.sample_rate = U64( 48000 ) * 2 * 64 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, 20 );
}

// 256 slots (U8 mType boundary)
void test_256_slots()
{
    Config c = DefaultConfig( "256-slots", 3 );
    c.slots_per_frame = 256;
    c.bits_per_slot = 2;
    c.data_bits_per_slot = 2;
    c.sample_rate = U64( 48000 ) * 256 * 2 * 4;
    auto frames = RunAndCollect( c );

    // Verify slot numbers cycle 0-255 and data values are correct
    CHECK( frames.size() >= 256, "Should have at least one full frame of 256 slots" );
    for( U32 i = 0; i < 256; i++ )
    {
        U8 expected_slot = U8( i );
        U64 expected_data = i % 4; // 2-bit data wraps at 4
        std::ostringstream oss;
        if( frames[ i ].slot != expected_slot )
        {
            oss << "Slot " << i << ": expected slot number " << (int)expected_slot
                << ", got " << (int)frames[ i ].slot;
            CHECK( false, oss.str() );
        }
        if( frames[ i ].data != expected_data )
        {
            oss << "Slot " << i << ": expected data " << expected_data
                << ", got " << frames[ i ].data;
            CHECK( false, oss.str() );
        }
    }
}

// Right-aligned with zero padding (data_bits == bits_per_slot)
void test_right_aligned_zero_padding()
{
    Config c = DefaultConfig( "right-zero-pad", TEST_FRAMES );
    c.data_alignment = RIGHT_ALIGNED;
    // bits_per_slot = data_bits_per_slot = 16 (default), so padding = 0
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}
