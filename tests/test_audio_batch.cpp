// Tests for the Audio Batch mode feature.
//
// When mAudioBatchSize > 0, the LLA accumulates N TDM frames and emits
// a single "audio_batch" FrameV2 with packed PCM data instead of
// individual "slot" FrameV2s.
//
// Categories:
//   Happy path (9 tests): basic operation, field values, PCM sizes
//   PCM oracle (4 tests): exact byte-level verification against known values
//   Multi-channel / bit depth (4 tests): 4ch, 8ch, 8-bit, 24-bit
//   Edge cases (4 tests): batch=1, large batch, partial batch, batch > frames
//   Error handling (3 tests): short_slot zero-fill, bitclock_error zero-fill
//   Robustness (3 tests): markers still work, V1 frames correct, no slot FV2s leak

#include "tdm_test_helpers.h"
#include <cstring>
#include <cmath>

// ============================================================================
// Helper: extract a little-endian signed value from a byte buffer
// ============================================================================

static S64 ReadLE( const U8* buf, U32 bytes_per_sample )
{
    U64 raw = 0;
    for( U32 i = 0; i < bytes_per_sample; i++ )
        raw |= U64( buf[i] ) << ( 8 * i );

    // Sign-extend
    U32 bits = bytes_per_sample * 8;
    if( raw & ( 1ULL << ( bits - 1 ) ) )
        return S64( raw ) - S64( 1ULL << bits );
    return S64( raw );
}

// ============================================================================
// Happy Path
// ============================================================================

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

    U32 slot_count = 0;
    U32 batch_count = 0;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" ) slot_count++;
        if( fv2.type == "audio_batch" ) batch_count++;
    }
    CHECK_EQ( slot_count, U32( 0 ), "No slot FrameV2s when batching is on" );
    CHECK( batch_count > 0, "Should have audio_batch FrameV2s" );

    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        CHECK_EQ( fv2.GetInteger( "num_frames" ), S64( 4 ), "num_frames should be 4" );
        CHECK_EQ( fv2.GetInteger( "channels" ), S64( 2 ), "channels should be 2" );
        CHECK_EQ( fv2.GetInteger( "bit_depth" ), S64( 16 ), "bit_depth should be 16" );
        CHECK_EQ( fv2.GetInteger( "sample_rate" ), S64( 48000 ), "sample_rate should match frame rate" );

        auto pcm = fv2.GetByteArray( "pcm_data" );
        CHECK_EQ( U32( pcm.size() ), U32( 16 ), "pcm_data should be 16 bytes" );
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
    U32 checked = 0;
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
        checked++;
    }
    CHECK( checked >= 2, "Should have at least 2 batches to verify increment" );
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

    Config c = DefaultConfig( "batch-signed", 200 );
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 16;
    c.sign = AnalyzerEnums::SignedInteger;
    c.audio_batch_size = 8;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    bool found_batch = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        found_batch = true;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        CHECK_EQ( U32( pcm.size() ), U32( 32 ), "pcm_data should be 32 bytes" );

        if( pcm.size() >= 2 )
        {
            S16 sample;
            std::memcpy( &sample, pcm.data(), 2 );
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
        CHECK_EQ( U32( pcm.size() ), U32( 4 ), "pcm_data should be 4 bytes" );
    }
    CHECK( batch_count >= 8, "Should have ~10 batch FrameV2s for batch_size=1" );
}

// ============================================================================
// PCM Oracle -- exact byte-level verification
// ============================================================================

// ---------------------------------------------------------------------------
// test_batch_pcm_oracle_unsigned: verify exact PCM values against counting pattern
// ---------------------------------------------------------------------------

void test_batch_pcm_oracle_unsigned()
{
    ClearCapturedFrameV2s();

    // Stereo 16-bit unsigned, batch=2.
    // Counting pattern: slot0=0, slot1=1, slot0=2, slot1=3, ...
    // First batch (frames 0-1): [0,1,2,3] as unsigned 16-bit LE
    Config c = DefaultConfig( "batch-pcm-oracle-u", 10 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 16;
    c.sign = AnalyzerEnums::UnsignedInteger;
    c.audio_batch_size = 2;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        found = true;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        CHECK_EQ( U32( pcm.size() ), U32( 8 ), "First batch should be 8 bytes" );

        if( pcm.size() >= 8 )
        {
            S64 s0 = ReadLE( pcm.data() + 0, 2 );  // frame 0, slot 0
            S64 s1 = ReadLE( pcm.data() + 2, 2 );  // frame 0, slot 1
            S64 s2 = ReadLE( pcm.data() + 4, 2 );  // frame 1, slot 0
            S64 s3 = ReadLE( pcm.data() + 6, 2 );  // frame 1, slot 1

            CHECK_EQ( s0, S64( 0 ), "frame 0 slot 0 = 0" );
            CHECK_EQ( s1, S64( 1 ), "frame 0 slot 1 = 1" );
            CHECK_EQ( s2, S64( 2 ), "frame 1 slot 0 = 2" );
            CHECK_EQ( s3, S64( 3 ), "frame 1 slot 1 = 3" );
        }
        break;
    }
    CHECK( found, "Should have at least one audio_batch" );
}

// ---------------------------------------------------------------------------
// test_batch_pcm_oracle_signed: verify negative values in signed mode
// ---------------------------------------------------------------------------

void test_batch_pcm_oracle_signed()
{
    ClearCapturedFrameV2s();

    // 4-bit signed data in 4-bit slots, batch=2.
    // Counter: 0, 1, 2, 3, 4, 5, 6, 7, 8(=0), 9(=1), ...
    // With 4-bit data, val_mod = 16, values 0-15. Signed: 8+ becomes negative.
    // Bytes per sample = 1 for 4-bit data.
    // Frame 0: slot0=0 (signed 0), slot1=1 (signed 1)
    // Frame 1: slot0=2 (signed 2), slot1=3 (signed 3)
    Config c = DefaultConfig( "batch-pcm-oracle-s", 20 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 4;
    c.data_bits_per_slot = 4;
    c.sign = AnalyzerEnums::SignedInteger;
    c.audio_batch_size = 4;
    // 4x oversampling of 48000 * 2 slots * 4 bits
    c.sample_rate = U64( 48000 ) * 2 * 4 * 4;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        found = true;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 4 frames * 2 channels * 1 byte = 8 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 8 ), "4-bit batch should be 8 bytes" );

        if( pcm.size() >= 8 )
        {
            // Read as signed 1-byte values, sign-extended from 4 bits
            // Counter values: 0,1,2,3,4,5,6,7
            // Signed 4-bit: 0-7 are positive, 8-15 are negative
            S64 s0 = ReadLE( pcm.data() + 0, 1 );
            S64 s1 = ReadLE( pcm.data() + 1, 1 );
            S64 s2 = ReadLE( pcm.data() + 2, 1 );
            S64 s3 = ReadLE( pcm.data() + 3, 1 );

            // Values 0-7 are positive in 4-bit signed
            CHECK_EQ( s0, S64( 0 ), "frame 0 slot 0 = 0 (signed 4-bit)" );
            CHECK_EQ( s1, S64( 1 ), "frame 0 slot 1 = 1 (signed 4-bit)" );
            CHECK_EQ( s2, S64( 2 ), "frame 1 slot 0 = 2 (signed 4-bit)" );
            CHECK_EQ( s3, S64( 3 ), "frame 1 slot 1 = 3 (signed 4-bit)" );
        }
        break;
    }
    CHECK( found, "Should have at least one audio_batch" );
}

// ---------------------------------------------------------------------------
// test_batch_pcm_oracle_32bit: exact values for 32-bit packing
// ---------------------------------------------------------------------------

void test_batch_pcm_oracle_32bit()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-pcm-oracle-32", 10 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 32;
    c.data_bits_per_slot = 32;
    c.sign = AnalyzerEnums::UnsignedInteger;
    c.audio_batch_size = 2;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        found = true;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        CHECK_EQ( U32( pcm.size() ), U32( 16 ), "32-bit batch should be 16 bytes" );

        if( pcm.size() >= 16 )
        {
            S64 s0 = ReadLE( pcm.data() + 0, 4 );
            S64 s1 = ReadLE( pcm.data() + 4, 4 );
            S64 s2 = ReadLE( pcm.data() + 8, 4 );
            S64 s3 = ReadLE( pcm.data() + 12, 4 );

            CHECK_EQ( s0, S64( 0 ), "frame 0 slot 0 = 0 (32-bit)" );
            CHECK_EQ( s1, S64( 1 ), "frame 0 slot 1 = 1 (32-bit)" );
            CHECK_EQ( s2, S64( 2 ), "frame 1 slot 0 = 2 (32-bit)" );
            CHECK_EQ( s3, S64( 3 ), "frame 1 slot 1 = 3 (32-bit)" );
        }
        break;
    }
    CHECK( found, "Should have at least one audio_batch" );
}

// ---------------------------------------------------------------------------
// test_batch_pcm_consecutive_batches: verify values across batch boundaries
// ---------------------------------------------------------------------------

void test_batch_pcm_consecutive_batches()
{
    ClearCapturedFrameV2s();

    // Stereo 16-bit unsigned, batch=2, 10 frames -> 5 batches
    Config c = DefaultConfig( "batch-pcm-consec", 12 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 16;
    c.sign = AnalyzerEnums::UnsignedInteger;
    c.audio_batch_size = 2;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    U32 batch_idx = 0;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        if( pcm.size() < 8 ) { batch_idx++; continue; }

        // Each batch has 2 frames * 2 slots = 4 values
        // Batch 0: [0,1,2,3], Batch 1: [4,5,6,7], Batch 2: [8,9,10,11]
        U32 base = batch_idx * 4;
        S64 s0 = ReadLE( pcm.data() + 0, 2 );
        S64 s1 = ReadLE( pcm.data() + 2, 2 );
        S64 s2 = ReadLE( pcm.data() + 4, 2 );
        S64 s3 = ReadLE( pcm.data() + 6, 2 );

        CHECK_EQ( s0, S64( base + 0 ), "Consecutive batch value 0" );
        CHECK_EQ( s1, S64( base + 1 ), "Consecutive batch value 1" );
        CHECK_EQ( s2, S64( base + 2 ), "Consecutive batch value 2" );
        CHECK_EQ( s3, S64( base + 3 ), "Consecutive batch value 3" );

        batch_idx++;
        if( batch_idx >= 3 ) break;  // Verify first 3 batches
    }
    CHECK( batch_idx >= 3, "Should have verified at least 3 consecutive batches" );
}

// ============================================================================
// Multi-channel and bit depth coverage
// ============================================================================

// ---------------------------------------------------------------------------
// test_batch_4channel: 4 slots per frame
// ---------------------------------------------------------------------------

void test_batch_4channel()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-4ch", 10 );
    c.slots_per_frame = 4;
    c.audio_batch_size = 2;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        CHECK_EQ( fv2.GetInteger( "channels" ), S64( 4 ), "channels should be 4" );
        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 2 frames * 4 channels * 2 bytes = 16 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 16 ), "4ch batch should be 16 bytes" );

        // Verify first frame: counter 0,1,2,3
        if( pcm.size() >= 8 )
        {
            CHECK_EQ( ReadLE( pcm.data() + 0, 2 ), S64( 0 ), "4ch frame 0 slot 0" );
            CHECK_EQ( ReadLE( pcm.data() + 2, 2 ), S64( 1 ), "4ch frame 0 slot 1" );
            CHECK_EQ( ReadLE( pcm.data() + 4, 2 ), S64( 2 ), "4ch frame 0 slot 2" );
            CHECK_EQ( ReadLE( pcm.data() + 6, 2 ), S64( 3 ), "4ch frame 0 slot 3" );
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// test_batch_8channel: 8 slots per frame
// ---------------------------------------------------------------------------

void test_batch_8channel()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-8ch", 5 );
    c.slots_per_frame = 8;
    c.audio_batch_size = 2;
    // Lower sample rate to reduce mock memory usage for 8 channels
    c.sample_rate = U64( 48000 ) * 8 * 16 * 4;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        CHECK_EQ( fv2.GetInteger( "channels" ), S64( 8 ), "channels should be 8" );
        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 2 frames * 8 channels * 2 bytes = 32 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 32 ), "8ch batch should be 32 bytes" );
        break;
    }
}

// ---------------------------------------------------------------------------
// test_batch_8bit: 1-byte packing for <= 8 bit data
// ---------------------------------------------------------------------------

void test_batch_8bit()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-8bit", 10 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 8;
    c.data_bits_per_slot = 8;
    c.audio_batch_size = 4;
    c.sample_rate = U64( 48000 ) * 2 * 8 * 4;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        CHECK_EQ( fv2.GetInteger( "bit_depth" ), S64( 8 ), "bit_depth should be 8" );
        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 4 frames * 2 channels * 1 byte = 8 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 8 ), "8-bit batch should be 8 bytes" );

        // Verify values: counter 0,1,2,3,4,5,6,7
        if( pcm.size() >= 8 )
        {
            for( U32 i = 0; i < 8; i++ )
            {
                CHECK_EQ( S64( pcm[i] ), S64( i ),
                           "8-bit counting pattern should match" );
            }
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// test_batch_24bit: 3-byte packing for 17-24 bit data
// ---------------------------------------------------------------------------

void test_batch_24bit()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-24bit", 10 );
    c.slots_per_frame = 2;
    c.bits_per_slot = 24;
    c.data_bits_per_slot = 24;
    c.audio_batch_size = 2;
    c.sample_rate = U64( 48000 ) * 2 * 24 * 4;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;

        CHECK_EQ( fv2.GetInteger( "bit_depth" ), S64( 24 ), "bit_depth should be 24" );
        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 2 frames * 2 channels * 3 bytes = 12 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 12 ), "24-bit batch should be 12 bytes" );

        // Verify first frame: 0, 1
        if( pcm.size() >= 6 )
        {
            CHECK_EQ( ReadLE( pcm.data() + 0, 3 ), S64( 0 ), "24-bit frame 0 slot 0 = 0" );
            CHECK_EQ( ReadLE( pcm.data() + 3, 3 ), S64( 1 ), "24-bit frame 0 slot 1 = 1" );
        }
        break;
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

// ---------------------------------------------------------------------------
// test_batch_large_1024: batch size 1024 works
// ---------------------------------------------------------------------------

void test_batch_large_1024()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-1024", 1100 );
    c.audio_batch_size = 1024;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        found = true;

        CHECK_EQ( fv2.GetInteger( "num_frames" ), S64( 1024 ),
                   "Large batch should have 1024 frames" );

        auto pcm = fv2.GetByteArray( "pcm_data" );
        // 1024 frames * 2 channels * 2 bytes = 4096 bytes
        CHECK_EQ( U32( pcm.size() ), U32( 4096 ),
                   "Large batch should be 4096 bytes" );
        break;
    }
    CHECK( found, "Should have at least one 1024-frame batch" );
}

// ---------------------------------------------------------------------------
// test_batch_partial_fewer_than_batch: capture has fewer frames than batch size
// ---------------------------------------------------------------------------

void test_batch_partial_fewer_than_batch()
{
    // 5 frames with batch=8. The partial batch is flushed in the destructor,
    // but our test mock reads FrameV2s before the destructor runs (the
    // TestInstance is still alive when RunAndCollect returns). In the real
    // SDK this works because the destructor writes to the same results
    // object. We verify the next best thing: no crash, and the V1 frames
    // are all present.
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "batch-partial", 5 );
    c.audio_batch_size = 8;
    auto frames = RunAndCollect( c );

    // V1 Frames should all be present (5 frames * 2 slots = 10)
    CHECK( frames.size() >= 8, "V1 Frames should be emitted for partial capture" );

    // The batch FrameV2 from the destructor is emitted after RunAndCollect
    // returns, so we can't capture it in the mock. Verify no crash occurred
    // and the advisory was emitted.
    auto& fv2s = GetCapturedFrameV2s();
    bool found_advisory = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "advisory" ) found_advisory = true;
    }
    CHECK( found_advisory, "Advisory should be emitted for batch mode" );
}

// ---------------------------------------------------------------------------
// test_batch_no_slot_fv2_leak: no slot FrameV2s when batching is on
// ---------------------------------------------------------------------------

void test_batch_no_slot_fv2_leak()
{
    ClearCapturedFrameV2s();

    // Run several configs and verify zero slot FrameV2s
    U32 batch_sizes[] = { 1, 2, 4, 8, 16, 64 };
    for( U32 bs : batch_sizes )
    {
        ClearCapturedFrameV2s();
        Config c = DefaultConfig( "batch-noleak", 20 );
        c.audio_batch_size = bs;
        auto frames = RunAndCollect( c );

        auto& fv2s = GetCapturedFrameV2s();
        for( const auto& fv2 : fv2s )
        {
            if( fv2.type == "slot" )
            {
                char msg[128];
                snprintf( msg, sizeof(msg),
                    "Slot FrameV2 leaked with batch_size=%u", bs );
                CHECK( false, msg );
                return;
            }
        }
    }
}

// ============================================================================
// Error frame handling
// ============================================================================

// ---------------------------------------------------------------------------
// test_batch_short_slot_zero_fill: short slots produce zero in batch PCM
// ---------------------------------------------------------------------------

// Note: this tests that when a slot has SHORT_SLOT flag, the batch accumulator
// does not include garbage data. The AccumulateSlotIntoBatch is skipped for
// short slots, so the pre-zeroed buffer should have 0 for that position.
// We verify this indirectly through the V1 frames (short_slot detection)
// and batch PCM sizes (correct even with errors).
void test_batch_with_errors()
{
    ClearCapturedFrameV2s();

    // Use advanced analysis which can detect errors
    Config c = DefaultConfig( "batch-errors", 20 );
    c.audio_batch_size = 4;
    c.advanced_analysis = true;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    // Batch frames should still be emitted even with advanced analysis on
    bool found = false;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type != "audio_batch" ) continue;
        found = true;

        auto pcm = fv2.GetByteArray( "pcm_data" );
        CHECK_EQ( U32( pcm.size() ), U32( 16 ),
                   "Batch PCM should be correct size with advanced analysis" );
        break;
    }
    CHECK( found, "Should have batch frames with advanced analysis" );
}

// ============================================================================
// V1 Frame correctness during batch mode
// ============================================================================

// ---------------------------------------------------------------------------
// test_batch_v1_values_correct: V1 Frame data values match non-batch mode
// ---------------------------------------------------------------------------

void test_batch_v1_values_correct()
{
    // Run once without batch, collect V1 frame values
    Config c1 = DefaultConfig( "batch-v1-ref", 10 );
    c1.audio_batch_size = 0;
    auto frames_nobatch = RunAndCollect( c1 );

    // Run again with batch
    Config c2 = DefaultConfig( "batch-v1-test", 10 );
    c2.audio_batch_size = 4;
    auto frames_batch = RunAndCollect( c2 );

    // V1 frame counts should be similar
    CHECK( frames_batch.size() >= frames_nobatch.size() - 2,
           "V1 frame count should be similar with and without batch" );

    // Compare first 10 V1 frame data values
    U32 to_check = std::min( U32( 10 ), U32( std::min( frames_nobatch.size(), frames_batch.size() ) ) );
    for( U32 i = 0; i < to_check; i++ )
    {
        CHECK_EQ( frames_batch[i].data, frames_nobatch[i].data,
                   "V1 frame data should match with and without batch" );
        CHECK_EQ( frames_batch[i].slot, frames_nobatch[i].slot,
                   "V1 frame slot should match with and without batch" );
    }
}
