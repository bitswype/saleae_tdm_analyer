// Extra stubs for SDK symbols not provided by the testlib.
// The TDM analyzer uses FrameV2 APIs and ConvertToSignedNumber,
// which the SDK testlib does not implement.

#include "AnalyzerResults.h"
#include "Analyzer.h"
#include "AnalyzerHelpers.h"

// --- FrameV2 stubs ---

FrameV2::FrameV2() : mInternals( nullptr )
{
}

FrameV2::~FrameV2()
{
}

void FrameV2::AddString( const char*, const char* )
{
}

void FrameV2::AddDouble( const char*, double )
{
}

void FrameV2::AddInteger( const char*, S64 )
{
}

void FrameV2::AddBoolean( const char*, bool )
{
}

void FrameV2::AddByte( const char*, U8 )
{
}

void FrameV2::AddByteArray( const char*, const U8*, U64 )
{
}

// --- AnalyzerResults::AddFrameV2 ---

void AnalyzerResults::AddFrameV2( const FrameV2&, const char*, U64, U64 )
{
}

// --- Analyzer::UseFrameV2 ---

void Analyzer::UseFrameV2()
{
}

// --- AnalyzerHelpers::ConvertToSignedNumber ---
// Real implementation needed for correct decode of signed audio samples.

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
