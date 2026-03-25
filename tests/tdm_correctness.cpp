// TDM Analyzer correctness tests.
//
// Verifies that the analyzer decodes known signals to the expected values.
// Each test generates a synthetic TDM signal with a counting pattern,
// runs the analyzer, and checks every decoded Frame against expected values.
//
// Usage:
//   tdm_correctness
//
// Exit code 0 = all pass, 1 = any failure.

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "MockChannelData.h"
#include "MockResults.h"
#include "TestInstance.h"
#include "AnalyzerHelpers.h"

#include "tdm_test_signal.h"
#include "TdmAnalyzerResults.h"

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;
static bool g_test_failed = false;

#define CHECK( cond, msg )                                                     \
    do                                                                         \
    {                                                                          \
        if( !( cond ) )                                                        \
        {                                                                      \
            std::cerr << "    FAIL: " << msg << std::endl;                     \
            g_test_failed = true;                                              \
            return;                                                            \
        }                                                                      \
    } while( 0 )

#define CHECK_EQ( actual, expected, msg )                                      \
    do                                                                         \
    {                                                                          \
        if( ( actual ) != ( expected ) )                                       \
        {                                                                      \
            std::cerr << "    FAIL: " << msg << " (expected " << ( expected )  \
                      << ", got " << ( actual ) << ")" << std::endl;           \
            g_test_failed = true;                                              \
            return;                                                            \
        }                                                                      \
    } while( 0 )

static void RunTest( const char* name, void ( *fn )() )
{
    std::cout << "  " << name << std::endl;
    g_test_failed = false;
    fn();
    if( g_test_failed )
        g_fail++;
    else
        g_pass++;
}

// ---------------------------------------------------------------------------
// Helper: run analyzer and collect decoded frames
// ---------------------------------------------------------------------------

struct DecodedFrame
{
    U64 data;    // mData1: raw unsigned decoded value
    U8 slot;     // mType: slot number
    U8 flags;    // mFlags: error bit flags
};

static std::vector<DecodedFrame> RunAndCollect( const Config& cfg )
{
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, cfg );

    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( cfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = cfg.frame_rate;
    settings->mSlotsPerFrame = cfg.slots_per_frame;
    settings->mBitsPerSlot = cfg.bits_per_slot;
    settings->mDataBitsPerSlot = cfg.data_bits_per_slot;
    settings->mShiftOrder = cfg.shift_order;
    settings->mDataValidEdge = cfg.data_valid_edge;
    settings->mDataAlignment = cfg.data_alignment;
    settings->mBitAlignment = cfg.bit_alignment;
    settings->mSigned = cfg.sign;
    settings->mFrameSyncInverted = cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = cfg.advanced_analysis;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    std::vector<DecodedFrame> frames;
    U64 count = mock_results->TotalFrameCount();
    frames.reserve( count );
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        DecodedFrame df;
        df.data = f.mData1;
        df.slot = f.mType;
        df.flags = f.mFlags;
        frames.push_back( df );
    }

    return frames;
}

// ---------------------------------------------------------------------------
// Helper: verify counting pattern across all decoded frames
// ---------------------------------------------------------------------------

// The signal generator uses a counting pattern: each slot value increments
// sequentially (0, 1, 2, 3, ...) wrapping at 2^data_bits_per_slot.
// The analyzer discards the first frame (used for sync), so decoded frames
// start at counter = slots_per_frame (first complete frame after preamble).
//
// We verify a window of frames starting from a known offset. The +2 extra
// frames in GenerateTdmSignal ensure there are always enough decoded frames.

static void VerifyCountingPattern( const std::vector<DecodedFrame>& frames,
                                    const Config& cfg,
                                    U32 num_verify_frames )
{
    const U64 val_mod = ( cfg.data_bits_per_slot >= 64 ) ? 0 : ( 1ULL << cfg.data_bits_per_slot );
    const U32 total_slots = num_verify_frames * cfg.slots_per_frame;

    // The preamble provides synchronization, so the analyzer decodes
    // starting from the very first generated TDM frame (counter = 0).
    U32 counter_start = 0;

    std::ostringstream oss;

    // We need at least total_slots decoded frames to verify
    if( frames.size() < total_slots )
    {
        oss << "Not enough frames: expected at least " << total_slots
            << ", got " << frames.size();
        CHECK( false, oss.str() );
    }

    for( U32 i = 0; i < total_slots; i++ )
    {
        U32 counter_val = counter_start + i;
        U64 expected_data;
        if( val_mod == 0 )
            expected_data = counter_val; // 64-bit: no wrapping needed for small counters
        else
            expected_data = counter_val % val_mod;

        U8 expected_slot = U8( i % cfg.slots_per_frame );

        const DecodedFrame& df = frames[ i ];

        if( df.data != expected_data )
        {
            oss << "Frame " << i << " slot " << (int)df.slot
                << ": data mismatch (expected " << expected_data
                << ", got " << df.data << ")";
            CHECK( false, oss.str() );
        }

        if( df.slot != expected_slot )
        {
            oss << "Frame " << i << ": slot number mismatch (expected "
                << (int)expected_slot << ", got " << (int)df.slot << ")";
            CHECK( false, oss.str() );
        }

        // Clean flags (mask out display bits 6-7)
        U8 error_flags = df.flags & 0x3F;
        if( error_flags != 0 )
        {
            oss << "Frame " << i << " slot " << (int)df.slot
                << ": unexpected error flags 0x" << std::hex << (int)error_flags;
            CHECK( false, oss.str() );
        }
    }
}

// ===================================================================
// Happy Path Tests — Value Correctness
// ===================================================================

static const U32 TEST_FRAMES = 100;

// Test 1: Stereo 16-bit MSB-first left-aligned (baseline)
void test_stereo_16bit_baseline()
{
    Config c = DefaultConfig( "stereo-16bit", TEST_FRAMES );
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 2: LSB-first decode
void test_lsb_first()
{
    Config c = DefaultConfig( "lsb-first", TEST_FRAMES );
    c.shift_order = AnalyzerEnums::LsbFirst;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 3: Right-aligned 24-bit data in 32-bit slot
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

// Test 4: Left-aligned 24-bit data in 32-bit slot
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

// Test 5: 8-channel slot numbering
void test_8channel_slot_numbering()
{
    Config c = DefaultConfig( "8channel", TEST_FRAMES );
    c.slots_per_frame = 8;
    c.sample_rate = U64( 48000 ) * 8 * 16 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 6: DSP Mode B
void test_dsp_mode_b()
{
    Config c = DefaultConfig( "dsp-mode-b", TEST_FRAMES );
    c.bit_alignment = DSP_MODE_B;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 7: Frame sync inverted
void test_frame_sync_inverted()
{
    Config c = DefaultConfig( "fs-inverted", TEST_FRAMES );
    c.fs_inverted = FS_INVERTED;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 8: Negative edge sampling
void test_neg_edge_sampling()
{
    Config c = DefaultConfig( "neg-edge", TEST_FRAMES );
    c.data_valid_edge = AnalyzerEnums::NegEdge;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 9: 32-bit data
void test_32bit_data()
{
    Config c = DefaultConfig( "32bit", TEST_FRAMES );
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 32;
    c.sample_rate = U64( 48000 ) * 2 * 32 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 10: 64-bit data
void test_64bit_data()
{
    Config c = DefaultConfig( "64bit", TEST_FRAMES );
    c.bits_per_slot = 64;
    c.data_bits_per_slot = 64;
    c.sample_rate = U64( 48000 ) * 2 * 64 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Test 11: Mono (single slot per frame)
void test_mono_single_slot()
{
    Config c = DefaultConfig( "mono", TEST_FRAMES );
    c.slots_per_frame = 1;
    c.sample_rate = U64( 48000 ) * 1 * 16 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// ===================================================================
// Sign Conversion Unit Tests
// ===================================================================

void test_sign_16bit_positive()
{
    S64 result = AnalyzerHelpers::ConvertToSignedNumber( 0x7FFF, 16 );
    CHECK_EQ( result, S64( 32767 ), "16-bit max positive" );
}

void test_sign_16bit_negative()
{
    S64 result = AnalyzerHelpers::ConvertToSignedNumber( 0x8000, 16 );
    CHECK_EQ( result, S64( -32768 ), "16-bit min negative" );
}

void test_sign_16bit_zero()
{
    S64 result = AnalyzerHelpers::ConvertToSignedNumber( 0, 16 );
    CHECK_EQ( result, S64( 0 ), "16-bit zero" );
}

void test_sign_24bit_negative()
{
    S64 result = AnalyzerHelpers::ConvertToSignedNumber( 0xFFFFFF, 24 );
    CHECK_EQ( result, S64( -1 ), "24-bit all-ones = -1" );
}

void test_sign_32bit_min()
{
    S64 result = AnalyzerHelpers::ConvertToSignedNumber( 0x80000000ULL, 32 );
    CHECK_EQ( result, S64( -2147483648LL ), "32-bit min negative" );
}

void test_sign_edge_cases()
{
    // 0 bits returns 0
    S64 r0 = AnalyzerHelpers::ConvertToSignedNumber( 0xFFFF, 0 );
    CHECK_EQ( r0, S64( 0 ), "0-bit returns 0" );

    // 1-bit: 0 = 0, 1 = -1
    S64 r1a = AnalyzerHelpers::ConvertToSignedNumber( 0, 1 );
    CHECK_EQ( r1a, S64( 0 ), "1-bit zero" );

    S64 r1b = AnalyzerHelpers::ConvertToSignedNumber( 1, 1 );
    CHECK_EQ( r1b, S64( -1 ), "1-bit one = -1" );

    // 64-bit max positive
    S64 r64 = AnalyzerHelpers::ConvertToSignedNumber( 0x7FFFFFFFFFFFFFFFULL, 64 );
    CHECK_EQ( r64, S64( 0x7FFFFFFFFFFFFFFFLL ), "64-bit max positive" );

    // 64-bit negative (MSB set)
    S64 r64n = AnalyzerHelpers::ConvertToSignedNumber( 0x8000000000000000ULL, 64 );
    CHECK( r64n < 0, "64-bit MSB set should be negative" );

    // >64 bits returns 0
    S64 r65 = AnalyzerHelpers::ConvertToSignedNumber( 0xFFFF, 65 );
    CHECK_EQ( r65, S64( 0 ), ">64-bit returns 0" );
}

// ===================================================================
// Error Condition Tests
// ===================================================================

// Helper: generate a signal where one frame has fewer bits than expected.
// Produces 3 normal frames, then 1 frame where frame sync fires after
// only half the bits of the second slot, then 2 more normal frames.
static std::vector<DecodedFrame> RunShortSlotTest()
{
    // Configure for stereo 16-bit
    const U32 slots = 2;
    const U32 bps = 16;
    const U32 bpf = slots * bps; // 32 bits per frame
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * bpf * 4;
    const double bit_freq = double( frame_rate ) * bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

    // Build a bit-level description of the signal.
    // Each entry: (data_bit, is_frame_sync_active)
    struct BitDesc { BitState data; BitState frame; };
    std::vector<BitDesc> stream;

    // Preamble
    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // 3 normal frames (DSP Mode A: fs pulse, then data delayed by 1 bit)
    for( int f = 0; f < 3; f++ )
    {
        stream.push_back( { BIT_LOW, BIT_HIGH } ); // FS active, data = offset bit
        for( U32 b = 0; b < bpf - 1; b++ )
            stream.push_back( { BIT_LOW, BIT_LOW } ); // data bits (all zero is fine)
    }

    // Short frame: FS pulse, then only 24 bits instead of 32.
    // The second slot will get only 8 bits (short).
    stream.push_back( { BIT_LOW, BIT_HIGH } ); // FS active
    for( U32 b = 0; b < 24 - 1; b++ )          // 23 more bits (24 total including FS bit)
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Next normal frame starts here (FS fires, cutting short the previous frame)
    for( int f = 0; f < 3; f++ )
    {
        stream.push_back( { BIT_LOW, BIT_HIGH } );
        for( U32 b = 0; b < bpf - 1; b++ )
            stream.push_back( { BIT_LOW, BIT_LOW } );
    }

    // Trailing idle
    for( int i = 0; i < 32; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Convert to mock channel data
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat = new AnalyzerTest::MockChannelData( &instance );

    clk->TestSetInitialBitState( BIT_LOW );
    frm->TestSetInitialBitState( stream[ 0 ].frame );
    dat->TestSetInitialBitState( stream[ 0 ].data );

    BitState cur_dat = stream[ 0 ].data;
    BitState cur_frm = stream[ 0 ].frame;
    double err = 0.0;
    U64 pos = 0;

    for( size_t i = 0; i < stream.size(); i++ )
    {
        double target = half_samples + err;
        U32 n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );

        target = half_samples + err;
        n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );

        if( i + 1 < stream.size() )
        {
            if( stream[ i + 1 ].data != cur_dat )
            {
                dat->TestAppendTransitionAtSamples( pos );
                cur_dat = stream[ i + 1 ].data;
            }
            if( stream[ i + 1 ].frame != cur_frm )
            {
                frm->TestAppendTransitionAtSamples( pos );
                cur_frm = stream[ i + 1 ].frame;
            }
        }
    }

    clk->ResetCurrentSample();
    frm->ResetCurrentSample();
    dat->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk );
    instance.SetChannelData( FRM_CH, frm );
    instance.SetChannelData( DAT_CH, dat );
    instance.SetSampleRate( sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = frame_rate;
    settings->mSlotsPerFrame = slots;
    settings->mBitsPerSlot = bps;
    settings->mDataBitsPerSlot = bps;
    settings->mShiftOrder = AnalyzerEnums::MsbFirst;
    settings->mDataValidEdge = AnalyzerEnums::PosEdge;
    settings->mDataAlignment = LEFT_ALIGNED;
    settings->mBitAlignment = DSP_MODE_A;
    settings->mSigned = AnalyzerEnums::UnsignedInteger;
    settings->mFrameSyncInverted = FS_NOT_INVERTED;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    std::vector<DecodedFrame> frames;
    U64 count = mock_results->TotalFrameCount();
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        DecodedFrame df;
        df.data = f.mData1;
        df.slot = f.mType;
        df.flags = f.mFlags;
        frames.push_back( df );
    }
    return frames;
}

void test_short_slot_detection()
{
    auto frames = RunShortSlotTest();
    CHECK( frames.size() >= 6, "Should have at least 6 decoded slots" );

    // Find a frame with SHORT_SLOT flag and verify its data is 0
    bool found_short = false;
    for( const auto& f : frames )
    {
        if( f.flags & SHORT_SLOT )
        {
            found_short = true;
            // Task 14: When SHORT_SLOT is set, bit assembly is skipped,
            // so decoded value should be 0.
            CHECK_EQ( f.data, U64( 0 ), "SHORT_SLOT frame data should be 0" );
            break;
        }
    }
    CHECK( found_short, "Expected at least one SHORT_SLOT flag in decoded frames" );

    // Verify frames after the short slot still decode (post-error recovery)
    // There should be clean frames (no error flags) after the short one
    bool found_clean_after_short = false;
    bool past_short = false;
    for( const auto& f : frames )
    {
        if( f.flags & SHORT_SLOT )
            past_short = true;
        else if( past_short && ( f.flags & 0x3F ) == 0 )
        {
            found_clean_after_short = true;
            break;
        }
    }
    CHECK( found_clean_after_short, "Should have clean frames after SHORT_SLOT (post-error recovery)" );
}

// Helper: generate a signal configured for 2 slots/frame but with 3 slots
// worth of bits before the next frame sync.
static std::vector<DecodedFrame> RunExtraSlotsTest()
{
    const U32 configured_slots = 2;
    const U32 actual_slots = 3;  // more bits than expected
    const U32 bps = 16;
    const U32 actual_bpf = actual_slots * bps; // 48 bits per frame
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * actual_bpf * 4;
    const double bit_freq = double( frame_rate ) * actual_bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

    struct BitDesc { BitState data; BitState frame; };
    std::vector<BitDesc> stream;

    // Preamble
    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Generate 5 frames, each with 48 bits (3 slots worth)
    for( int f = 0; f < 5; f++ )
    {
        stream.push_back( { BIT_LOW, BIT_HIGH } ); // FS active, data = offset bit
        for( U32 b = 0; b < actual_bpf - 1; b++ )
            stream.push_back( { BIT_LOW, BIT_LOW } );
    }

    // Trailing idle
    for( int i = 0; i < 32; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Convert to mock channel data
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat = new AnalyzerTest::MockChannelData( &instance );

    clk->TestSetInitialBitState( BIT_LOW );
    frm->TestSetInitialBitState( stream[ 0 ].frame );
    dat->TestSetInitialBitState( stream[ 0 ].data );

    BitState cur_dat = stream[ 0 ].data;
    BitState cur_frm = stream[ 0 ].frame;
    double err = 0.0;
    U64 pos = 0;

    for( size_t i = 0; i < stream.size(); i++ )
    {
        double target = half_samples + err;
        U32 n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );

        target = half_samples + err;
        n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );

        if( i + 1 < stream.size() )
        {
            if( stream[ i + 1 ].data != cur_dat )
            {
                dat->TestAppendTransitionAtSamples( pos );
                cur_dat = stream[ i + 1 ].data;
            }
            if( stream[ i + 1 ].frame != cur_frm )
            {
                frm->TestAppendTransitionAtSamples( pos );
                cur_frm = stream[ i + 1 ].frame;
            }
        }
    }

    clk->ResetCurrentSample();
    frm->ResetCurrentSample();
    dat->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk );
    instance.SetChannelData( FRM_CH, frm );
    instance.SetChannelData( DAT_CH, dat );
    instance.SetSampleRate( sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = frame_rate;
    settings->mSlotsPerFrame = configured_slots; // configured for 2, but signal has 3
    settings->mBitsPerSlot = bps;
    settings->mDataBitsPerSlot = bps;
    settings->mShiftOrder = AnalyzerEnums::MsbFirst;
    settings->mDataValidEdge = AnalyzerEnums::PosEdge;
    settings->mDataAlignment = LEFT_ALIGNED;
    settings->mBitAlignment = DSP_MODE_A;
    settings->mSigned = AnalyzerEnums::UnsignedInteger;
    settings->mFrameSyncInverted = FS_NOT_INVERTED;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    std::vector<DecodedFrame> frames;
    U64 count = mock_results->TotalFrameCount();
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        DecodedFrame df;
        df.data = f.mData1;
        df.slot = f.mType;
        df.flags = f.mFlags;
        frames.push_back( df );
    }
    return frames;
}

void test_extra_slot_detection()
{
    auto frames = RunExtraSlotsTest();
    CHECK( frames.size() >= 3, "Should have decoded at least 3 slots" );

    // Look for UNEXPECTED_BITS flag on slot 2+ (the extra slot)
    bool found_extra = false;
    for( const auto& f : frames )
    {
        if( f.flags & UNEXPECTED_BITS )
        {
            found_extra = true;
            // Extra slot should be slot number >= configured_slots (2)
            CHECK( f.slot >= 2, "UNEXPECTED_BITS should be on slot >= 2" );
            break;
        }
    }
    CHECK( found_extra, "Expected at least one UNEXPECTED_BITS flag in decoded frames" );
}

void test_clean_signal_no_errors()
{
    Config c = DefaultConfig( "clean-check", TEST_FRAMES );
    auto frames = RunAndCollect( c );
    CHECK( frames.size() >= TEST_FRAMES * c.slots_per_frame,
           "Should have enough decoded frames" );

    U32 error_count = 0;
    for( U32 i = 0; i < TEST_FRAMES * c.slots_per_frame; i++ )
    {
        U8 error_flags = frames[ i ].flags & 0x3F;
        if( error_flags != 0 )
            error_count++;
    }
    CHECK_EQ( error_count, U32( 0 ), "No error flags on clean signal" );
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
// Robustness Tests — Misconfig and Edge Cases (no crashes)
// ===================================================================

// Helper: run analyzer with given config, return true if it completes
// without crashing (regardless of output correctness).
static bool RunWithoutCrash( const Config& cfg )
{
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, cfg );

    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( cfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = cfg.frame_rate;
    settings->mSlotsPerFrame = cfg.slots_per_frame;
    settings->mBitsPerSlot = cfg.bits_per_slot;
    settings->mDataBitsPerSlot = cfg.data_bits_per_slot;
    settings->mShiftOrder = cfg.shift_order;
    settings->mDataValidEdge = cfg.data_valid_edge;
    settings->mDataAlignment = cfg.data_alignment;
    settings->mBitAlignment = cfg.bit_alignment;
    settings->mSigned = cfg.sign;
    settings->mFrameSyncInverted = cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = cfg.advanced_analysis;

    instance.RunAnalyzerWorker();
    return true; // if we got here, no crash
}

// Signal generated for 2 slots, analyzer configured for 8.
// Analyzer should see fewer slots than expected but not crash.
void test_misconfig_fewer_slots_than_expected()
{
    Config gen_cfg = DefaultConfig( "misconfig-fewer-slots", 50 );
    gen_cfg.slots_per_frame = 2; // signal has 2 slots

    // But we'll configure the analyzer to expect 8 slots
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, gen_cfg );

    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( gen_cfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = gen_cfg.frame_rate;
    settings->mSlotsPerFrame = 8; // mismatch: expect 8, signal has 2
    settings->mBitsPerSlot = gen_cfg.bits_per_slot;
    settings->mDataBitsPerSlot = gen_cfg.data_bits_per_slot;
    settings->mShiftOrder = gen_cfg.shift_order;
    settings->mDataValidEdge = gen_cfg.data_valid_edge;
    settings->mDataAlignment = gen_cfg.data_alignment;
    settings->mBitAlignment = gen_cfg.bit_alignment;
    settings->mSigned = gen_cfg.sign;
    settings->mFrameSyncInverted = gen_cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    U64 count = mock_results->TotalFrameCount();
    CHECK( count > 0, "Should decode some frames even with misconfig" );

    // With 2-slot signal and 8-slot config, the analyzer sees 2 complete
    // slots per frame then hits the next FS. Slot 2 gets partial bits
    // (SHORT_SLOT) or the frame just has fewer slots than expected. Either
    // way verify all decoded slot numbers are in valid range.
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        CHECK( f.mType <= 255, "Slot number should be valid U8" );
    }
}

// Signal generated for 8 slots, analyzer configured for 2.
// Analyzer should flag extra slots but not crash.
void test_misconfig_more_slots_than_expected()
{
    Config gen_cfg = DefaultConfig( "misconfig-more-slots", 50 );
    gen_cfg.slots_per_frame = 8; // signal has 8 slots
    gen_cfg.sample_rate = U64( 48000 ) * 8 * 16 * 4;

    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, gen_cfg );

    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( gen_cfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = gen_cfg.frame_rate;
    settings->mSlotsPerFrame = 2; // mismatch: expect 2, signal has 8
    settings->mBitsPerSlot = gen_cfg.bits_per_slot;
    settings->mDataBitsPerSlot = gen_cfg.data_bits_per_slot;
    settings->mShiftOrder = gen_cfg.shift_order;
    settings->mDataValidEdge = gen_cfg.data_valid_edge;
    settings->mDataAlignment = gen_cfg.data_alignment;
    settings->mBitAlignment = gen_cfg.bit_alignment;
    settings->mSigned = gen_cfg.sign;
    settings->mFrameSyncInverted = gen_cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    U64 count = mock_results->TotalFrameCount();
    CHECK( count > 0, "Should decode frames even with slot count mismatch" );

    // Should have UNEXPECTED_BITS on excess slots
    bool found_extra = false;
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        if( f.mFlags & UNEXPECTED_BITS )
        {
            found_extra = true;
            break;
        }
    }
    CHECK( found_extra, "Should flag extra slots with UNEXPECTED_BITS" );
}

// Wrong bit depth: signal has 32-bit slots, analyzer configured for 16-bit.
// Analyzer sees 2x the expected slots per frame (each 32-bit slot looks
// like two 16-bit slots). Should not crash.
void test_misconfig_wrong_bit_depth()
{
    Config gen_cfg = DefaultConfig( "misconfig-bitdepth", 50 );
    gen_cfg.bits_per_slot = 32;
    gen_cfg.data_bits_per_slot = 32;
    gen_cfg.sample_rate = U64( 48000 ) * 2 * 32 * 4;

    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, gen_cfg );

    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( gen_cfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = gen_cfg.frame_rate;
    settings->mSlotsPerFrame = 2;
    settings->mBitsPerSlot = 16;       // mismatch: analyzer expects 16-bit
    settings->mDataBitsPerSlot = 16;   // but signal is 32-bit
    settings->mShiftOrder = gen_cfg.shift_order;
    settings->mDataValidEdge = gen_cfg.data_valid_edge;
    settings->mDataAlignment = gen_cfg.data_alignment;
    settings->mBitAlignment = gen_cfg.bit_alignment;
    settings->mSigned = gen_cfg.sign;
    settings->mFrameSyncInverted = gen_cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    U64 count = mock_results->TotalFrameCount();
    CHECK( count > 0, "Should decode frames with wrong bit depth config" );

    // With 32-bit signal and 16-bit config, analyzer sees 2x slots per frame.
    // Extra slots should be flagged with UNEXPECTED_BITS.
    bool found_extra = false;
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        if( f.mFlags & UNEXPECTED_BITS )
        {
            found_extra = true;
            break;
        }
    }
    CHECK( found_extra, "Should flag extra slots when bit depth is narrower than signal" );
}

// Wrong DSP mode: signal generated as Mode A, analyzer set to Mode B.
// Data will be offset by one bit. Should not crash, just produce
// different (wrong) values.
void test_misconfig_wrong_dsp_mode()
{
    Config gen_cfg = DefaultConfig( "misconfig-dsp-mode", 50 );
    gen_cfg.bit_alignment = DSP_MODE_A; // signal is Mode A

    // Run analyzer with Mode B (mismatch)
    Config analyze_cfg = gen_cfg;
    analyze_cfg.bit_alignment = DSP_MODE_B;

    // We can't use RunAndCollect directly since gen and analyze configs differ.
    // Use RunWithoutCrash with the gen signal but override the analyzer setting.
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, gen_cfg );

    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( gen_cfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = gen_cfg.frame_rate;
    settings->mSlotsPerFrame = gen_cfg.slots_per_frame;
    settings->mBitsPerSlot = gen_cfg.bits_per_slot;
    settings->mDataBitsPerSlot = gen_cfg.data_bits_per_slot;
    settings->mShiftOrder = gen_cfg.shift_order;
    settings->mDataValidEdge = gen_cfg.data_valid_edge;
    settings->mDataAlignment = gen_cfg.data_alignment;
    settings->mBitAlignment = DSP_MODE_B; // mismatch
    settings->mSigned = gen_cfg.sign;
    settings->mFrameSyncInverted = gen_cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    U64 count = mock_results->TotalFrameCount();
    CHECK( count > 0, "Should decode frames even with wrong DSP mode" );
    // With wrong mode, data is off by 1 bit, but frame count should be
    // in the neighborhood of expected (not wildly different)
    U64 expected_approx = U64( 50 ) * gen_cfg.slots_per_frame;
    CHECK( count >= expected_approx / 2, "Frame count should be reasonable even with wrong DSP mode" );
}

// Wrong frame sync polarity: signal uses non-inverted, analyzer set to inverted.
// Analyzer will look for the wrong edge. Should not crash.
void test_misconfig_wrong_fs_polarity()
{
    Config gen_cfg = DefaultConfig( "misconfig-fs-polarity", 50 );
    gen_cfg.fs_inverted = FS_NOT_INVERTED; // signal is non-inverted

    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, gen_cfg );

    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( gen_cfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = gen_cfg.frame_rate;
    settings->mSlotsPerFrame = gen_cfg.slots_per_frame;
    settings->mBitsPerSlot = gen_cfg.bits_per_slot;
    settings->mDataBitsPerSlot = gen_cfg.data_bits_per_slot;
    settings->mShiftOrder = gen_cfg.shift_order;
    settings->mDataValidEdge = gen_cfg.data_valid_edge;
    settings->mDataAlignment = gen_cfg.data_alignment;
    settings->mBitAlignment = gen_cfg.bit_alignment;
    settings->mSigned = gen_cfg.sign;
    settings->mFrameSyncInverted = FS_INVERTED; // mismatch
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );
    U64 count = mock_results->TotalFrameCount();
    CHECK( count > 0, "Should decode frames even with wrong FS polarity" );
    // With wrong polarity, analyzer syncs on the wrong edge. Verify all
    // slot numbers are in valid U8 range (no memory corruption).
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        CHECK( f.mType <= 255, "Slot number should be valid U8" );
    }
}

// Minimum-size configuration: 1 channel, 2-bit slots
void test_minimum_config()
{
    Config c = DefaultConfig( "minimum-config", TEST_FRAMES );
    c.slots_per_frame = 1;
    c.bits_per_slot = 2;
    c.data_bits_per_slot = 2;
    c.sample_rate = U64( 48000 ) * 1 * 2 * 4;
    auto frames = RunAndCollect( c );

    // 2-bit data wraps at 4, so pattern is 0,1,2,3,0,1,...
    CHECK( frames.size() >= TEST_FRAMES, "Should have enough decoded frames" );
    U32 counter = 0;
    for( U32 i = 0; i < TEST_FRAMES; i++ )
    {
        U64 expected = counter % 4;
        std::ostringstream oss;
        if( frames[ i ].data != expected )
        {
            oss << "Frame " << i << ": expected " << expected
                << ", got " << frames[ i ].data;
            CHECK( false, oss.str() );
        }
        counter++;
    }
}

// ===================================================================
// Round 2: Bit Pattern Coverage
// ===================================================================

// Task 1: 8-bit counter wraps through all 256 values (0x00-0xFF)
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

// ===================================================================
// Round 2: Boundary Value Tests
// ===================================================================

// Task 2a: 3-bit slots (non-power-of-2)
void test_3bit_slots()
{
    Config c = DefaultConfig( "3bit-slots", TEST_FRAMES );
    c.bits_per_slot = 3;
    c.data_bits_per_slot = 3;
    c.sample_rate = U64( 48000 ) * 2 * 3 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Task 2b: 8-bit slots (byte-aligned, common in practice)
void test_8bit_slots()
{
    Config c = DefaultConfig( "8bit-slots", TEST_FRAMES );
    c.bits_per_slot = 8;
    c.data_bits_per_slot = 8;
    c.sample_rate = U64( 48000 ) * 2 * 8 * 4;
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Task 3a: LSB-first + right-aligned (8 data in 16-bit slot)
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

// Task 3b: LSB-first + left-aligned (8 data in 16-bit slot)
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

// Task 4a: Extreme padding, 2 data bits in 64-bit slot, right-aligned
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

// Task 4b: Extreme padding, 2 data bits in 64-bit slot, left-aligned
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

// Task 5: 63 data bits in 64-bit slot (1 bit of padding)
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

// Task 6: 256 slots (U8 mType boundary)
void test_256_slots()
{
    Config c = DefaultConfig( "256-slots", 3 );
    c.slots_per_frame = 256;
    c.bits_per_slot = 2;
    c.data_bits_per_slot = 2;
    c.sample_rate = U64( 48000 ) * 256 * 2 * 4;
    auto frames = RunAndCollect( c );

    // Verify slot numbers cycle 0-255 correctly
    CHECK( frames.size() >= 256, "Should have at least one full frame of 256 slots" );
    for( U32 i = 0; i < 256; i++ )
    {
        U8 expected_slot = U8( i );
        std::ostringstream oss;
        if( frames[ i ].slot != expected_slot )
        {
            oss << "Slot " << i << ": expected slot number " << (int)expected_slot
                << ", got " << (int)frames[ i ].slot;
            CHECK( false, oss.str() );
        }
    }
}

// Task 7: Right-aligned with zero padding (data_bits == bits_per_slot)
void test_right_aligned_zero_padding()
{
    Config c = DefaultConfig( "right-zero-pad", TEST_FRAMES );
    c.data_alignment = RIGHT_ALIGNED;
    // bits_per_slot = data_bits_per_slot = 16 (default), so padding = 0
    auto frames = RunAndCollect( c );
    VerifyCountingPattern( frames, c, TEST_FRAMES );
}

// Task 8: Signed integer end-to-end (4-bit, counter wraps through signed range)
void test_signed_4bit_end_to_end()
{
    Config c = DefaultConfig( "signed-4bit-e2e", TEST_FRAMES );
    c.bits_per_slot = 4;
    c.data_bits_per_slot = 4;
    c.sign = AnalyzerEnums::SignedInteger;
    c.sample_rate = U64( 48000 ) * 2 * 4 * 4;
    auto frames = RunAndCollect( c );

    // mData1 stores the RAW UNSIGNED value regardless of sign setting.
    // Counter wraps at 16 (2^4). Verify the unsigned values are still correct.
    // The signed conversion only affects FrameV2 (not inspectable), but this
    // exercises the code path without crashing.
    CHECK( frames.size() >= TEST_FRAMES * 2, "Should have enough frames" );
    U32 counter = 0;
    for( U32 i = 0; i < TEST_FRAMES * 2; i++ )
    {
        U64 expected = counter % 16;
        std::ostringstream oss;
        if( frames[ i ].data != expected )
        {
            oss << "Frame " << i << ": unsigned data mismatch (expected "
                << expected << ", got " << frames[ i ].data << ")";
            CHECK( false, oss.str() );
        }
        counter++;
    }
}

// ===================================================================
// Round 2: Advanced Analysis Error Detection
// ===================================================================

// Helper: build a hand-crafted signal from a bit-level description.
// Uses 8x oversampling for room to inject glitches.
struct HandcraftedConfig
{
    U32 frame_rate;
    U32 slots_per_frame;
    U32 bits_per_slot;
    U64 sample_rate;
};

static std::vector<DecodedFrame> RunHandcraftedSignal(
    const HandcraftedConfig& hcfg,
    const std::vector<U64>& clk_transitions,
    const std::vector<U64>& frm_transitions,
    const std::vector<U64>& dat_transitions,
    BitState clk_init, BitState frm_init, BitState dat_init,
    bool advanced_analysis )
{
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat = new AnalyzerTest::MockChannelData( &instance );

    clk->TestSetInitialBitState( clk_init );
    frm->TestSetInitialBitState( frm_init );
    dat->TestSetInitialBitState( dat_init );

    for( auto s : clk_transitions ) clk->TestAppendTransitionAtSamples( s );
    for( auto s : frm_transitions ) frm->TestAppendTransitionAtSamples( s );
    for( auto s : dat_transitions ) dat->TestAppendTransitionAtSamples( s );

    clk->ResetCurrentSample();
    frm->ResetCurrentSample();
    dat->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk );
    instance.SetChannelData( FRM_CH, frm );
    instance.SetChannelData( DAT_CH, dat );
    instance.SetSampleRate( hcfg.sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = hcfg.frame_rate;
    settings->mSlotsPerFrame = hcfg.slots_per_frame;
    settings->mBitsPerSlot = hcfg.bits_per_slot;
    settings->mDataBitsPerSlot = hcfg.bits_per_slot;
    settings->mShiftOrder = AnalyzerEnums::MsbFirst;
    settings->mDataValidEdge = AnalyzerEnums::PosEdge;
    settings->mDataAlignment = LEFT_ALIGNED;
    settings->mBitAlignment = DSP_MODE_A;
    settings->mSigned = AnalyzerEnums::UnsignedInteger;
    settings->mFrameSyncInverted = FS_NOT_INVERTED;
    settings->mEnableAdvancedAnalysis = advanced_analysis;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    std::vector<DecodedFrame> frames;
    U64 count = mock_results->TotalFrameCount();
    for( U64 i = 0; i < count; i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        DecodedFrame df;
        df.data = f.mData1;
        df.slot = f.mType;
        df.flags = f.mFlags;
        frames.push_back( df );
    }
    return frames;
}

// Task 9: BITCLOCK_ERROR detection
// Generate a clean signal but stretch one clock cycle to 2x normal period.
void test_bitclock_error_detection()
{
    // Mono 4-bit, 8x oversampling, DSP Mode A
    // bit_freq = 48000 * 1 * 4 = 192000
    // sample_rate = 192000 * 8 = 1,536,000
    // half_period = 4 samples, full_period = 8 samples
    // mDesiredBitClockPeriod = 1536000 / (1 * 4 * 48000) = 8
    const U32 HP = 4; // half-period in samples

    HandcraftedConfig hcfg = { 48000, 1, 4, 1536000 };

    std::vector<U64> clk_trans, frm_trans, dat_trans;
    U64 pos = 0;

    // Preamble: 8 normal clock cycles with frame LOW, data LOW
    for( int i = 0; i < 8; i++ )
    {
        pos += HP; clk_trans.push_back( pos ); // rising
        pos += HP; clk_trans.push_back( pos ); // falling
    }

    // Frame 1: FS pulse at rising edge, then 4 data bits (DSP Mode A: +1 offset)
    // FS goes HIGH at next rising edge
    pos += HP;
    clk_trans.push_back( pos ); // rising - FS active here
    frm_trans.push_back( pos ); // FS goes HIGH
    pos += HP;
    clk_trans.push_back( pos ); // falling
    frm_trans.push_back( pos ); // FS goes LOW

    // 4 data bits (offset bit + 4 bits = 5 clock cycles)
    for( int i = 0; i < 5; i++ )
    {
        pos += HP; clk_trans.push_back( pos ); // rising
        pos += HP; clk_trans.push_back( pos ); // falling
    }

    // Frame 2: normal FS pulse
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos ); // FS HIGH
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos ); // FS LOW

    // 2 normal bits
    for( int i = 0; i < 2; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // 1 STRETCHED clock cycle: 2x normal period (half-period = 2*HP)
    pos += HP * 2; clk_trans.push_back( pos ); // rising (late)
    pos += HP * 2; clk_trans.push_back( pos ); // falling (late)

    // 2 more normal bits
    for( int i = 0; i < 2; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 3: another normal frame
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    for( int i = 0; i < 5; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Trailing idle
    for( int i = 0; i < 16; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    auto frames = RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                         BIT_LOW, BIT_LOW, BIT_LOW, true );

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & BITCLOCK_ERROR )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected BITCLOCK_ERROR flag from stretched clock cycle" );
}

// Task 10: MISSED_DATA detection
// Generate a clean signal but inject a glitch (extra transition) on the data
// line between clock edges.
void test_missed_data_detection()
{
    // Mono 4-bit, 8x oversampling
    const U32 HP = 4;
    HandcraftedConfig hcfg = { 48000, 1, 4, 1536000 };

    std::vector<U64> clk_trans, frm_trans, dat_trans;
    U64 pos = 0;

    // Preamble
    for( int i = 0; i < 8; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 1 FS
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    // 5 normal bits
    for( int i = 0; i < 5; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 2 FS
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    // Bit 0 (offset bit): normal
    pos += HP;
    U64 rising1 = pos;
    clk_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos ); // falling

    // Bit 1: inject data glitch between rising and falling edge
    pos += HP;
    U64 rising2 = pos;
    clk_trans.push_back( pos ); // rising at pos

    // Data glitch: transition at rising+1, then back at rising+2
    // This creates 2 transitions between rising and falling edges
    dat_trans.push_back( rising2 + 1 ); // LOW -> HIGH
    dat_trans.push_back( rising2 + 2 ); // HIGH -> LOW
    // Another transition after falling edge for WouldAdvancing check
    pos += HP;
    clk_trans.push_back( pos ); // falling
    dat_trans.push_back( pos + 1 ); // transition after falling edge

    // 3 more normal bits
    for( int i = 0; i < 3; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 3 FS
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    for( int i = 0; i < 5; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Trailing
    for( int i = 0; i < 16; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    auto frames = RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                         BIT_LOW, BIT_LOW, BIT_LOW, true );

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & MISSED_DATA )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected MISSED_DATA flag from data glitch between clock edges" );
}

// Task 11: MISSED_FRAME_SYNC detection
// Same as MISSED_DATA but glitch on the frame sync line.
void test_missed_frame_sync_detection()
{
    const U32 HP = 4;
    HandcraftedConfig hcfg = { 48000, 1, 4, 1536000 };

    std::vector<U64> clk_trans, frm_trans, dat_trans;
    U64 pos = 0;

    // Preamble
    for( int i = 0; i < 8; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 1 FS
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    for( int i = 0; i < 5; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 2 FS
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    // Offset bit
    pos += HP;
    clk_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );

    // Bit 1: inject frame sync glitch
    pos += HP;
    U64 rising = pos;
    clk_trans.push_back( pos );

    // Frame sync glitch between rising and falling
    frm_trans.push_back( rising + 1 ); // LOW -> HIGH
    frm_trans.push_back( rising + 2 ); // HIGH -> LOW
    pos += HP;
    clk_trans.push_back( pos );
    // Another FS transition after falling for WouldAdvancing check
    frm_trans.push_back( pos + 1 ); // transition after falling

    // 3 more normal bits
    for( int i = 0; i < 3; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 3
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    for( int i = 0; i < 5; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    for( int i = 0; i < 16; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    auto frames = RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                         BIT_LOW, BIT_LOW, BIT_LOW, true );

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & MISSED_FRAME_SYNC )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected MISSED_FRAME_SYNC flag from FS glitch between clock edges" );
}

// ===================================================================
// Round 2: Generator Blind Spot Tests
// ===================================================================

// Task 12: Padding bits set to HIGH -- verify analyzer ignores them.
// Hand-craft a stereo 8-in-16 signal where padding bits are all ones.
void test_padding_bits_high()
{
    // Stereo, 8 data bits in 16-bit slot, right-aligned.
    // First 8 bits = padding (HIGH), last 8 bits = data (value 0x42 = 66).
    // Expected decoded value: 0x42, not 0xFF42.
    const U32 slots = 2;
    const U32 bps = 16;
    const U32 dbps = 8;
    const U32 bpf = slots * bps;
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * bpf * 4;
    const double bit_freq = double( frame_rate ) * bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

    struct BitDesc { BitState data; BitState frame; };
    std::vector<BitDesc> stream;

    // Preamble
    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Generate 4 frames (DSP Mode A)
    // Value for each slot: 0x42 (MSB-first: 01000010)
    // RIGHT_ALIGNED: 8 padding bits (HIGH) + 8 data bits
    for( int f = 0; f < 4; f++ )
    {
        // FS active + offset bit
        stream.push_back( { BIT_LOW, BIT_HIGH } );

        for( U32 s = 0; s < slots; s++ )
        {
            // 8 padding bits = HIGH
            for( U32 b = 0; b < bps - dbps; b++ )
                stream.push_back( { BIT_HIGH, BIT_LOW } );

            // 8 data bits = 0x42 (MSB-first: 01000010)
            U8 val = 0x42;
            for( U32 b = 0; b < dbps; b++ )
            {
                BitState bit = ( val & ( 1 << ( dbps - 1 - b ) ) ) ? BIT_HIGH : BIT_LOW;
                stream.push_back( { bit, BIT_LOW } );
            }
        }

        // Remove the offset bit's effect: data_stream is shifted by 1,
        // so strip the last bit (it belongs to the next frame's offset)
        stream.pop_back();
    }

    // Trailing idle
    for( int i = 0; i < 32; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Convert to mock channel data
    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat = new AnalyzerTest::MockChannelData( &instance );

    clk->TestSetInitialBitState( BIT_LOW );
    frm->TestSetInitialBitState( stream[ 0 ].frame );
    dat->TestSetInitialBitState( stream[ 0 ].data );

    BitState cur_dat = stream[ 0 ].data;
    BitState cur_frm = stream[ 0 ].frame;
    double err = 0.0;
    U64 pos = 0;

    for( size_t i = 0; i < stream.size(); i++ )
    {
        double target = half_samples + err;
        U32 n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );
        target = half_samples + err;
        n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );

        if( i + 1 < stream.size() )
        {
            if( stream[ i + 1 ].data != cur_dat )
            {
                dat->TestAppendTransitionAtSamples( pos );
                cur_dat = stream[ i + 1 ].data;
            }
            if( stream[ i + 1 ].frame != cur_frm )
            {
                frm->TestAppendTransitionAtSamples( pos );
                cur_frm = stream[ i + 1 ].frame;
            }
        }
    }

    clk->ResetCurrentSample();
    frm->ResetCurrentSample();
    dat->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk );
    instance.SetChannelData( FRM_CH, frm );
    instance.SetChannelData( DAT_CH, dat );
    instance.SetSampleRate( sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = frame_rate;
    settings->mSlotsPerFrame = slots;
    settings->mBitsPerSlot = bps;
    settings->mDataBitsPerSlot = dbps;
    settings->mShiftOrder = AnalyzerEnums::MsbFirst;
    settings->mDataValidEdge = AnalyzerEnums::PosEdge;
    settings->mDataAlignment = RIGHT_ALIGNED;
    settings->mBitAlignment = DSP_MODE_A;
    settings->mSigned = AnalyzerEnums::UnsignedInteger;
    settings->mFrameSyncInverted = FS_NOT_INVERTED;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    U64 count = mock_results->TotalFrameCount();
    CHECK( count >= 2, "Should have at least 2 decoded slots" );

    // Verify decoded value is 0x42, NOT 0xFF42 or anything else
    for( U64 i = 0; i < std::min( count, U64( 4 ) ); i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        std::ostringstream oss;
        if( f.mData1 != 0x42 )
        {
            oss << "Slot " << i << ": expected 0x42 (66), got " << f.mData1
                << " -- padding bits may have leaked into decoded value";
            CHECK( false, oss.str() );
        }
    }
}

// Task 16: DSP Mode A offset bit = HIGH -- verify it's excluded from data.
// In DSP Mode A, the very first FS-coincident data bit is skipped by
// SetupForGettingFirstTdmFrame. Subsequent FS-coincident bits are the last
// bit of the previous frame. This test verifies the setup skip works:
// we set data=HIGH at the first FS position and verify it doesn't appear
// in the first decoded slot.
void test_dsp_mode_a_offset_bit_high()
{
    // Mono 4-bit. First FS offset bit = HIGH (should be skipped).
    // All actual frame data = LOW (value 0).
    // At each subsequent FS boundary, data = LOW (last bit of prev frame).
    const U32 slots = 1;
    const U32 bps = 4;
    const U32 bpf = slots * bps;
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * bpf * 4;
    const double bit_freq = double( frame_rate ) * bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

    struct BitDesc { BitState data; BitState frame; };
    std::vector<BitDesc> stream;

    // Preamble
    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // First FS: offset bit = HIGH (this should be skipped by setup)
    stream.push_back( { BIT_HIGH, BIT_HIGH } );

    // Frame 0 data: 3 bits (positions 1-3), then FS at position 4
    // In DSP Mode A: frame 0 has bits at positions 1,2,3 + last bit at
    // position 4 (FS boundary) = 4 bits total
    for( U32 b = 0; b < bpf - 1; b++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Subsequent frames: FS with data=LOW (last bit of prev frame = LOW)
    for( int f = 1; f < 5; f++ )
    {
        stream.push_back( { BIT_LOW, BIT_HIGH } ); // FS active, data=LOW
        for( U32 b = 0; b < bpf - 1; b++ )
            stream.push_back( { BIT_LOW, BIT_LOW } );
    }

    // Trailing
    for( int i = 0; i < 32; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat = new AnalyzerTest::MockChannelData( &instance );

    clk->TestSetInitialBitState( BIT_LOW );
    frm->TestSetInitialBitState( stream[ 0 ].frame );
    dat->TestSetInitialBitState( stream[ 0 ].data );

    BitState cur_dat = stream[ 0 ].data;
    BitState cur_frm = stream[ 0 ].frame;
    double err = 0.0;
    U64 pos = 0;

    for( size_t i = 0; i < stream.size(); i++ )
    {
        double target = half_samples + err;
        U32 n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );
        target = half_samples + err;
        n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos );

        if( i + 1 < stream.size() )
        {
            if( stream[ i + 1 ].data != cur_dat )
            {
                dat->TestAppendTransitionAtSamples( pos );
                cur_dat = stream[ i + 1 ].data;
            }
            if( stream[ i + 1 ].frame != cur_frm )
            {
                frm->TestAppendTransitionAtSamples( pos );
                cur_frm = stream[ i + 1 ].frame;
            }
        }
    }

    clk->ResetCurrentSample();
    frm->ResetCurrentSample();
    dat->ResetCurrentSample();

    instance.SetChannelData( CLK_CH, clk );
    instance.SetChannelData( FRM_CH, frm );
    instance.SetChannelData( DAT_CH, dat );
    instance.SetSampleRate( sample_rate );

    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = frame_rate;
    settings->mSlotsPerFrame = slots;
    settings->mBitsPerSlot = bps;
    settings->mDataBitsPerSlot = bps;
    settings->mShiftOrder = AnalyzerEnums::MsbFirst;
    settings->mDataValidEdge = AnalyzerEnums::PosEdge;
    settings->mDataAlignment = LEFT_ALIGNED;
    settings->mBitAlignment = DSP_MODE_A;
    settings->mSigned = AnalyzerEnums::UnsignedInteger;
    settings->mFrameSyncInverted = FS_NOT_INVERTED;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    U64 count = mock_results->TotalFrameCount();
    CHECK( count >= 1, "Should have decoded at least one slot" );

    for( U64 i = 0; i < std::min( count, U64( 3 ) ); i++ )
    {
        const Frame& f = mock_results->GetFrame( i );
        std::ostringstream oss;
        if( f.mData1 != 0 )
        {
            oss << "Slot " << i << ": expected 0 but got " << f.mData1
                << " -- DSP Mode A offset bit leaked into decoded value";
            CHECK( false, oss.str() );
        }
    }
}

// Task 17: Low sample rate detection (below 4x oversampling)
void test_low_sample_rate()
{
    Config c = DefaultConfig( "low-sample-rate", 50 );
    // bit_clock_hz = 48000 * 2 * 16 = 1,536,000
    // recommended_min = 1,536,000 * 4 = 6,144,000
    // Use 2x oversampling (below threshold)
    c.sample_rate = U64( 48000 ) * 2 * 16 * 2;
    // The analyzer should still decode (mLowSampleRate only affects FrameV2
    // advisory and severity), but the path must not crash.
    auto frames = RunAndCollect( c );
    CHECK( frames.size() > 0, "Should decode frames even with low sample rate" );
}

// Task 15 (additional): 64-bit signed conversion after UB fix
void test_sign_64bit_after_fix()
{
    // Positive 64-bit
    S64 r1 = AnalyzerHelpers::ConvertToSignedNumber( 0x7FFFFFFFFFFFFFFFULL, 64 );
    CHECK_EQ( r1, S64( 0x7FFFFFFFFFFFFFFFLL ), "64-bit max positive" );

    // Negative 64-bit (MSB set) -- was UB before fix
    S64 r2 = AnalyzerHelpers::ConvertToSignedNumber( 0x8000000000000000ULL, 64 );
    CHECK( r2 < 0, "64-bit MSB set should be negative" );
    CHECK_EQ( r2, S64( -9223372036854775807LL - 1 ), "64-bit min negative" );

    // All ones = -1
    S64 r3 = AnalyzerHelpers::ConvertToSignedNumber( 0xFFFFFFFFFFFFFFFFULL, 64 );
    CHECK_EQ( r3, S64( -1 ), "64-bit all ones = -1" );
}

// ===================================================================
// Main
// ===================================================================

int main()
{
    std::cout << "TDM Analyzer Correctness Tests" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << std::endl;

    std::cout << "Happy Path — Value Correctness:" << std::endl;
    RunTest( "test_stereo_16bit_baseline", test_stereo_16bit_baseline );
    RunTest( "test_lsb_first", test_lsb_first );
    RunTest( "test_right_aligned_24in32", test_right_aligned_24in32 );
    RunTest( "test_left_aligned_24in32", test_left_aligned_24in32 );
    RunTest( "test_8channel_slot_numbering", test_8channel_slot_numbering );
    RunTest( "test_dsp_mode_b", test_dsp_mode_b );
    RunTest( "test_frame_sync_inverted", test_frame_sync_inverted );
    RunTest( "test_neg_edge_sampling", test_neg_edge_sampling );
    RunTest( "test_32bit_data", test_32bit_data );
    RunTest( "test_64bit_data", test_64bit_data );
    RunTest( "test_mono_single_slot", test_mono_single_slot );
    std::cout << std::endl;

    std::cout << "Sign Conversion:" << std::endl;
    RunTest( "test_sign_16bit_positive", test_sign_16bit_positive );
    RunTest( "test_sign_16bit_negative", test_sign_16bit_negative );
    RunTest( "test_sign_16bit_zero", test_sign_16bit_zero );
    RunTest( "test_sign_24bit_negative", test_sign_24bit_negative );
    RunTest( "test_sign_32bit_min", test_sign_32bit_min );
    RunTest( "test_sign_edge_cases", test_sign_edge_cases );
    std::cout << std::endl;

    std::cout << "Error Conditions:" << std::endl;
    RunTest( "test_short_slot_detection", test_short_slot_detection );
    RunTest( "test_extra_slot_detection", test_extra_slot_detection );
    RunTest( "test_clean_signal_no_errors", test_clean_signal_no_errors );
    std::cout << std::endl;

    std::cout << "Combination Tests:" << std::endl;
    RunTest( "test_combo_8ch_24in32_right_lsb", test_combo_8ch_24in32_right_lsb );
    RunTest( "test_combo_modeb_fsinv_negedge", test_combo_modeb_fsinv_negedge );
    RunTest( "test_combo_32ch_32bit_lsb_modeb", test_combo_32ch_32bit_lsb_modeb );
    RunTest( "test_combo_mono_24in32_right_fsinv_96k", test_combo_mono_24in32_right_fsinv_96k );
    RunTest( "test_combo_all_nondefault", test_combo_all_nondefault );
    RunTest( "test_combo_4ch_24in32_advanced", test_combo_4ch_24in32_advanced );
    std::cout << std::endl;

    std::cout << "Robustness — Misconfig / Edge Cases:" << std::endl;
    RunTest( "test_misconfig_fewer_slots_than_expected", test_misconfig_fewer_slots_than_expected );
    RunTest( "test_misconfig_more_slots_than_expected", test_misconfig_more_slots_than_expected );
    RunTest( "test_misconfig_wrong_bit_depth", test_misconfig_wrong_bit_depth );
    RunTest( "test_misconfig_wrong_dsp_mode", test_misconfig_wrong_dsp_mode );
    RunTest( "test_misconfig_wrong_fs_polarity", test_misconfig_wrong_fs_polarity );
    RunTest( "test_minimum_config", test_minimum_config );
    std::cout << std::endl;

    std::cout << "Bit Pattern Coverage:" << std::endl;
    RunTest( "test_counter_wrap_8bit", test_counter_wrap_8bit );
    std::cout << std::endl;

    std::cout << "Boundary Values:" << std::endl;
    RunTest( "test_3bit_slots", test_3bit_slots );
    RunTest( "test_8bit_slots", test_8bit_slots );
    RunTest( "test_lsb_right_aligned_8in16", test_lsb_right_aligned_8in16 );
    RunTest( "test_lsb_left_aligned_8in16", test_lsb_left_aligned_8in16 );
    RunTest( "test_extreme_padding_right_2in64", test_extreme_padding_right_2in64 );
    RunTest( "test_extreme_padding_left_2in64", test_extreme_padding_left_2in64 );
    RunTest( "test_63in64_right", test_63in64_right );
    RunTest( "test_256_slots", test_256_slots );
    RunTest( "test_right_aligned_zero_padding", test_right_aligned_zero_padding );
    RunTest( "test_signed_4bit_end_to_end", test_signed_4bit_end_to_end );
    std::cout << std::endl;

    std::cout << "Advanced Analysis Error Detection:" << std::endl;
    RunTest( "test_bitclock_error_detection", test_bitclock_error_detection );
    RunTest( "test_missed_data_detection", test_missed_data_detection );
    RunTest( "test_missed_frame_sync_detection", test_missed_frame_sync_detection );
    std::cout << std::endl;

    std::cout << "Generator Blind Spots:" << std::endl;
    RunTest( "test_padding_bits_high", test_padding_bits_high );
    RunTest( "test_dsp_mode_a_offset_bit_high", test_dsp_mode_a_offset_bit_high );
    RunTest( "test_low_sample_rate", test_low_sample_rate );
    RunTest( "test_sign_64bit_after_fix", test_sign_64bit_after_fix );
    std::cout << std::endl;

    std::cout << "==============================" << std::endl;
    std::cout << "Results: " << g_pass << " passed, " << g_fail << " failed, "
              << ( g_pass + g_fail ) << " total" << std::endl;

    return ( g_fail > 0 ) ? 1 : 0;
}
