// Tests advanced analysis error detection using hand-crafted signals with
// specific timing anomalies. Each test constructs a signal with a deliberate
// defect (stretched clock, data glitch, frame sync glitch) and verifies the
// analyzer flags it. Uses 8x oversampling (HP=4 samples) to provide room
// for placing transitions between clock edges.

#include "tdm_test_helpers.h"

void test_bitclock_error_detection()
{
    // Mono 4-bit, 8x oversampling, DSP Mode A
    // bit_freq = 48000 * 1 * 4 = 192000
    // sample_rate = 192000 * 8 = 1,536,000
    // half_period = 4 samples, full_period = 8 samples
    // mDesiredBitClockPeriod = 1536000 / (1 * 4 * 48000) = 8
    const U32 HP = 4; // half-period in samples

    HandcraftedConfig hcfg = { 48000, 1, 4, 1536000 };

    std::vector<U64> clk_trans, frm_trans, dat_trans;
    U64 pos = 0;

    // Preamble: 8 normal clock cycles with frame LOW, data LOW
    for( int i = 0; i < 8; i++ )
    {
        pos += HP; clk_trans.push_back( pos ); // rising
        pos += HP; clk_trans.push_back( pos ); // falling
    }

    // Frame 1: FS pulse at rising edge, then 4 data bits (DSP Mode A: +1 offset)
    // FS goes HIGH at next rising edge
    pos += HP;
    clk_trans.push_back( pos ); // rising - FS active here
    frm_trans.push_back( pos ); // FS goes HIGH
    pos += HP;
    clk_trans.push_back( pos ); // falling
    frm_trans.push_back( pos ); // FS goes LOW

    // 4 data bits (offset bit + 4 bits = 5 clock cycles)
    for( int i = 0; i < 5; i++ )
    {
        pos += HP; clk_trans.push_back( pos ); // rising
        pos += HP; clk_trans.push_back( pos ); // falling
    }

    // Frame 2: normal FS pulse
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos ); // FS HIGH
    pos += HP;
    clk_trans.push_back( pos );
    frm_trans.push_back( pos ); // FS LOW

    // 2 normal bits
    for( int i = 0; i < 2; i++ )
    {
        pos += HP; clk_trans.push_back( pos );
        pos += HP; clk_trans.push_back( pos );
    }

    // 1 STRETCHED clock cycle: 2x normal period (half-period = 2*HP)
    pos += HP * 2; clk_trans.push_back( pos ); // rising (late)
    pos += HP * 2; clk_trans.push_back( pos ); // falling (late)

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

    auto frames = RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                         BIT_LOW, BIT_LOW, BIT_LOW, true );

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & BITCLOCK_ERROR )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected BITCLOCK_ERROR flag from stretched clock cycle" );
}

// Task 10: MISSED_DATA detection
// Generate a clean signal but inject a glitch (extra transition) on the data
// line between clock edges.
void test_missed_data_detection()
{
    // Mono 4-bit, 8x oversampling
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
    U64 rising1 = pos;
    clk_trans.push_back( pos );
    pos += HP;
    clk_trans.push_back( pos ); // falling

    // Bit 1: inject data glitch between rising and falling edge
    pos += HP;
    U64 rising2 = pos;
    clk_trans.push_back( pos ); // rising at pos

    // Data glitch: transition at rising+1, then back at rising+2
    // This creates 2 transitions between rising and falling edges
    dat_trans.push_back( rising2 + 1 ); // LOW -> HIGH
    dat_trans.push_back( rising2 + 2 ); // HIGH -> LOW
    // Another transition after falling edge for WouldAdvancing check
    pos += HP;
    clk_trans.push_back( pos ); // falling
    dat_trans.push_back( pos + 1 ); // transition after falling edge

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

    auto frames = RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                         BIT_LOW, BIT_LOW, BIT_LOW, true );

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & MISSED_DATA )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected MISSED_DATA flag from data glitch between clock edges" );
}

// Task 11: MISSED_FRAME_SYNC detection
// Same as MISSED_DATA but glitch on the frame sync line.
void test_missed_frame_sync_detection()
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

    // Frame sync glitch between rising and falling
    frm_trans.push_back( rising + 1 ); // LOW -> HIGH
    frm_trans.push_back( rising + 2 ); // HIGH -> LOW
    pos += HP;
    clk_trans.push_back( pos );
    // Another FS transition after falling for WouldAdvancing check
    frm_trans.push_back( pos + 1 ); // transition after falling

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

    auto frames = RunHandcraftedSignal( hcfg, clk_trans, frm_trans, dat_trans,
                                         BIT_LOW, BIT_LOW, BIT_LOW, true );

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & MISSED_FRAME_SYNC )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected MISSED_FRAME_SYNC flag from FS glitch between clock edges" );
}
