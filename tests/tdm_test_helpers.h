// Shared test infrastructure for TDM analyzer correctness tests.
//
// Provides:
//   - Minimal test framework (CHECK macros, RunTest, pass/fail counters)
//   - DecodedFrame struct for V1 Frame inspection
//   - RunAndCollect() to run the analyzer and collect decoded frames
//   - VerifyCountingPattern() to verify the counting data pattern
//   - HandcraftedConfig + RunHandcraftedSignal() for hand-crafted signals
//   - RunShortSlotTest() / RunExtraSlotsTest() error signal generators
//   - RunWithoutCrash() for robustness testing

#pragma once

#include <algorithm>
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
#include "framev2_capture.h"

// ---------------------------------------------------------------------------
// Test framework globals (defined in tdm_correctness.cpp)
// ---------------------------------------------------------------------------

extern int g_pass;
extern int g_fail;
extern bool g_test_failed;

// ---------------------------------------------------------------------------
// Assertion macros
// ---------------------------------------------------------------------------

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

inline void RunTest( const char* name, void ( *fn )() )
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
// Decoded frame (V1 Frame fields accessible from MockResults)
// ---------------------------------------------------------------------------

struct DecodedFrame
{
    U64 data;    // mData1: raw unsigned decoded value
    U8 slot;     // mType: slot number
    U8 flags;    // mFlags: error bit flags
};

// ---------------------------------------------------------------------------
// Number of frames per test for config-based tests
// ---------------------------------------------------------------------------

static const U32 TEST_FRAMES = 100;

// ---------------------------------------------------------------------------
// Run the analyzer with a Config and collect all decoded V1 Frames.
// ---------------------------------------------------------------------------

inline std::vector<DecodedFrame> RunAndCollect( const Config& cfg )
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
// Verify counting pattern across all decoded frames.
//
// The signal generator uses a counting pattern: each slot value increments
// sequentially (0, 1, 2, 3, ...) wrapping at 2^data_bits_per_slot.
// The preamble provides synchronization so the analyzer decodes starting
// from the very first generated TDM frame (counter = 0).
// ---------------------------------------------------------------------------

inline void VerifyCountingPattern( const std::vector<DecodedFrame>& frames,
                                    const Config& cfg,
                                    U32 num_verify_frames )
{
    const U64 val_mod = ( cfg.data_bits_per_slot >= 64 ) ? 0 : ( 1ULL << cfg.data_bits_per_slot );
    const U32 total_slots = num_verify_frames * cfg.slots_per_frame;
    U32 counter_start = 0;

    std::ostringstream oss;

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
            expected_data = counter_val;
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

        U8 error_flags = df.flags & 0x3F;
        if( error_flags != 0 )
        {
            oss << "Frame " << i << " slot " << (int)df.slot
                << ": unexpected error flags 0x" << std::hex << (int)error_flags;
            CHECK( false, oss.str() );
        }
    }
}

// ---------------------------------------------------------------------------
// Hand-crafted signal support
// ---------------------------------------------------------------------------

struct HandcraftedConfig
{
    U32 frame_rate;
    U32 slots_per_frame;
    U32 bits_per_slot;
    U64 sample_rate;
};

inline std::vector<DecodedFrame> RunHandcraftedSignal(
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

// ---------------------------------------------------------------------------
// Shared error signal generators (used by error condition and FrameV2 tests)
// ---------------------------------------------------------------------------

// Generate a signal where one frame has fewer bits than expected (SHORT_SLOT).
inline std::vector<DecodedFrame> RunShortSlotTest()
{
    const U32 slots = 2;
    const U32 bps = 16;
    const U32 bpf = slots * bps;
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * bpf * 4;
    const double bit_freq = double( frame_rate ) * bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

    struct BitDesc { BitState data; BitState frame; };
    std::vector<BitDesc> stream;

    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    for( int f = 0; f < 3; f++ )
    {
        stream.push_back( { BIT_LOW, BIT_HIGH } );
        for( U32 b = 0; b < bpf - 1; b++ )
            stream.push_back( { BIT_LOW, BIT_LOW } );
    }

    stream.push_back( { BIT_LOW, BIT_HIGH } );
    for( U32 b = 0; b < 24 - 1; b++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    for( int f = 0; f < 3; f++ )
    {
        stream.push_back( { BIT_LOW, BIT_HIGH } );
        for( U32 b = 0; b < bpf - 1; b++ )
            stream.push_back( { BIT_LOW, BIT_LOW } );
    }

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

// Generate a signal configured for 2 slots but with 3 slots of bits (EXTRA_SLOT).
inline std::vector<DecodedFrame> RunExtraSlotsTest()
{
    const U32 configured_slots = 2;
    const U32 actual_slots = 3;
    const U32 bps = 16;
    const U32 actual_bpf = actual_slots * bps;
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * actual_bpf * 4;
    const double bit_freq = double( frame_rate ) * actual_bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

    struct BitDesc { BitState data; BitState frame; };
    std::vector<BitDesc> stream;

    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    for( int f = 0; f < 5; f++ )
    {
        stream.push_back( { BIT_LOW, BIT_HIGH } );
        for( U32 b = 0; b < actual_bpf - 1; b++ )
            stream.push_back( { BIT_LOW, BIT_LOW } );
    }

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
    settings->mSlotsPerFrame = configured_slots;
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

// Run analyzer with given config, return true if it completes without crashing.
inline bool RunWithoutCrash( const Config& cfg )
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
    return true;
}
