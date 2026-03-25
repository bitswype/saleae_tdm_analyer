// Tests that address blind spots in the standard counting-pattern signal
// generator. The generator always fills padding bits with zeros and the
// DSP Mode A offset bit with zero, which could mask bugs. These tests use
// hand-crafted signals with non-zero padding/offset to verify the analyzer
// correctly ignores them. Also tests low sample rate (below 4x oversampling)
// to exercise the advisory path.

#include "tdm_test_helpers.h"

void test_padding_bits_high()
{
    // Stereo, 8 data bits in 16-bit slot, right-aligned, DSP Mode B.
    // Each slot: 8 padding bits (HIGH) + 8 data bits (value 0x42 MSB-first).
    // Expected decoded value: 0x42, not 0xFF42.
    const U32 slots = 2;
    const U32 bps = 16;
    const U32 dbps = 8;
    const U32 bpf = slots * bps;
    const U32 frame_rate = 48000;
    const U64 sample_rate = U64( frame_rate ) * bpf * 4;
    const double bit_freq = double( frame_rate ) * bpf;
    const double half_samples = double( sample_rate ) / ( 2.0 * bit_freq );

    std::vector<BitDesc> stream;

    // Preamble
    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, BIT_LOW } );

    // Generate 4 frames (DSP Mode B: data and FS aligned, no offset)
    for( int f = 0; f < 4; f++ )
    {
        for( U32 s = 0; s < slots; s++ )
        {
            for( U32 b = 0; b < bps; b++ )
            {
                BitState fs = ( s == 0 && b == 0 ) ? BIT_HIGH : BIT_LOW;
                BitState dat;
                if( b < bps - dbps )
                {
                    dat = BIT_HIGH; // padding = HIGH
                }
                else
                {
                    U32 dbi = b - ( bps - dbps );
                    dat = ( 0x42 & ( 1 << ( dbps - 1 - dbi ) ) ) ? BIT_HIGH : BIT_LOW;
                }
                stream.push_back( { dat, fs } );
            }
        }
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
    settings->mDataBitsPerSlot = dbps;
    settings->mShiftOrder = AnalyzerEnums::MsbFirst;
    settings->mDataValidEdge = AnalyzerEnums::PosEdge;
    settings->mDataAlignment = RIGHT_ALIGNED;
    settings->mBitAlignment = DSP_MODE_B;
    settings->mSigned = AnalyzerEnums::UnsignedInteger;
    settings->mFrameSyncInverted = FS_NOT_INVERTED;
    settings->mEnableAdvancedAnalysis = false;

    instance.RunAnalyzerWorker();

    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );

    U64 count = mock_results->TotalFrameCount();
    CHECK( count >= 2, "Should have at least 2 decoded slots" );

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

// DSP Mode A offset bit = HIGH -- verify it's excluded from data.
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

// Low sample rate detection (below 4x oversampling)
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
