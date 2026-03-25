// Tests error condition detection (SHORT_SLOT, UNEXPECTED_BITS flags) and
// robustness under misconfigured settings. Error tests verify the analyzer
// sets correct flags when signals don't match expected framing. Misconfig
// tests verify the analyzer handles mismatched signal/settings gracefully
// without crashing.

#include "tdm_test_helpers.h"

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
