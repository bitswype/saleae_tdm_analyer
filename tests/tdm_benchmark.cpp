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
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include "MockChannelData.h"
#include "MockResults.h"
#include "TestInstance.h"

#include "TdmAnalyzerSettings.h"

// Fixed channel assignments for test harness
static const Channel CLK_CH( 0, 0, DIGITAL_CHANNEL );
static const Channel FRM_CH( 0, 1, DIGITAL_CHANNEL );
static const Channel DAT_CH( 0, 2, DIGITAL_CHANNEL );

// ---------------------------------------------------------------------------
// Benchmark configuration
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
};

static Config DefaultConfig( const char* label, U32 num_frames )
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
    return c;
}

// ---------------------------------------------------------------------------
// Synthetic TDM signal generator
// ---------------------------------------------------------------------------

// Generates clock, frame sync, and data channel transitions that the
// analyzer can decode. The signal uses a counting pattern for data values
// and follows the configured TDM framing conventions.

static void GenerateTdmSignal( AnalyzerTest::MockChannelData* clk,
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
    const U64 val_mod = 1ULL << cfg.data_bits_per_slot;

    for( U32 f = 0; f < gen_frames; f++ )
    {
        for( U32 s = 0; s < cfg.slots_per_frame; s++ )
        {
            U64 val = counter++ % val_mod;

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
    settings->mDataValidEdge = AnalyzerEnums::PosEdge;
    settings->mDataAlignment = cfg.data_alignment;
    settings->mBitAlignment = cfg.bit_alignment;
    settings->mSigned = cfg.sign;
    settings->mFrameSyncInverted = cfg.fs_inverted;
    settings->mEnableAdvancedAnalysis = cfg.advanced_analysis;

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

    return 0;
}
