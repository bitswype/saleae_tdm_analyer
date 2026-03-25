// Extra stubs for SDK symbols not provided by the testlib.
// The TDM analyzer uses FrameV2 APIs and ConvertToSignedNumber,
// which the SDK testlib does not implement.
//
// FrameV2 methods capture key/value pairs into FrameV2Data so that
// AddFrameV2 can copy them into a global capture vector for test
// assertions. See framev2_capture.h for the test-side API.

#include "AnalyzerResults.h"
#include "Analyzer.h"
#include "AnalyzerHelpers.h"

#include "framev2_capture.h"

#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// FrameV2Data: the opaque internals of FrameV2, defined here for testing.
// ---------------------------------------------------------------------------

struct FrameV2Data
{
    std::map<std::string, FrameV2FieldValue> fields;
};

// ---------------------------------------------------------------------------
// Global capture storage
// ---------------------------------------------------------------------------

static std::vector<CapturedFrameV2> g_captured;

std::vector<CapturedFrameV2>& GetCapturedFrameV2s()
{
    return g_captured;
}

void ClearCapturedFrameV2s()
{
    g_captured.clear();
}

// ---------------------------------------------------------------------------
// FrameV2 implementation (captures fields into mInternals)
// ---------------------------------------------------------------------------

FrameV2::FrameV2() : mInternals( new FrameV2Data() )
{
}

FrameV2::~FrameV2()
{
    delete mInternals;
}

void FrameV2::AddString( const char* key, const char* value )
{
    FrameV2FieldValue fv;
    fv.type = FrameV2FieldValue::STRING;
    fv.str_val = value ? value : "";
    mInternals->fields[ key ] = fv;
}

void FrameV2::AddDouble( const char* key, double value )
{
    FrameV2FieldValue fv;
    fv.type = FrameV2FieldValue::DOUBLE;
    fv.dbl_val = value;
    mInternals->fields[ key ] = fv;
}

void FrameV2::AddInteger( const char* key, S64 value )
{
    FrameV2FieldValue fv;
    fv.type = FrameV2FieldValue::INTEGER;
    fv.int_val = value;
    mInternals->fields[ key ] = fv;
}

void FrameV2::AddBoolean( const char* key, bool value )
{
    FrameV2FieldValue fv;
    fv.type = FrameV2FieldValue::BOOLEAN;
    fv.bool_val = value;
    mInternals->fields[ key ] = fv;
}

void FrameV2::AddByte( const char* key, U8 value )
{
    FrameV2FieldValue fv;
    fv.type = FrameV2FieldValue::BYTE;
    fv.byte_val = value;
    mInternals->fields[ key ] = fv;
}

void FrameV2::AddByteArray( const char*, const U8*, U64 )
{
    // Not captured -- no test currently needs byte array fields.
}

// ---------------------------------------------------------------------------
// AnalyzerResults::AddFrameV2 -- captures the FrameV2 into the global vector
// ---------------------------------------------------------------------------

void AnalyzerResults::AddFrameV2( const FrameV2& frame, const char* type,
                                   U64 starting_sample, U64 ending_sample )
{
    CapturedFrameV2 record;
    record.type = type ? type : "";
    record.starting_sample = starting_sample;
    record.ending_sample = ending_sample;
    if( frame.mInternals )
        record.fields = frame.mInternals->fields;
    g_captured.push_back( record );
}

// ---------------------------------------------------------------------------
// Analyzer::UseFrameV2
// ---------------------------------------------------------------------------

void Analyzer::UseFrameV2()
{
}

// ---------------------------------------------------------------------------
// AnalyzerHelpers::ConvertToSignedNumber
// Real implementation needed for correct decode of signed audio samples.
// ---------------------------------------------------------------------------

S64 AnalyzerHelpers::ConvertToSignedNumber( U64 number, U32 num_bits )
{
    if( num_bits == 0 || num_bits > 64 )
        return 0;

    U64 sign_bit = 1ULL << ( num_bits - 1 );
    if( number & sign_bit )
    {
        if( num_bits == 64 )
            return static_cast<S64>( number ); // already fills all 64 bits
        U64 mask = ~( ( 1ULL << num_bits ) - 1 );
        return static_cast<S64>( number | mask );
    }
    return static_cast<S64>( number );
}
