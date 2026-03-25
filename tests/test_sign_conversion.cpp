/**
 * Sign Conversion Tests
 *
 * Sign conversion is tested at two levels:
 *   1. Unit tests of ConvertToSignedNumber with known inputs — verifying correct
 *      two's-complement interpretation for 16-bit, 24-bit, 32-bit, and 64-bit widths,
 *      plus edge cases (0-bit, 1-bit, >64-bit).
 *   2. End-to-end tests verifying the analyzer pipeline with signed mode enabled.
 *
 * The FrameV2 signed decode test verifies that the "data" field in FrameV2 contains
 * correct signed values (e.g., 4-bit value 8 produces -8), which was previously
 * untestable before the FrameV2 capture mock.
 */

#include "tdm_test_helpers.h"

// ===================================================================
// Unit Tests: ConvertToSignedNumber
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
// End-to-End: Signed Mode Pipeline
// ===================================================================

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
// UB Fix Verification: 64-bit Sign Extension
// ===================================================================

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
// FrameV2 Signed Decode Verification
// ===================================================================

void test_framev2_signed_decode()
{
    ClearCapturedFrameV2s();

    Config c = DefaultConfig( "fv2-signed", 20 );
    c.bits_per_slot = 4;
    c.data_bits_per_slot = 4;
    c.sign = AnalyzerEnums::SignedInteger;
    c.sample_rate = U64( 48000 ) * 2 * 4 * 4;
    auto frames = RunAndCollect( c );

    auto& fv2s = GetCapturedFrameV2s();

    // Collect "slot" type FrameV2 records
    std::vector<const CapturedFrameV2*> slot_fv2s;
    for( const auto& fv2 : fv2s )
    {
        if( fv2.type == "slot" )
            slot_fv2s.push_back( &fv2 );
    }

    CHECK( slot_fv2s.size() >= 20, "Should have at least 20 slot FrameV2 records" );

    // Counter goes 0,1,2,...,15,0,1,... (mod 16 for 4-bit).
    // Signed 4-bit: 0-7 -> 0-7, 8-15 -> -8 to -1
    U32 counter = 0;
    for( U32 i = 0; i < 20 && i < slot_fv2s.size(); i++ )
    {
        U32 raw = counter % 16;
        S64 expected_signed;
        if( raw >= 8 )
            expected_signed = S64( raw ) - 16; // 8->-8, 9->-7, ..., 15->-1
        else
            expected_signed = S64( raw );

        S64 actual = slot_fv2s[ i ]->GetInteger( "data" );
        std::ostringstream oss;
        if( actual != expected_signed )
        {
            oss << "FrameV2 slot " << i << ": signed data expected "
                << expected_signed << ", got " << actual
                << " (raw=" << raw << ")";
            CHECK( false, oss.str() );
        }
        counter++;
    }
}
