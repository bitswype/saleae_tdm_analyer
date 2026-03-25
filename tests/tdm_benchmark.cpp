// TDM Analyzer performance benchmark.
//
// Generates synthetic TDM signals as MockChannelData transitions,
// runs the analyzer's WorkerThread via the SDK testlib, and reports
// decode throughput.
//
// Usage:
//   tdm_benchmark [num_frames]
//
// Default: 48000 frames (1 second at 48 kHz).

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "MockResults.h"
#include "TestInstance.h"

#include "tdm_test_signal.h"
#include "TdmProfiler.h"
#include "framev2_capture.h"

// ---------------------------------------------------------------------------
// Benchmark runner
// ---------------------------------------------------------------------------

struct BenchResult
{
    double gen_ms;
    double decode_ms;
    U64 slots_decoded;
};

static BenchResult RunBenchmark( const Config& cfg )
{
    ClearCapturedFrameV2s(); // prevent FrameV2 mock accumulation across runs
    BenchResult result = {};

    AnalyzerTest::Instance instance;
    instance.CreatePlugin( "TDM" );

    AnalyzerTest::MockChannelData* clk_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* frm_mock = new AnalyzerTest::MockChannelData( &instance );
    AnalyzerTest::MockChannelData* dat_mock = new AnalyzerTest::MockChannelData( &instance );

    // Generate synthetic signal
    auto gen_start = std::chrono::steady_clock::now();
    GenerateTdmSignal( clk_mock, frm_mock, dat_mock, cfg );
    auto gen_end = std::chrono::steady_clock::now();
    result.gen_ms = std::chrono::duration<double, std::milli>( gen_end - gen_start ).count();

    // TestAppendTransitionAtSamples leaves the mock cursor at the end of
    // the transition list.  Reset all three channels to sample 0 so the
    // analyzer starts reading from the beginning.
    clk_mock->ResetCurrentSample();
    frm_mock->ResetCurrentSample();
    dat_mock->ResetCurrentSample();

    // Wire up channels and sample rate
    instance.SetChannelData( CLK_CH, clk_mock );
    instance.SetChannelData( FRM_CH, frm_mock );
    instance.SetChannelData( DAT_CH, dat_mock );
    instance.SetSampleRate( cfg.sample_rate );

    // Configure analyzer settings
    TdmAnalyzerSettings* settings = dynamic_cast<TdmAnalyzerSettings*>( instance.GetSettings() );
    settings->mClockChannel = CLK_CH;
    settings->mFrameChannel = FRM_CH;
    settings->mDataChannel = DAT_CH;
    settings->mTdmFrameRate = cfg.frame_rate;
    settings->mSlotsPerFrame = cfg.slots_per_frame;
    settings->mBitsPerSlot = cfg.bits_per_slot;
    settings->mDataBitsPerSlot = cfg.data_bits_per_slot;
    settings->mShiftOrder = cfg.shift_order;
    settings->mDataValidEdge = cfg.data_valid_edge;
    settings->mDataAlignment = cfg.data_alignment;
    settings->mBitAlignment = cfg.bit_alignment;
    settings->mSigned = cfg.sign;
    settings->mFrameSyncInverted = cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = cfg.advanced_analysis;
    settings->mFrameV2Detail = cfg.framev2_detail;
    settings->mMarkerDensity = cfg.marker_density;

    // Run the analyzer and time it
    auto decode_start = std::chrono::steady_clock::now();
    AnalyzerTest::Instance::RunResult run_result = instance.RunAnalyzerWorker();
    auto decode_end = std::chrono::steady_clock::now();
    result.decode_ms = std::chrono::duration<double, std::milli>( decode_end - decode_start ).count();

    if( run_result != AnalyzerTest::Instance::WorkerRanOutOfData )
    {
        std::cerr << "  WARNING: worker did not terminate normally (result=" << run_result << ")" << std::endl;
    }

    // Count decoded slot frames
    AnalyzerTest::MockResultData* mock_results =
        AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );
    result.slots_decoded = mock_results->TotalFrameCount();

    return result;
}

// ---------------------------------------------------------------------------
// Output formatting
// ---------------------------------------------------------------------------

static void PrintResult( const Config& cfg, const BenchResult& r )
{
    U64 tdm_frames = r.slots_decoded / cfg.slots_per_frame;
    U64 total_bits = r.slots_decoded * cfg.data_bits_per_slot;
    double decode_sec = r.decode_ms / 1000.0;

    double frames_per_sec = ( decode_sec > 0 ) ? ( double( tdm_frames ) / decode_sec ) : 0;
    double mbits_per_sec = ( decode_sec > 0 ) ? ( double( total_bits ) / decode_sec / 1e6 ) : 0;
    double realtime_x = ( cfg.frame_rate > 0 ) ? ( frames_per_sec / cfg.frame_rate ) : 0;

    std::cout << std::fixed;
    std::cout << "  " << cfg.label << std::endl;
    std::cout << "    Config: " << cfg.frame_rate / 1000 << " kHz, "
              << cfg.slots_per_frame << " ch, " << cfg.data_bits_per_slot << "-bit"
              << ( cfg.advanced_analysis ? ", +advanced" : "" ) << std::endl;
    std::cout << "    Signal gen:  " << std::setprecision( 1 ) << r.gen_ms << " ms" << std::endl;
    std::cout << "    Decode:      " << std::setprecision( 1 ) << r.decode_ms << " ms"
              << "  (" << tdm_frames << " frames, " << r.slots_decoded << " slots)" << std::endl;
    std::cout << "    Throughput:  " << std::setprecision( 1 ) << mbits_per_sec << " Mbit/s"
              << "  |  " << std::setprecision( 1 ) << realtime_x << "x realtime" << std::endl;
    TDM_PROFILE_PRINT();
    TDM_PROFILE_RESET();
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main( int argc, char** argv )
{
    U32 num_frames = 48000; // 1 second at 48 kHz

    if( argc > 1 )
    {
        int n = std::atoi( argv[ 1 ] );
        if( n > 0 )
            num_frames = static_cast<U32>( n );
    }

    std::cout << "TDM Analyzer Performance Benchmark" << std::endl;
    std::cout << "===================================" << std::endl;
    std::cout << "Frames per test: " << num_frames << std::endl;
    std::cout << std::endl;

    // --- Stereo 16-bit (basic I2S-like) ---
    {
        Config c = DefaultConfig( "Stereo 16-bit", num_frames );
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- Stereo 16-bit with advanced analysis ---
    {
        Config c = DefaultConfig( "Stereo 16-bit +advanced", num_frames );
        c.advanced_analysis = true;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- 8-channel 16-bit ---
    {
        Config c = DefaultConfig( "8-channel 16-bit", num_frames );
        c.slots_per_frame = 8;
        c.sample_rate = U64( 48000 ) * 8 * 16 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- Stereo 32-bit ---
    {
        Config c = DefaultConfig( "Stereo 32-bit", num_frames );
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 32;
        c.sample_rate = U64( 48000 ) * 2 * 32 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- 96 kHz stereo 24-bit (24 data bits in 32-bit slot) ---
    {
        Config c = DefaultConfig( "96 kHz Stereo 24/32-bit", num_frames );
        c.frame_rate = 96000;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 24;
        c.sample_rate = U64( 96000 ) * 2 * 32 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- 8-channel 16-bit with advanced analysis ---
    {
        Config c = DefaultConfig( "8-channel 16-bit +advanced", num_frames );
        c.slots_per_frame = 8;
        c.sample_rate = U64( 48000 ) * 8 * 16 * 4;
        c.advanced_analysis = true;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High channel count: 16-channel 16-bit ---
    {
        Config c = DefaultConfig( "16-channel 16-bit", num_frames );
        c.slots_per_frame = 16;
        c.sample_rate = U64( 48000 ) * 16 * 16 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High channel count: 32-channel 16-bit ---
    {
        Config c = DefaultConfig( "32-channel 16-bit", num_frames );
        c.slots_per_frame = 32;
        c.sample_rate = U64( 48000 ) * 32 * 16 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High channel count: 64-channel 16-bit ---
    {
        Config c = DefaultConfig( "64-channel 16-bit", num_frames );
        c.slots_per_frame = 64;
        c.sample_rate = U64( 48000 ) * 64 * 16 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High resolution: Stereo 64-bit ---
    {
        Config c = DefaultConfig( "Stereo 64-bit", num_frames );
        c.bits_per_slot = 64;
        c.data_bits_per_slot = 64;
        c.sample_rate = U64( 48000 ) * 2 * 64 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High resolution: 8-channel 32-bit ---
    {
        Config c = DefaultConfig( "8-channel 32-bit", num_frames );
        c.slots_per_frame = 8;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 32;
        c.sample_rate = U64( 48000 ) * 8 * 32 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High resolution: 8-channel 24/32-bit ---
    {
        Config c = DefaultConfig( "8-channel 24/32-bit", num_frames );
        c.slots_per_frame = 8;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 24;
        c.sample_rate = U64( 48000 ) * 8 * 32 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High sample rate: 192 kHz Stereo 24/32-bit ---
    {
        Config c = DefaultConfig( "192 kHz Stereo 24/32-bit", num_frames );
        c.frame_rate = 192000;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 24;
        c.sample_rate = U64( 192000 ) * 2 * 32 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- High sample rate: 384 kHz Stereo 32-bit ---
    {
        Config c = DefaultConfig( "384 kHz Stereo 32-bit", num_frames );
        c.frame_rate = 384000;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 32;
        c.sample_rate = U64( 384000 ) * 2 * 32 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- Combined stress: 16-channel 32-bit 96 kHz ---
    {
        Config c = DefaultConfig( "96 kHz 16-channel 32-bit", num_frames );
        c.frame_rate = 96000;
        c.slots_per_frame = 16;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 32;
        c.sample_rate = U64( 96000 ) * 16 * 32 * 4;
        PrintResult( c, RunBenchmark( c ) );
    }

    // --- Combined stress: 32-channel 32-bit 48 kHz +advanced ---
    {
        Config c = DefaultConfig( "32-channel 32-bit +advanced", num_frames );
        c.slots_per_frame = 32;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 32;
        c.sample_rate = U64( 48000 ) * 32 * 32 * 4;
        c.advanced_analysis = true;
        PrintResult( c, RunBenchmark( c ) );
    }

    // -----------------------------------------------------------------------
    // Profiled runs with optimized settings (Minimal + SlotMarkers)
    // -----------------------------------------------------------------------

    std::cout << "Optimized Mode (Minimal + Slot Markers)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    {
        Config c = DefaultConfig( "Stereo 16-bit [Minimal+Slot]", num_frames );
        c.framev2_detail = FV2_MINIMAL;
        c.marker_density = MARKERS_SLOT_ONLY;
        PrintResult( c, RunBenchmark( c ) );
    }

    {
        Config c = DefaultConfig( "8-channel 16-bit [Minimal+Slot]", num_frames );
        c.slots_per_frame = 8;
        c.sample_rate = U64( 48000 ) * 8 * 16 * 4;
        c.framev2_detail = FV2_MINIMAL;
        c.marker_density = MARKERS_SLOT_ONLY;
        PrintResult( c, RunBenchmark( c ) );
    }

    {
        Config c = DefaultConfig( "8-channel 32-bit [Minimal+Slot]", num_frames );
        c.slots_per_frame = 8;
        c.bits_per_slot = 32;
        c.data_bits_per_slot = 32;
        c.sample_rate = U64( 48000 ) * 8 * 32 * 4;
        c.framev2_detail = FV2_MINIMAL;
        c.marker_density = MARKERS_SLOT_ONLY;
        PrintResult( c, RunBenchmark( c ) );
    }

    {
        Config c = DefaultConfig( "Stereo 16-bit [Off+None]", num_frames );
        c.framev2_detail = FV2_OFF;
        c.marker_density = MARKERS_NONE;
        PrintResult( c, RunBenchmark( c ) );
    }

    {
        Config c = DefaultConfig( "8-channel 16-bit [Off+None]", num_frames );
        c.slots_per_frame = 8;
        c.sample_rate = U64( 48000 ) * 8 * 16 * 4;
        c.framev2_detail = FV2_OFF;
        c.marker_density = MARKERS_NONE;
        PrintResult( c, RunBenchmark( c ) );
    }

    // -----------------------------------------------------------------------
    // Performance mode comparison
    // -----------------------------------------------------------------------

    std::cout << "Performance Mode Comparison" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << std::endl;

    // Compare all FV2 + marker combos on a few representative configs
    struct ModeLabel { TdmFrameV2Detail fv2; TdmMarkerDensity markers; const char* name; };
    ModeLabel modes[] = {
        { FV2_FULL,    MARKERS_ALL,       "Full + AllMarkers (default)" },
        { FV2_FULL,    MARKERS_SLOT_ONLY, "Full + SlotMarkers" },
        { FV2_FULL,    MARKERS_NONE,      "Full + NoMarkers" },
        { FV2_MINIMAL, MARKERS_ALL,       "Minimal + AllMarkers" },
        { FV2_MINIMAL, MARKERS_SLOT_ONLY, "Minimal + SlotMarkers" },
        { FV2_MINIMAL, MARKERS_NONE,      "Minimal + NoMarkers" },
        { FV2_OFF,     MARKERS_ALL,       "Off + AllMarkers" },
        { FV2_OFF,     MARKERS_NONE,      "Off + NoMarkers (fastest)" },
    };

    struct CompareConfig { const char* label; U32 slots; U32 bits; U32 data_bits; U32 rate; };
    CompareConfig compare_cfgs[] = {
        { "Stereo 16-bit",    2,  16, 16, 48000 },
        { "8-channel 16-bit", 8,  16, 16, 48000 },
        { "8-channel 32-bit", 8,  32, 32, 48000 },
    };

    for( const auto& cc : compare_cfgs )
    {
        std::cout << "  " << cc.label << ":" << std::endl;
        for( const auto& mode : modes )
        {
            Config c = DefaultConfig( cc.label, num_frames );
            c.slots_per_frame = cc.slots;
            c.bits_per_slot = cc.bits;
            c.data_bits_per_slot = cc.data_bits;
            c.frame_rate = cc.rate;
            c.sample_rate = U64( cc.rate ) * cc.slots * cc.bits * 4;
            c.framev2_detail = mode.fv2;
            c.marker_density = mode.markers;

            BenchResult r = RunBenchmark( c );
            U64 total_bits = r.slots_decoded * cc.data_bits;
            double decode_sec = r.decode_ms / 1000.0;
            double mbits = ( decode_sec > 0 ) ? ( double( total_bits ) / decode_sec / 1e6 ) : 0;
            double rtx = ( cc.rate > 0 ) ? ( double( r.slots_decoded / cc.slots ) / decode_sec / cc.rate ) : 0;

            std::cout << std::fixed << std::setprecision( 1 );
            std::cout << "    " << std::setw( 35 ) << std::left << mode.name
                      << "  " << std::setw( 8 ) << std::right << r.decode_ms << " ms"
                      << "  " << std::setw( 5 ) << mbits << " Mbit/s"
                      << "  " << std::setw( 5 ) << rtx << "x RT" << std::endl;
            TDM_PROFILE_RESET();
        }
        std::cout << std::endl;
    }

    return 0;
}
