// Shared TDM signal generation for benchmark and correctness tests.
//
// Generates synthetic TDM signals as MockChannelData transitions.
// Data values follow a counting pattern: slot 0 = 0, slot 1 = 1, ...
// wrapping at 2^data_bits_per_slot.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "MockChannelData.h"
#include "TdmAnalyzerSettings.h"

// Fixed channel assignments for test harness.
// Wrapped in functions to avoid C++17 inline variable requirement.
static const Channel CLK_CH( 0, 0, DIGITAL_CHANNEL );
static const Channel FRM_CH( 0, 1, DIGITAL_CHANNEL );
static const Channel DAT_CH( 0, 2, DIGITAL_CHANNEL );

// ---------------------------------------------------------------------------
// Signal configuration
// ---------------------------------------------------------------------------

struct Config
{
    const char* label;
    U32 frame_rate;
    U32 slots_per_frame;
    U32 bits_per_slot;
    U32 data_bits_per_slot;
    U32 num_frames;
    U64 sample_rate;
    bool advanced_analysis;
    TdmBitAlignment bit_alignment;
    TdmFrameSelectInverted fs_inverted;
    AnalyzerEnums::ShiftOrder shift_order;
    TdmDataAlignment data_alignment;
    AnalyzerEnums::Sign sign;
    AnalyzerEnums::EdgeDirection data_valid_edge;
};

inline Config DefaultConfig( const char* label, U32 num_frames )
{
    Config c;
    c.label = label;
    c.frame_rate = 48000;
    c.slots_per_frame = 2;
    c.bits_per_slot = 16;
    c.data_bits_per_slot = 16;
    c.num_frames = num_frames;
    // 4x oversampling of the bit clock
    c.sample_rate = U64( 48000 ) * 2 * 16 * 4;
    c.advanced_analysis = false;
    c.bit_alignment = DSP_MODE_A;
    c.fs_inverted = FS_NOT_INVERTED;
    c.shift_order = AnalyzerEnums::MsbFirst;
    c.data_alignment = LEFT_ALIGNED;
    c.sign = AnalyzerEnums::UnsignedInteger;
    c.data_valid_edge = AnalyzerEnums::PosEdge;
    return c;
}

// ---------------------------------------------------------------------------
// Synthetic TDM signal generator
// ---------------------------------------------------------------------------

// Generates clock, frame sync, and data channel transitions that the
// analyzer can decode. The signal uses a counting pattern for data values
// and follows the configured TDM framing conventions.

inline void GenerateTdmSignal( AnalyzerTest::MockChannelData* clk,
                                AnalyzerTest::MockChannelData* frm,
                                AnalyzerTest::MockChannelData* dat,
                                const Config& cfg )
{
    const U32 bpf = cfg.slots_per_frame * cfg.bits_per_slot; // bits per TDM frame
    const U32 gen_frames = cfg.num_frames + 2; // extra frames for clean termination

    const BitState fs_active = ( cfg.fs_inverted == FS_NOT_INVERTED ) ? BIT_HIGH : BIT_LOW;
    const BitState fs_inactive = ( cfg.fs_inverted == FS_NOT_INVERTED ) ? BIT_LOW : BIT_HIGH;

    // --- Build data bit stream (one bit per clock cycle per frame) ---

    std::vector<BitState> data_stream;
    data_stream.reserve( U64( gen_frames ) * bpf );

    U32 counter = 0;
    // For 64-bit data, 1ULL << 64 is undefined behavior, so use 0 as
    // a sentinel meaning "no modulo needed" (counter never wraps).
    const U64 val_mod = ( cfg.data_bits_per_slot >= 64 ) ? 0 : ( 1ULL << cfg.data_bits_per_slot );

    for( U32 f = 0; f < gen_frames; f++ )
    {
        for( U32 s = 0; s < cfg.slots_per_frame; s++ )
        {
            U64 val = ( val_mod == 0 ) ? counter++ : ( counter++ % val_mod );

            const U32 padding = cfg.bits_per_slot - cfg.data_bits_per_slot;
            const U32 left_pad = ( cfg.data_alignment == RIGHT_ALIGNED ) ? padding : 0;

            for( U32 b = 0; b < cfg.bits_per_slot; b++ )
            {
                if( b < left_pad || b >= left_pad + cfg.data_bits_per_slot )
                {
                    data_stream.push_back( BIT_LOW );
                }
                else
                {
                    U32 dbi = b - left_pad; // data bit index within the slot
                    U64 mask;
                    if( cfg.shift_order == AnalyzerEnums::MsbFirst )
                        mask = 1ULL << ( cfg.data_bits_per_slot - 1 - dbi );
                    else
                        mask = 1ULL << dbi;
                    data_stream.push_back( ( val & mask ) ? BIT_HIGH : BIT_LOW );
                }
            }
        }
    }

    // --- Build frame sync bit stream ---
    // Pattern per frame: [active, inactive, inactive, ..., inactive]

    std::vector<BitState> frame_stream;
    frame_stream.reserve( U64( gen_frames ) * bpf );

    for( U32 f = 0; f < gen_frames; f++ )
    {
        frame_stream.push_back( fs_active );
        for( U32 b = 1; b < bpf; b++ )
            frame_stream.push_back( fs_inactive );
    }

    // --- Combine with DSP mode offset into a single bit stream ---

    struct BitPair
    {
        BitState data;
        BitState frame;
    };

    std::vector<BitPair> stream;
    stream.reserve( 32 + frame_stream.size() + 32 );

    // Preamble: 16 idle bits so the analyzer can lock onto the first frame sync
    for( int i = 0; i < 16; i++ )
        stream.push_back( { BIT_LOW, fs_inactive } );

    if( cfg.bit_alignment == DSP_MODE_A )
    {
        // Data is delayed by 1 bit relative to frame sync.
        // First bit: frame sync active, data is the offset bit (LOW).
        stream.push_back( { BIT_LOW, frame_stream[ 0 ] } );
        for( size_t i = 1; i < frame_stream.size(); i++ )
            stream.push_back( { data_stream[ i - 1 ], frame_stream[ i ] } );
    }
    else
    {
        // DSP Mode B: frame and data are aligned
        size_t len = std::min( frame_stream.size(), data_stream.size() );
        for( size_t i = 0; i < len; i++ )
            stream.push_back( { data_stream[ i ], frame_stream[ i ] } );
    }

    // Trailing idle so the analyzer doesn't run out of data mid-slot
    for( int i = 0; i < 32; i++ )
        stream.push_back( { BIT_LOW, fs_inactive } );

    // --- Convert bit stream to MockChannelData transitions ---
    //
    // Clock: regular square wave (LOW -> HIGH -> LOW per bit).
    // Data/frame: transitions placed at the falling clock edge BEFORE the
    // rising edge where the value must be valid. This avoids false
    // MISSED_DATA errors in advanced analysis mode.

    const double bit_freq = double( cfg.frame_rate ) * bpf;
    const double half_samples = double( cfg.sample_rate ) / ( 2.0 * bit_freq );

    // Initial channel states
    clk->TestSetInitialBitState( BIT_LOW );
    frm->TestSetInitialBitState( stream[ 0 ].frame );
    dat->TestSetInitialBitState( stream[ 0 ].data );

    BitState cur_dat = stream[ 0 ].data;
    BitState cur_frm = stream[ 0 ].frame;
    double err = 0.0;
    U64 pos = 0;

    for( size_t i = 0; i < stream.size(); i++ )
    {
        // Rising edge (data is already valid from previous setup)
        double target = half_samples + err;
        U32 n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos ); // LOW -> HIGH

        // Falling edge
        target = half_samples + err;
        n = static_cast<U32>( std::lround( target ) );
        err = target - double( n );
        pos += n;
        clk->TestAppendTransitionAtSamples( pos ); // HIGH -> LOW

        // Set up data/frame for the NEXT bit at this falling edge
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
