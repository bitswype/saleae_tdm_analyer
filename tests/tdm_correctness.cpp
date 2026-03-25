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

    // Find a frame with SHORT_SLOT flag
    bool found_short = false;
    for( const auto& f : frames )
    {
        if( f.flags & SHORT_SLOT )
        {
            found_short = true;
            break;
        }
    }
    CHECK( found_short, "Expected at least one SHORT_SLOT flag in decoded frames" );
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

    // Should produce frames (short slots) without crashing
    CHECK( mock_results->TotalFrameCount() > 0, "Should decode some frames even with misconfig" );
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

    // Should produce frames (will see extra slots) without crashing
    CHECK( mock_results->TotalFrameCount() > 0, "Should decode frames with wrong bit depth config" );
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

    CHECK( mock_results->TotalFrameCount() > 0,
           "Should decode frames even with wrong DSP mode" );
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

    // Just verifying no crash. With wrong polarity, the analyzer will
    // sync on the wrong edge and produce garbage data, but it should
    // still terminate normally.
    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );
    CHECK( mock_results->TotalFrameCount() > 0,
           "Should decode frames even with wrong FS polarity" );
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

    std::cout << "==============================" << std::endl;
    std::cout << "Results: " << g_pass << " passed, " << g_fail << " failed, "
              << ( g_pass + g_fail ) << " total" << std::endl;

    return ( g_fail > 0 ) ? 1 : 0;
}
