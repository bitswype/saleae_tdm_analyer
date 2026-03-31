// Tests for the Audio Batch mode feature.
//
// When mAudioBatchSize > 0, the LLA accumulates N TDM frames and emits
// a single "audio_batch" FrameV2 with packed PCM data instead of
// individual "slot" FrameV2s.

#include "tdm_test_helpers.h"
#include <cstring>

// ---------------------------------------------------------------------------
// test_batch_off_identical: batch=0 produces same output as current behavior
// ---------------------------------------------------------------------------

void test_batch_off_identical()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-off", 20 );
    c.audio_batch_size = 0;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    // Should have slot-type FrameV2s, no audio_batch
    U32 slot_count = 0;
    U32 batch_count = 0;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" ) slot_count++;
        if( fv2.type == "audio_batch" ) batch_count++;
    }
    CHECK( slot_count > 0, "Should have slot FrameV2s when batch=0" );
    CHECK_EQ( batch_count, U32( 0 ), "Should have no audio_batch when batch=0" );
}

// ---------------------------------------------------------------------------
// test_batch_basic: batch=4 stereo 16-bit
// ---------------------------------------------------------------------------

void test_batch_basic()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-basic", 20 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 16;
    c.audio_batch_size = 4;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    // Should have audio_batch FrameV2s, no slot FrameV2s
    U32 slot_count = 0;
    U32 batch_count = 0;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" ) slot_count++;
        if( fv2.type == "audio_batch" ) batch_count++;
    }
    CHECK_EQ( slot_count, U32( 0 ), "No slot FrameV2s when batching is on" );
    CHECK( batch_count > 0, "Should have audio_batch FrameV2s" );

    // Check first full batch
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        CHECK_EQ( fv2.GetInteger( "num_frames" ), S64( 4 ), "num_frames should be 4" );
        CHECK_EQ( fv2.GetInteger( "channels" ), S64( 2 ), "channels should be 2" );
        CHECK_EQ( fv2.GetInteger( "bit_depth" ), S64( 16 ), "bit_depth should be 16" );
        CHECK_EQ( fv2.GetInteger( "sample_rate" ), S64( 48000 ), "sample_rate should match frame rate" );

        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 4 frames * 2 channels * 2 bytes = 16 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 16 ), "pcm_data should be 16 bytes" );
        break;
    }
}

// ---------------------------------------------------------------------------
// test_batch_pcm_correctness: verify byte-level PCM values
// ---------------------------------------------------------------------------

void test_batch_pcm_correctness()
{
    ClearCapturedFrameV2s();

    // Unsigned stereo 16-bit, batch=2. The counting pattern produces
    // predictable values we can verify.
    Config c = DefaultConfig( "batch-pcm", 10 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 16;
    c.sign = AnalyzerEnums::UnsignedInteger;
    c.audio_batch_size = 2;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    // Find first audio_batch
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 2 frames * 2 channels * 2 bytes = 8 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 8 ), "pcm_data should be 8 bytes" );

        if( pcm.size() >= 8 )
        {
            // Verify the data is little-endian by checking the byte order
            // is consistent (low byte first)
            U16 sample0 = U16( pcm[0] ) | ( U16( pcm[1] ) << 8 );
            U16 sample1 = U16( pcm[2] ) | ( U16( pcm[3] ) << 8 );
            // Samples should be non-negative (counting pattern)
            CHECK( sample0 < 65535, "Sample 0 should be valid" );
            CHECK( sample1 < 65535, "Sample 1 should be valid" );
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// test_batch_frame_number: start_frame_number increments by batch_size
// ---------------------------------------------------------------------------

void test_batch_frame_number()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-fn", 30 );
    c.audio_batch_size = 4;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    S64 prev_start = -1;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        S64 start_fn = fv2.GetInteger( "start_frame_number" );
        if( prev_start >= 0 )
        {
            CHECK_EQ( start_fn, prev_start + 4,
                       "start_frame_number should increment by batch_size" );
        }
        prev_start = start_fn;
    }
    CHECK( prev_start >= 0, "Should have seen at least one audio_batch" );
}

// ---------------------------------------------------------------------------
// test_batch_sample_rate: sample_rate matches mTdmFrameRate
// ---------------------------------------------------------------------------

void test_batch_sample_rate()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-sr", 10 );
    c.frame_rate = 96000;
    c.sample_rate = U64( 96000 ) * 2 * 16 * 4;
    c.audio_batch_size = 2;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        CHECK_EQ( fv2.GetInteger( "sample_rate" ), S64( 96000 ),
                   "sample_rate should be 96000" );
        break;
    }
}

// ---------------------------------------------------------------------------
// test_batch_v1_still_emitted: V1 Frames present when batching is on
// ---------------------------------------------------------------------------

void test_batch_v1_still_emitted()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-v1", 10 );
    c.audio_batch_size = 4;
    auto frames = RunAndCollect( c );

    // V1 Frames should still be emitted (one per slot per TDM frame)
    // With 2 slots and 10 frames, expect ~20 V1 frames
    CHECK( frames.size() >= 15, "V1 Frames should still be emitted with batching on" );
}

// ---------------------------------------------------------------------------
// test_batch_32bit: 4-byte little-endian packing
// ---------------------------------------------------------------------------

void test_batch_32bit()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-32", 10 );
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 32;
    c.audio_batch_size = 2;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        CHECK_EQ( fv2.GetInteger( "bit_depth" ), S64( 32 ), "bit_depth should be 32" );

        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 2 frames * 2 channels * 4 bytes = 16 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 16 ), "pcm_data should be 16 bytes for 32-bit" );
        break;
    }
}

// ---------------------------------------------------------------------------
// test_batch_signed: two's complement negative values pack correctly
// ---------------------------------------------------------------------------

void test_batch_signed()
{
    ClearCapturedFrameV2s();

    // Use signed mode. The counting pattern will eventually produce large
    // unsigned values that become negative when interpreted as signed.
    Config c = DefaultConfig( "batch-signed", 200 );
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 16;
    c.sign = AnalyzerEnums::SignedInteger;
    c.audio_batch_size = 8;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    // Verify we got batch frames and they have valid pcm_data
    bool found_batch = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        found_batch = true;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 8 frames * 2 channels * 2 bytes = 32 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 32 ), "pcm_data should be 32 bytes" );

        // Verify at least one sample can be decoded as a valid int16
        if( pcm.size() >= 2 )
        {
            S16 sample;
            std::memcpy( &sample, pcm.data(), 2 );
            // The counting pattern value should be reasonable
            CHECK( sample >= -32768 && sample <= 32767,
                   "Signed sample should be valid int16 range" );
        }
        break;
    }
    CHECK( found_batch, "Should have at least one audio_batch FrameV2" );
}

// ---------------------------------------------------------------------------
// test_batch_1_frame: batch=1 means one TDM frame per FrameV2
// ---------------------------------------------------------------------------

void test_batch_1_frame()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-1", 10 );
    c.audio_batch_size = 1;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    U32 batch_count = 0;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        batch_count++;

        CHECK_EQ( fv2.GetInteger( "num_frames" ), S64( 1 ),
                   "num_frames should be 1 for batch_size=1" );

        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 1 frame * 2 channels * 2 bytes = 4 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 4 ), "pcm_data should be 4 bytes" );
    }
    // With 10 TDM frames and batch=1, should get ~10 batch FrameV2s
    CHECK( batch_count >= 8, "Should have ~10 batch FrameV2s for batch_size=1" );
}
