// Tests that verify FrameV2 output fields using a capture mock (see
// framev2_capture.h). Before this infrastructure existed, the FrameV2 layer
// was completely untestable -- stubs were no-ops and all data was silently
// discarded. These tests verify severity strings, error boolean flags, frame
// numbering, signed conversion results, and the low sample rate advisory.
// They kill mutations where FrameV2 fields could be wrong without affecting
// V1 Frame data.

#include "tdm_test_helpers.h"

void test_framev2_happy_path()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "fv2-happy", 10 );
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();
    CHECK( fv2s.size() >= 10, "Should have captured FrameV2 records" );

    // Find "slot" type records (skip any advisory)
    S64 last_frame_num = -1;
    U32 checked = 0;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "slot" )
            continue;

        // severity should be "ok"
        std::string sev = fv2.GetString( "severity" );
        CHECK_EQ( sev, std::string( "ok" ), "Clean slot severity should be 'ok'" );

        // All error booleans should be false
        CHECK_EQ( fv2.GetBoolean( "short_slot" ), false, "short_slot should be false" );
        CHECK_EQ( fv2.GetBoolean( "extra_slot" ), false, "extra_slot should be false" );
        CHECK_EQ( fv2.GetBoolean( "bitclock_error" ), false, "bitclock_error should be false" );
        CHECK_EQ( fv2.GetBoolean( "missed_data" ), false, "missed_data should be false" );
        CHECK_EQ( fv2.GetBoolean( "missed_frame_sync" ), false, "missed_frame_sync should be false" );
        CHECK_EQ( fv2.GetBoolean( "low_sample_rate" ), false, "low_sample_rate should be false" );

        // slot field should match expected cycle
        S64 slot = fv2.GetInteger( "slot" );
        CHECK( slot >= 0 && slot < S64( c.slots_per_frame ),
               "FrameV2 slot should be in valid range" );

        // frame_number should be non-negative and non-decreasing
        S64 fn = fv2.GetInteger( "frame_number" );
        CHECK( fn >= 0, "frame_number should be non-negative" );
        if( slot == 0 && last_frame_num >= 0 )
        {
            CHECK( fn > last_frame_num, "frame_number should increment between frames" );
        }
        if( slot == 0 )
            last_frame_num = fn;

        checked++;
        if( checked >= 10 )
            break;
    }
    CHECK( checked >= 10, "Should have verified at least 10 slot FrameV2 records" );
}

void test_framev2_short_slot_severity()
{
    ClearCapturedFrameV2s();
    auto frames = RunShortSlotTest();

    auto& fv2s = GetCapturedFrameV2s();
    bool found_error = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" && fv2.GetBoolean( "short_slot" ) )
        {
            CHECK_EQ( fv2.GetString( "severity" ), std::string( "error" ),
                       "SHORT_SLOT should have severity 'error'" );
            found_error = true;
            break;
        }
    }
    CHECK( found_error, "Should find a FrameV2 with short_slot=true" );
}

void test_framev2_extra_slot_severity()
{
    ClearCapturedFrameV2s();
    RunExtraSlotsTest();

    auto& fv2s = GetCapturedFrameV2s();
    bool found_warning = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" && fv2.GetBoolean( "extra_slot" ) )
        {
            CHECK_EQ( fv2.GetString( "severity" ), std::string( "warning" ),
                       "EXTRA_SLOT should have severity 'warning'" );
            found_warning = true;
            break;
        }
    }
    CHECK( found_warning, "Should find a FrameV2 with extra_slot=true" );
}

void test_framev2_bitclock_error_severity()
{
    ClearCapturedFrameV2s();
    RunBitclockErrorSignal();

    auto& fv2s = GetCapturedFrameV2s();
    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" && fv2.GetBoolean( "bitclock_error" ) )
        {
            CHECK_EQ( fv2.GetString( "severity" ), std::string( "error" ),
                       "BITCLOCK_ERROR should have severity 'error'" );
            found = true;
            break;
        }
    }
    CHECK( found, "Should find a FrameV2 with bitclock_error=true" );
}

void test_framev2_missed_data_severity()
{
    ClearCapturedFrameV2s();
    RunMissedDataSignal();

    auto& fv2s = GetCapturedFrameV2s();
    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" && fv2.GetBoolean( "missed_data" ) )
        {
            CHECK_EQ( fv2.GetString( "severity" ), std::string( "error" ),
                       "MISSED_DATA should have severity 'error'" );
            found = true;
            break;
        }
    }
    CHECK( found, "Should find a FrameV2 with missed_data=true" );
}

void test_framev2_missed_frame_sync_severity()
{
    ClearCapturedFrameV2s();
    RunMissedFrameSyncSignal();

    auto& fv2s = GetCapturedFrameV2s();
    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" && fv2.GetBoolean( "missed_frame_sync" ) )
        {
            CHECK_EQ( fv2.GetString( "severity" ), std::string( "error" ),
                       "MISSED_FRAME_SYNC should have severity 'error'" );
            found = true;
            break;
        }
    }
    CHECK( found, "Should find a FrameV2 with missed_frame_sync=true" );
}

void test_framev2_low_sample_rate()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "fv2-low-sr", 20 );
    c.sample_rate = U64( 48000 ) * 2 * 16 * 2; // 2x oversampling, below 4x threshold
    RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    // Should have an advisory FrameV2
    bool found_advisory = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "advisory" )
        {
            CHECK_EQ( fv2.GetString( "severity" ), std::string( "warning" ),
                       "Advisory should have severity 'warning'" );
            CHECK( fv2.HasField( "message" ), "Advisory should have a message" );
            found_advisory = true;
            break;
        }
    }
    CHECK( found_advisory, "Should emit advisory FrameV2 for low sample rate" );

    // Slot FrameV2s should have low_sample_rate=true and severity="warning"
    bool found_low_sr_slot = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" )
        {
            CHECK_EQ( fv2.GetBoolean( "low_sample_rate" ), true,
                       "Slot should have low_sample_rate=true" );
            // If no other errors, severity should be "warning" (from mLowSampleRate)
            if( !fv2.GetBoolean( "short_slot" ) && !fv2.GetBoolean( "bitclock_error" ) &&
                !fv2.GetBoolean( "missed_data" ) && !fv2.GetBoolean( "missed_frame_sync" ) )
            {
                CHECK_EQ( fv2.GetString( "severity" ), std::string( "warning" ),
                           "Low SR slot without errors should have severity 'warning'" );
            }
            found_low_sr_slot = true;
            break;
        }
    }
    CHECK( found_low_sr_slot, "Should have slot FrameV2 records with low_sample_rate" );
}

// ---------------------------------------------------------------------------
// One-time "format" FrameV2 (v2.6.0, GitHub issue #10)
//
// Slot FrameV2s carry 'data' already sign-converted at the LLA's data width,
// but nothing told the HLAs what that width was. They assumed it equalled
// their own output width and masked 24-bit samples down to 16 bits. The LLA
// now emits a single "format" FrameV2 at the start of decode, before any
// slot or advisory frame, so HLAs can rescale correctly without a per-frame
// field on the hot path.
// ---------------------------------------------------------------------------

static void CheckFormatFrame( const Config& c, const char* what )
{
    auto& fv2s = GetCapturedFrameV2s();
    CHECK( !fv2s.empty(), std::string( what ) + ": should capture FrameV2 records" );

    // Must be first so an HLA learns the format before it sees any sample
    const CapturedFrameV2& first = fv2s[ 0 ];
    CHECK_EQ( first.type, std::string( "format" ),
              std::string( what ) + ": first FrameV2 should be the format frame" );
    CHECK_EQ( first.GetInteger( "bit_depth" ), S64( c.data_bits_per_slot ),
              std::string( what ) + ": format bit_depth" );
    CHECK_EQ( first.GetInteger( "slots_per_frame" ), S64( c.slots_per_frame ),
              std::string( what ) + ": format slots_per_frame" );
    CHECK_EQ( first.GetInteger( "sample_rate" ), S64( c.frame_rate ),
              std::string( what ) + ": format sample_rate" );
    CHECK_EQ( first.GetBoolean( "signed" ), c.sign == AnalyzerEnums::SignedInteger,
              std::string( what ) + ": format signed" );
    CHECK_EQ( first.starting_sample, U64( 0 ), std::string( what ) + ": format frame at sample 0" );

    // Exactly one, and never confused with a slot frame
    U32 count = 0;
    for( const auto& fv2 : fv2s )
        if( fv2.type == "format" )
            count++;
    CHECK_EQ( count, U32( 1 ), std::string( what ) + ": exactly one format frame" );
}

void test_framev2_format_frame()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "fv2-format", 5 );
    c.slots_per_frame = 4;
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 24;
    c.frame_rate = 44100;
    c.sign = AnalyzerEnums::SignedInteger;
    RunAndCollect( c );
    CheckFormatFrame( c, "signed 24-in-32" );

    ClearCapturedFrameV2s();
    Config u = DefaultConfig( "fv2-format-unsigned", 5 );
    u.sign = AnalyzerEnums::UnsignedInteger;
    RunAndCollect( u );
    CheckFormatFrame( u, "unsigned 16-bit" );

    // FV2_OFF suppresses slot frames but the format frame is still emitted
    // (alongside the "HLA output disabled" advisory) so an HLA attached to
    // this analyzer can at least report the format it will never receive
    ClearCapturedFrameV2s();
    Config off = DefaultConfig( "fv2-format-off", 5 );
    off.framev2_detail = FV2_OFF;
    off.data_bits_per_slot = 24;
    off.bits_per_slot = 32;
    RunAndCollect( off );
    CheckFormatFrame( off, "FrameV2 output off" );
}

void test_framev2_format_frame_batch_mode()
{
    // Batch mode replaces slot FrameV2s with audio_batch frames, but HLAs
    // still need the format frame (and the Minimal tier must carry it too).
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "fv2-format-batch", 8 );
    c.data_bits_per_slot = 24;
    c.bits_per_slot = 32;
    c.audio_batch_size = 4;
    c.framev2_detail = FV2_MINIMAL;
    c.sign = AnalyzerEnums::SignedInteger;
    RunAndCollect( c );
    CheckFormatFrame( c, "batch mode, minimal detail" );

    // Prove batch mode was actually engaged, otherwise this test would pass
    // for any detail level since the format frame is unconditional
    U32 batch_count = 0, slot_count = 0;
    for( const auto& fv2 : GetCapturedFrameV2s() )
    {
        if( fv2.type == "audio_batch" ) batch_count++;
        if( fv2.type == "slot" ) slot_count++;
    }
    CHECK( batch_count >= 1, "batch mode should emit audio_batch frames after the format frame" );
    CHECK_EQ( slot_count, U32( 0 ), "batch mode should emit no slot frames" );
}
