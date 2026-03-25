// Implementation of shared test helpers declared in tdm_test_helpers.h.
//
// Moved out of the header to avoid duplicate symbols when multiple
// translation units include the header.

#include "tdm_test_helpers.h"

#include <cmath>

// ---------------------------------------------------------------------------
// RunAndCollect
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunAndCollect( const Config& cfg )
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
// VerifyCountingPattern
// ---------------------------------------------------------------------------

void VerifyCountingPattern( const std::vector<DecodedFrame>& frames,
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

        U8 error_flags = df.flags & ERROR_FLAG_MASK;
        if( error_flags != 0 )
        {
            oss << "Frame " << i << " slot " << (int)df.slot
                << ": unexpected error flags 0x" << std::hex << (int)error_flags;
            CHECK( false, oss.str() );
        }
    }
}

// ---------------------------------------------------------------------------
// RunHandcraftedSignal
//
// NOTE: Hardcodes MSB-first, PosEdge, LEFT_ALIGNED, DSP_MODE_A,
//       UnsignedInteger, FS_NOT_INVERTED.
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunHandcraftedSignal(
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
// EmitBitDescSignal
// ---------------------------------------------------------------------------

void EmitBitDescSignal(
    AnalyzerTest::MockChannelData* clk,
    AnalyzerTest::MockChannelData* frm,
    AnalyzerTest::MockChannelData* dat,
    const std::vector<BitDesc>& stream,
    double half_samples )
{
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
}

// ---------------------------------------------------------------------------
// RunShortSlotTest
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunShortSlotTest()
{
    const U32 slots = 2;
    const U32 bps = 16;
    const U32 bpf = slots * bps;
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * bpf * 4;
    const double bit_freq = double( frame_rate ) * bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

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

    EmitBitDescSignal( clk, frm, dat, stream, half_samples );

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

// ---------------------------------------------------------------------------
// RunExtraSlotsTest
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunExtraSlotsTest()
{
    const U32 configured_slots = 2;
    const U32 actual_slots = 3;
    const U32 bps = 16;
    const U32 actual_bpf = actual_slots * bps;
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * actual_bpf * 4;
    const double bit_freq = double( frame_rate ) * actual_bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

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

    EmitBitDescSignal( clk, frm, dat, stream, half_samples );

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

// ---------------------------------------------------------------------------
// RunBitclockErrorSignal
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunBitclockErrorSignal()
{
    const U32 HP = 4;
    HandcraftedConfig hcfg = { 48000, 1, 4, 1536000 };

    std::vector<U64> clk_trans, frm_trans, dat_trans;
    U64 pos = 0;

    // Preamble: 8 normal clock cycles with frame LOW, data LOW
    for( int i = 0; i < 8; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // Frame 1: FS pulse at rising edge, then 4 data bits (DSP Mode A: +1 offset)
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

    // Frame 2: normal FS pulse
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos );

    // 2 normal bits
    for( int i = 0; i < 2; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // 1 STRETCHED clock cycle: 2x normal period (half-period = 2*HP)
    pos += HP * 2; clk_trans.push_back( pos );
    pos += HP * 2; clk_trans.push_back( pos );

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

    return RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                  BIT_LOW, BIT_LOW, BIT_LOW, true );
}

// ---------------------------------------------------------------------------
// RunMissedDataSignal
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunMissedDataSignal()
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
    clk_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos );

    // Bit 1: inject data glitch between rising and falling edge
    pos += HP;
    U64 rising2 = pos;
    clk_trans.push_back( pos );

    dat_trans.push_back( rising2 + 1 );
    dat_trans.push_back( rising2 + 2 );
    pos += HP;
    clk_trans.push_back( pos );
    dat_trans.push_back( pos + 1 );

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

    return RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                  BIT_LOW, BIT_LOW, BIT_LOW, true );
}

// ---------------------------------------------------------------------------
// RunMissedFrameSyncSignal
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunMissedFrameSyncSignal()
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

    frm_trans.push_back( rising + 1 );
    frm_trans.push_back( rising + 2 );
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos + 1 );

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

    return RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                  BIT_LOW, BIT_LOW, BIT_LOW, true );
}

// ---------------------------------------------------------------------------
// RunWithMismatch
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunWithMismatch(
    const Config& gen_cfg,
    const Config& analyze_cfg )
{
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
    settings->mTdmFrameRate = analyze_cfg.frame_rate;
    settings->mSlotsPerFrame = analyze_cfg.slots_per_frame;
    settings->mBitsPerSlot = analyze_cfg.bits_per_slot;
    settings->mDataBitsPerSlot = analyze_cfg.data_bits_per_slot;
    settings->mShiftOrder = analyze_cfg.shift_order;
    settings->mDataValidEdge = analyze_cfg.data_valid_edge;
    settings->mDataAlignment = analyze_cfg.data_alignment;
    settings->mBitAlignment = analyze_cfg.bit_alignment;
    settings->mSigned = analyze_cfg.sign;
    settings->mFrameSyncInverted = analyze_cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = analyze_cfg.advanced_analysis;

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
