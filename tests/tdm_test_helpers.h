// Shared test infrastructure for TDM analyzer correctness tests.
//
// Provides:
//   - Minimal test framework (CHECK macros, RunTest, pass/fail counters)
//   - DecodedFrame struct for V1 Frame inspection
//   - RunAndCollect() to run the analyzer and collect decoded frames
//   - VerifyCountingPattern() to verify the counting data pattern
//   - HandcraftedConfig + RunHandcraftedSignal() for hand-crafted signals
//   - RunShortSlotTest() / RunExtraSlotsTest() error signal generators
//   - EmitBitDescSignal() for BitDesc-to-MockChannelData conversion
//   - RunBitclockErrorSignal/RunMissedDataSignal/RunMissedFrameSyncSignal
//   - RunWithMismatch() for misconfig tests

#pragma once

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "MockChannelData.h"
#include "MockResults.h"
#include "TestInstance.h"
#include "AnalyzerHelpers.h"

#include "tdm_test_signal.h"
#include "TdmAnalyzerResults.h"
#include "framev2_capture.h"

// ---------------------------------------------------------------------------
// Test framework globals (defined in tdm_correctness.cpp)
// ---------------------------------------------------------------------------

extern int g_pass;
extern int g_fail;
extern bool g_test_failed;

// ---------------------------------------------------------------------------
// Assertion macros
// ---------------------------------------------------------------------------

#define CHECK( cond, msg )                                                     \
    do                                                                         \
    {                                                                          \
        if( !( cond ) )                                                        \
        {                                                                      \
            std::cerr << "    FAIL: " << msg << std::endl;                     \
            g_test_failed = true;                                              \
            return;                                                            \
        }                                                                      \
    } while( 0 )

#define CHECK_EQ( actual, expected, msg )                                      \
    do                                                                         \
    {                                                                          \
        if( ( actual ) != ( expected ) )                                       \
        {                                                                      \
            std::cerr << "    FAIL: " << msg << " (expected " << ( expected )  \
                      << ", got " << ( actual ) << ")" << std::endl;           \
            g_test_failed = true;                                              \
            return;                                                            \
        }                                                                      \
    } while( 0 )

inline void RunTest( const char* name, void ( *fn )() )
{
    std::cout << "  " << name << std::endl;
    g_test_failed = false;
    fn();
    if( g_test_failed )
        g_fail++;
    else
        g_pass++;
}

// ---------------------------------------------------------------------------
// Decoded frame (V1 Frame fields accessible from MockResults)
// ---------------------------------------------------------------------------

struct DecodedFrame
{
    U64 data;    // mData1: raw unsigned decoded value
    U8 slot;     // mType: slot number
    U8 flags;    // mFlags: error bit flags
};

// ---------------------------------------------------------------------------
// Named constant for the 6-bit error flag mask (bits 0-5 of mFlags)
// ---------------------------------------------------------------------------

static constexpr U8 ERROR_FLAG_MASK = 0x3F;

// ---------------------------------------------------------------------------
// Number of frames per test for config-based tests
// ---------------------------------------------------------------------------

static const U32 TEST_FRAMES = 100;

// ---------------------------------------------------------------------------
// BitDesc: per-bit description for hand-crafted signal construction
// ---------------------------------------------------------------------------

struct BitDesc
{
    BitState data;
    BitState frame;
};

// ---------------------------------------------------------------------------
// Run the analyzer with a Config and collect all decoded V1 Frames.
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunAndCollect( const Config& cfg );

// ---------------------------------------------------------------------------
// Verify counting pattern across all decoded frames.
//
// The signal generator uses a counting pattern: each slot value increments
// sequentially (0, 1, 2, 3, ...) wrapping at 2^data_bits_per_slot.
// The preamble provides synchronization so the analyzer decodes starting
// from the very first generated TDM frame (counter = 0).
// ---------------------------------------------------------------------------

void VerifyCountingPattern( const std::vector<DecodedFrame>& frames,
                            const Config& cfg,
                            U32 num_verify_frames );

// ---------------------------------------------------------------------------
// Hand-crafted signal support
// ---------------------------------------------------------------------------

struct HandcraftedConfig
{
    U32 frame_rate;
    U32 slots_per_frame;
    U32 bits_per_slot;
    U64 sample_rate;
};

// Run analyzer with hand-crafted transition lists.
// NOTE: Hardcodes MSB-first, PosEdge, LEFT_ALIGNED, DSP_MODE_A,
//       UnsignedInteger, FS_NOT_INVERTED.
std::vector<DecodedFrame> RunHandcraftedSignal(
    const HandcraftedConfig& hcfg,
    const std::vector<U64>& clk_transitions,
    const std::vector<U64>& frm_transitions,
    const std::vector<U64>& dat_transitions,
    BitState clk_init, BitState frm_init, BitState dat_init,
    bool advanced_analysis );

// ---------------------------------------------------------------------------
// EmitBitDescSignal: convert a BitDesc stream into MockChannelData transitions.
//
// Sets initial bit states from stream[0], then generates clock transitions
// and data/frame transitions using the standard err-accumulation loop.
// ---------------------------------------------------------------------------

void EmitBitDescSignal(
    AnalyzerTest::MockChannelData* clk,
    AnalyzerTest::MockChannelData* frm,
    AnalyzerTest::MockChannelData* dat,
    const std::vector<BitDesc>& stream,
    double half_samples );

// ---------------------------------------------------------------------------
// Shared error signal generators (used by error condition and FrameV2 tests)
// ---------------------------------------------------------------------------

// Generate a signal where one frame has fewer bits than expected (SHORT_SLOT).
std::vector<DecodedFrame> RunShortSlotTest();

// Generate a signal configured for 2 slots but with 3 slots of bits (EXTRA_SLOT).
std::vector<DecodedFrame> RunExtraSlotsTest();

// Generate a signal with a stretched clock cycle (BITCLOCK_ERROR).
std::vector<DecodedFrame> RunBitclockErrorSignal();

// Generate a signal with a data glitch between clock edges (MISSED_DATA).
std::vector<DecodedFrame> RunMissedDataSignal();

// Generate a signal with a frame sync glitch between clock edges (MISSED_FRAME_SYNC).
std::vector<DecodedFrame> RunMissedFrameSyncSignal();

// ---------------------------------------------------------------------------
// RunWithMismatch: generate signal from gen_cfg but configure analyzer
// from analyze_cfg. Collects and returns DecodedFrame vector.
// ---------------------------------------------------------------------------

std::vector<DecodedFrame> RunWithMismatch(
    const Config& gen_cfg,
    const Config& analyze_cfg );
