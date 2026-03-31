#include "TdmAnalyzer.h"
#include "TdmAnalyzerSettings.h"
#include "TdmProfiler.h"
#include <AnalyzerChannelData.h>
#include <stdio.h>

#ifdef TDM_BENCHMARK_TIMING
#include <chrono>
#include <cstdlib>
static void WriteBenchmarkTiming( double decode_seconds, U64 frame_count,
                                   U32 slots, U32 bits, U32 data_bits,
                                   int fv2_detail, int marker_density );
#endif

TdmAnalyzer::TdmAnalyzer()
  : Analyzer2(),
    mSettings( new TdmAnalyzerSettings() ),
    mLowSampleRate( false ),
    mSimulationInitilized( false )
{
    SetAnalyzerSettings( mSettings.get() );
    // Required: registers this analyzer as a FrameV2 producer.
    // Without this, AddFrameV2() data is silently dropped and the data table will be empty.
    // Do not remove. Requires Logic 2.3.43+.
    UseFrameV2();
}

TdmAnalyzer::~TdmAnalyzer()
{
    KillThread();

    // Flush any partial audio batch accumulated before the thread exited
    if( mSettings && mSettings->mAudioBatchSize > 0 && mBatchFrameCount > 0 )
    {
        try
        {
            EmitAudioBatch();
            mResults->CommitResults();
        }
        catch( ... )
        {
            // mResults may be invalid by this point; silently drop
        }
    }

#ifdef TDM_BENCHMARK_TIMING
    // Write decode timing to temp file for external benchmarking.
    // Uses mDecodeLastFrame (stamped after each TDM frame) as the end time,
    // not "now", since the destructor runs when the user closes the tab.
    if( mDecodeStarted )
    {
        double seconds = std::chrono::duration<double>( mDecodeLastFrame - mDecodeStart ).count();
        WriteBenchmarkTiming( seconds, mFrameNum,
                              mSettings->mSlotsPerFrame, mSettings->mBitsPerSlot,
                              mSettings->mDataBitsPerSlot,
                              int( mSettings->mFrameV2Detail ),
                              int( mSettings->mMarkerDensity ) );
    }
#endif
}

void TdmAnalyzer::SetupResults()
{
    mResults.reset( new TdmAnalyzerResults( this, mSettings.get() ) );
    SetAnalyzerResults( mResults.get() );
    mResults->AddChannelBubblesWillAppearOn( mSettings->mDataChannel );
}

static void FormatHzString( char* buf, size_t buf_size, U64 hz )
{
    if( hz >= 1000000ULL )
        snprintf( buf, buf_size, "%llu MHz", (unsigned long long)( hz / 1000000ULL ) );
    else if( hz >= 1000ULL )
        snprintf( buf, buf_size, "%llu kHz", (unsigned long long)( hz / 1000ULL ) );
    else
        snprintf( buf, buf_size, "%llu Hz", (unsigned long long)hz );
}

void TdmAnalyzer::WorkerThread()
{
    // UpArrow, DownArrow
    if( mSettings->mDataValidEdge == AnalyzerEnums::NegEdge )
        mArrowMarker = AnalyzerResults::DownArrow;
    else
        mArrowMarker = AnalyzerResults::UpArrow;

    mClock = GetAnalyzerChannelData( mSettings->mClockChannel );
    mFrame = GetAnalyzerChannelData( mSettings->mFrameChannel );
    mData = GetAnalyzerChannelData( mSettings->mDataChannel );

    mSampleRate = GetSampleRate();
    mDesiredBitClockPeriod = double (mSampleRate) / double (mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate);

    // Phase 6 SRAT-01: Sample rate advisory — emit before first decoded slot
    U64 bit_clock_hz = U64( mSettings->mTdmFrameRate ) * U64( mSettings->mSlotsPerFrame ) * U64( mSettings->mBitsPerSlot );
    U64 recommended_min = bit_clock_hz * kMinOversampleRatio;
    mLowSampleRate = ( mSampleRate < recommended_min );

    if( mLowSampleRate )
    {
        char capture_str[ 32 ];
        char recommended_str[ 32 ];
        FormatHzString( capture_str, sizeof( capture_str ), mSampleRate );
        FormatHzString( recommended_str, sizeof( recommended_str ), recommended_min );

        char msg[ 256 ];
        snprintf( msg, sizeof( msg ),
            "Capture rate: %s is below recommended 4x bit clock (%s). "
            "4x oversampling is needed for reliable edge detection.",
            capture_str, recommended_str );

        FrameV2 advisory;
        advisory.AddString( "severity", "warning" );
        advisory.AddString( "message", msg );
        mResults->AddFrameV2( advisory, "advisory", 0, 0 );
        mResults->CommitResults();
    }

    if( mSettings->mFrameV2Detail == FV2_OFF )
    {
        FrameV2 advisory;
        advisory.AddString( "severity", "warning" );
        advisory.AddString( "message",
            "FrameV2 output is disabled (Data Table / HLA Output = Off). "
            "HLA extensions (WAV export, audio streaming) will not receive data. "
            "Change this setting to 'Full' or 'Minimal' to enable HLA support." );
        mResults->AddFrameV2( advisory, "advisory", 0, 0 );
        mResults->CommitResults();
    }

    mDataBits.reserve( mSettings->mBitsPerSlot );
    mDataValidEdges.reserve( mSettings->mBitsPerSlot );
    mDataFlags.reserve( mSettings->mBitsPerSlot );

    // Audio batch mode setup
    mBatchFrameCount = 0;
    mBatchStartFrameNum = 0;
    mBatchStartSample = 0;
    mBatchEndSample = 0;
    if( mSettings->mAudioBatchSize > 0 )
    {
        U32 data_bits = mSettings->mDataBitsPerSlot;
        if( data_bits <= 8 )       mBatchBytesPerSample = 1;
        else if( data_bits <= 16 ) mBatchBytesPerSample = 2;
        else if( data_bits <= 24 ) mBatchBytesPerSample = 3;
        else                       mBatchBytesPerSample = 4;

        mBatchBytesPerFrame = mSettings->mSlotsPerFrame * mBatchBytesPerSample;
        U64 total_bytes = U64( mSettings->mAudioBatchSize ) * mBatchBytesPerFrame;
        mBatchBuffer.resize( total_bytes, 0 );

        FrameV2 advisory;
        advisory.AddString( "severity", "info" );
        char msg[ 256 ];
        snprintf( msg, sizeof( msg ),
            "Audio Batch Size is %u. Individual slot FrameV2 output is disabled. "
            "HLAs will receive 'audio_batch' frames with packed PCM data.",
            mSettings->mAudioBatchSize );
        advisory.AddString( "message", msg );
        mResults->AddFrameV2( advisory, "advisory", 0, 0 );
        mResults->CommitResults();
    }
    else
    {
        mBatchBytesPerSample = 0;
        mBatchBytesPerFrame = 0;
    }

    SetupForGettingFirstBit();
    SetupForGettingFirstTdmFrame();
    mFrameNum = 0;

#ifdef TDM_BENCHMARK_TIMING
    mDecodeStart = std::chrono::steady_clock::now();
    mDecodeStarted = true;
#endif

    for( ;; )
    {
        GetTdmFrame();
#ifdef TDM_BENCHMARK_TIMING
        mDecodeLastFrame = std::chrono::steady_clock::now();
#endif
        CheckIfThreadShouldExit();
    }

    // Note: this code is reached if CheckIfThreadShouldExit returns normally.
    // In practice, the SDK terminates the thread via exception or signal,
    // so the timing write is in the destructor instead.
}

#ifdef TDM_BENCHMARK_TIMING
// Write benchmark timing to %USERPROFILE%\tdm_benchmark_timing.json.
// Called from destructor so it runs regardless of how WorkerThread exits.
// Enable via: cmake -DENABLE_BENCHMARK_TIMING=ON
static void WriteBenchmarkTiming( double decode_seconds, U64 frame_count,
                                   U32 slots, U32 bits, U32 data_bits,
                                   int fv2_detail, int marker_density )
{
    const char* home = std::getenv( "USERPROFILE" );
    if( !home ) home = std::getenv( "TEMP" );
    if( !home ) home = "C:\\Users\\Public";

    char path[ 512 ];
    snprintf( path, sizeof( path ), "%s\\tdm_benchmark_timing.json", home );

    FILE* f = fopen( path, "w" );
    if( !f ) return;

    double fps = ( decode_seconds > 0.0 ) ? ( frame_count / decode_seconds ) : 0.0;
    double total_bits = double( frame_count ) * double( bits );
    double mbps = ( decode_seconds > 0.0 ) ? ( total_bits / decode_seconds / 1000000.0 ) : 0.0;

    const char* fv2_str = ( fv2_detail == 0 ) ? "Full" : ( fv2_detail == 1 ) ? "Minimal" : "Off";
    const char* mkr_str = ( marker_density == 0 ) ? "All" : ( marker_density == 1 ) ? "Slot" : "None";

    fprintf( f,
        "{\n"
        "  \"decode_seconds\": %.4f,\n"
        "  \"frames\": %llu,\n"
        "  \"frames_per_sec\": %.0f,\n"
        "  \"megabits_per_sec\": %.2f,\n"
        "  \"slots_per_frame\": %u,\n"
        "  \"bits_per_slot\": %u,\n"
        "  \"data_bits_per_slot\": %u,\n"
        "  \"framev2_detail\": \"%s\",\n"
        "  \"marker_density\": \"%s\"\n"
        "}\n",
        decode_seconds,
        (unsigned long long)frame_count,
        fps, mbps,
        slots, bits, data_bits,
        fv2_str, mkr_str );
    fclose( f );
}
#endif // TDM_BENCHMARK_TIMING

void TdmAnalyzer::SetupForGettingFirstBit()
{
    if( mSettings->mDataValidEdge == AnalyzerEnums::PosEdge )
    {
        // we want to start out low, so the next time we advance, it'll be a rising edge.
        if( mClock->GetBitState() == BIT_HIGH )
            mClock->AdvanceToNextEdge(); // now we're low.
    }
    else
    {
        // we want to start out high, so the next time we advance, it'll be a falling edge.
        if( mClock->GetBitState() == BIT_LOW )
            mClock->AdvanceToNextEdge(); // now we're high.
    }
}

void TdmAnalyzer::SetupForGettingFirstTdmFrame()
{
    GetNextBit( mLastDataState, mLastFrameState, mLastSample ); // we have to throw away one bit to get enough history on the FRAME line.

    for( ;; )
    {
        GetNextBit( mCurrentDataState, mCurrentFrameState, mCurrentSample );

        if( ((mSettings->mFrameSyncInverted == FS_NOT_INVERTED) && (mCurrentFrameState == BIT_HIGH) && (mLastFrameState == BIT_LOW)) ||
            ((mSettings->mFrameSyncInverted == FS_INVERTED) && (mCurrentFrameState == BIT_LOW) && (mLastFrameState == BIT_HIGH)))
        {
            if( mSettings->mBitAlignment == DSP_MODE_A )
            {
                // we need to advance to the next bit past the frame.
                mLastFrameState = mCurrentFrameState;
                mLastDataState = mCurrentDataState;
                mLastSample = mCurrentSample;

                GetNextBit( mCurrentDataState, mCurrentFrameState, mCurrentSample );
            }
            return;
        }

        mLastFrameState = mCurrentFrameState;
        mLastDataState = mCurrentDataState;
        mLastSample = mCurrentSample;
    }
}

void TdmAnalyzer::GetTdmFrame()
{
    TDM_PROFILE_SCOPE( "GetTdmFrame" );
    // on entering this function:
    // we are at the beginning of a new TDM frame
    // mCurrentFrameState and State are the values of the first bit -- that belongs to us -- in the TDM frame.
    // mLastFrameState and mLastDataState are the values from the bit just before.

    mDataBits.clear();
    mDataValidEdges.clear();
    mDataFlags.clear();

    mSlotNum = 0;

    mDataBits.push_back( mCurrentDataState );
    mDataValidEdges.push_back( mCurrentSample );
    mDataFlags.push_back( mBitFlag );

    mLastFrameState = mCurrentFrameState;
    mLastDataState = mCurrentDataState;
    mLastSample = mCurrentSample;

    for( ;; )
    {
        if( mDataBits.size() >= mSettings->mBitsPerSlot)
        {
            AnalyzeTdmSlot();
        }

        GetNextBit( mCurrentDataState, mCurrentFrameState, mCurrentSample );

        if( ((mSettings->mFrameSyncInverted == FS_NOT_INVERTED) && (mCurrentFrameState == BIT_HIGH) && (mLastFrameState == BIT_LOW)) ||
            ((mSettings->mFrameSyncInverted == FS_INVERTED) && (mCurrentFrameState == BIT_LOW) && (mLastFrameState == BIT_HIGH)))
        {

            if( mSettings->mBitAlignment == DSP_MODE_A )
            {
                // this bit belongs to us:
                mDataBits.push_back( mCurrentDataState );
                mDataValidEdges.push_back( mCurrentSample );
                mDataFlags.push_back( mBitFlag );

                // we need to advance to the next bit past the frame.
                mLastFrameState = mCurrentFrameState;
                mLastDataState = mCurrentDataState;
                mLastSample = mCurrentSample;

                AnalyzeTdmSlot();

                GetNextBit( mCurrentDataState, mCurrentFrameState, mCurrentSample );
            }
            
            mFrameNum++;

            if( mSettings->mAudioBatchSize > 0 )
            {
                mBatchFrameCount++;
                if( mBatchFrameCount >= mSettings->mAudioBatchSize )
                {
                    EmitAudioBatch();
                }
            }

            {
                TDM_PROFILE_SCOPE( "GetTdmFrame::Commit" );
                mResults->CommitResults();
                ReportProgress( mClock->GetSampleNumber() );
            }

            return;
        }

        mDataBits.push_back( mCurrentDataState );
        mDataValidEdges.push_back( mCurrentSample );
        mDataFlags.push_back( mBitFlag );

        mLastFrameState = mCurrentFrameState;
        mLastDataState = mCurrentDataState;
        mLastSample = mCurrentSample;
    }
}

void TdmAnalyzer::GetNextBit( BitState& data, BitState& frame, U64& sample_number )
{
    TDM_PROFILE_SCOPE( "GetNextBit" );
    mBitFlag = 0;

    // we enter the function with the clock state such that on the next edge is where the data is valid.
    mClock->AdvanceToNextEdge(); // R: low -> high / F: high -> low
    U64 data_valid_sample = mClock->GetSampleNumber(); // R: high / F: low

    {
        TDM_PROFILE_SCOPE( "GetNextBit::ChannelAdvance" );
        mData->AdvanceToAbsPosition( data_valid_sample );
        data = mData->GetBitState();

        mFrame->AdvanceToAbsPosition( data_valid_sample );
        frame = mFrame->GetBitState();
    }

    sample_number = data_valid_sample;

    if( mSettings->mMarkerDensity == MARKERS_ALL )
    {
        TDM_PROFILE_SCOPE( "GetNextBit::Markers" );
        if( mSettings->mEnableAdvancedAnalysis == false )
        {
            mResults->AddMarker(data_valid_sample,
                data == BIT_HIGH ? AnalyzerResults::MarkerType::One : AnalyzerResults::MarkerType::Zero,
                mSettings->mDataChannel);
        }
        mResults->AddMarker( data_valid_sample, mArrowMarker, mSettings->mClockChannel );
    }

    mClock->AdvanceToNextEdge(); // R: high -> low / F: low -> high, advance one more, so we're ready for next time this function is called.

    if ( mSettings->mEnableAdvancedAnalysis == true )
    {
        TDM_PROFILE_SCOPE( "GetNextBit::AdvancedAnalysis" );
        U64 next_clock_edge = mClock->GetSampleNumber(); // R: low / F: high
        U32 data_tranistions = mData->AdvanceToAbsPosition( next_clock_edge );
        U32 frame_transitions = mFrame->AdvanceToAbsPosition( next_clock_edge );
        U64 next_clk_edge_sample = mClock->GetSampleOfNextEdge(); // psuedo R: low -> high / F: high -> low

        if(((next_clk_edge_sample - data_valid_sample) > (U64(mDesiredBitClockPeriod) + 1)) || ((next_clk_edge_sample - data_valid_sample) < (U64(mDesiredBitClockPeriod) - 1)))
        {
            mBitFlag |= BITCLOCK_ERROR | DISPLAY_AS_ERROR_FLAG;
        }

        if((data_tranistions > 0) && ( mData->WouldAdvancingToAbsPositionCauseTransition( next_clk_edge_sample ) == true))
        {
            mResults->AddMarker(next_clock_edge, AnalyzerResults::MarkerType::Stop, mSettings->mDataChannel);
            mBitFlag |= MISSED_DATA | DISPLAY_AS_ERROR_FLAG;
        }

        if((frame_transitions > 0) && ( mFrame->WouldAdvancingToAbsPositionCauseTransition( next_clk_edge_sample ) == true))
        {
            mResults->AddMarker(next_clock_edge, AnalyzerResults::MarkerType::Stop, mSettings->mFrameChannel);
            mBitFlag |= MISSED_FRAME_SYNC | DISPLAY_AS_ERROR_FLAG;
        }
    }
}

void TdmAnalyzer::AnalyzeTdmSlot()
{
    TDM_PROFILE_SCOPE( "AnalyzeTdmSlot" );
    U64 result = 0;
    U32 starting_index = 0;
    size_t num_bits_to_process = mDataBits.size();

    // begin a new frame
    mResultsFrame.mFlags = 0;
    mResultsFrame.mStartingSampleInclusive = 0;
    mResultsFrame.mEndingSampleInclusive = 0;
    mResultsFrame.mData1 = 0;
    mResultsFrame.mData2 = 0;
    mResultsFrame.mType = 0;

    if( mSlotNum >= mSettings->mSlotsPerFrame )
    {
        mResultsFrame.mFlags |= UNEXPECTED_BITS | DISPLAY_AS_WARNING_FLAG;
    }

    if( num_bits_to_process < mSettings->mBitsPerSlot )
    {
        mResultsFrame.mFlags |= SHORT_SLOT | DISPLAY_AS_ERROR_FLAG;
    }
    else
    {
        if( mSettings->mDataAlignment == TdmDataAlignment::RIGHT_ALIGNED )
        {
            starting_index = mSettings->mBitsPerSlot - mSettings->mDataBitsPerSlot;
        }
        else // mSettings->mDataAlignment == TdmDataAlignment::LEFT_ALIGNED
        {
            num_bits_to_process = mSettings->mDataBitsPerSlot;
        }
    }

    if( num_bits_to_process - starting_index <= 1 )
    {
        mDataBits.clear();
        mDataValidEdges.clear();
        mDataFlags.clear();

        mSlotNum++;
        return;
    }

    // scan for and flag any data errors found during processing of each non-padded bit
    for( U32 i = starting_index; i < num_bits_to_process; i++)
    {
        mResultsFrame.mFlags |= mDataFlags[ i ];
    }

    if( (mResultsFrame.mFlags & SHORT_SLOT) == 0)
    {
        if( mSettings->mShiftOrder == AnalyzerEnums::LsbFirst )
        {
            U64 bit_value = 1ULL;
            for( U32 i = starting_index; i < num_bits_to_process; i++ )
            {
                if( mDataBits[ i ] == BIT_HIGH )
                    result |= bit_value;

                bit_value <<= 1;
            }
        }
        else
        {
            U64 bit_value = 1ULL << ( mSettings->mDataBitsPerSlot - 1 );
            for( U32 i = starting_index; i < num_bits_to_process; i++ )
            {
                if( mDataBits[ i ] == BIT_HIGH )
                    result |= bit_value;

                bit_value >>= 1;
            }
        }
    }
    
    mResultsFrame.mData1 = result;
    mResultsFrame.mType = U8( mSlotNum );
    mResultsFrame.mStartingSampleInclusive = mDataValidEdges[ starting_index ];
    mResultsFrame.mEndingSampleInclusive = mDataValidEdges[ num_bits_to_process - 1 ];

    if( mSettings->mMarkerDensity == MARKERS_SLOT_ONLY )
    {
        mResults->AddMarker( mResultsFrame.mStartingSampleInclusive, mArrowMarker, mSettings->mClockChannel );
    }

    {
        TDM_PROFILE_SCOPE( "AnalyzeTdmSlot::AddFrame" );
        mResults->AddFrame( mResultsFrame );
    }

    if( mSettings->mAudioBatchSize > 0 )
    {
        // Batch mode: accumulate signed PCM into buffer instead of per-slot FrameV2
        if( mSlotNum < mSettings->mSlotsPerFrame && ( mResultsFrame.mFlags & SHORT_SLOT ) == 0 )
        {
            S64 signed_value;
            if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
                signed_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
            else
                signed_value = static_cast<S64>( result );

            AccumulateSlotIntoBatch( signed_value );
        }
        // Track sample range for this batch
        if( mBatchFrameCount == 0 && mSlotNum == 0 )
        {
            mBatchStartSample = mResultsFrame.mStartingSampleInclusive;
            mBatchStartFrameNum = mFrameNum;
        }
        mBatchEndSample = mResultsFrame.mEndingSampleInclusive;
    }
    else if( mSettings->mFrameV2Detail != FV2_OFF )
    {
        TDM_PROFILE_SCOPE( "AnalyzeTdmSlot::FrameV2" );
        // Must use a fresh FrameV2 each frame: the SDK's Add* methods append
        // (not overwrite), and FrameV2 has no Clear/Reset method. Reusing a
        // member caused O(N^2) memory growth and OOM crashes.
        FrameV2 frame_v2;
        frame_v2.AddInteger( "slot", mResultsFrame.mType );
        S64 adjusted_value = result;
        if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
        {
            adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
        }
        frame_v2.AddInteger( "data", adjusted_value );
        frame_v2.AddInteger( "frame_number", mFrameNum );

        bool is_short_slot = (mResultsFrame.mFlags & SHORT_SLOT) != 0;
        bool is_bitclock_error = (mResultsFrame.mFlags & BITCLOCK_ERROR) != 0;
        frame_v2.AddBoolean( "short_slot", is_short_slot );
        frame_v2.AddBoolean( "bitclock_error", is_bitclock_error );

        if( mSettings->mFrameV2Detail == FV2_FULL )
        {
            bool is_extra_slot = (mResultsFrame.mFlags & UNEXPECTED_BITS) != 0;
            bool is_missed_data = (mResultsFrame.mFlags & MISSED_DATA) != 0;
            bool is_missed_frame_sync = (mResultsFrame.mFlags & MISSED_FRAME_SYNC) != 0;

            const char* severity;
            if( is_short_slot || is_bitclock_error || is_missed_data || is_missed_frame_sync )
                severity = "error";
            else if( is_extra_slot || mLowSampleRate )
                severity = "warning";
            else
                severity = "ok";

            frame_v2.AddString( "severity", severity );
            frame_v2.AddBoolean( "extra_slot", is_extra_slot );
            frame_v2.AddBoolean( "missed_data", is_missed_data );
            frame_v2.AddBoolean( "missed_frame_sync", is_missed_frame_sync );
            frame_v2.AddBoolean( "low_sample_rate", mLowSampleRate );
        }

        mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
    }

    mDataBits.clear();
    mDataValidEdges.clear();
    mDataFlags.clear();

    mSlotNum++;
}

void TdmAnalyzer::AccumulateSlotIntoBatch( S64 signed_value )
{
    U32 offset = mBatchFrameCount * mBatchBytesPerFrame
               + mSlotNum * mBatchBytesPerSample;

    // Write little-endian, truncated to mBatchBytesPerSample bytes
    U64 raw = static_cast<U64>( signed_value );
    for( U32 i = 0; i < mBatchBytesPerSample; i++ )
    {
        mBatchBuffer[ offset + i ] = static_cast<U8>( raw & 0xFF );
        raw >>= 8;
    }
}

void TdmAnalyzer::EmitAudioBatch()
{
    if( mBatchFrameCount == 0 )
        return;

    FrameV2 batch_fv2;
    batch_fv2.AddByteArray( "pcm_data", mBatchBuffer.data(),
                             U64( mBatchFrameCount ) * mBatchBytesPerFrame );
    batch_fv2.AddInteger( "num_frames", mBatchFrameCount );
    batch_fv2.AddInteger( "channels", mSettings->mSlotsPerFrame );
    batch_fv2.AddInteger( "bit_depth", mSettings->mDataBitsPerSlot );
    batch_fv2.AddInteger( "sample_rate", mSettings->mTdmFrameRate );
    batch_fv2.AddInteger( "start_frame_number", S64( mBatchStartFrameNum ) );

    mResults->AddFrameV2( batch_fv2, "audio_batch",
                           mBatchStartSample, mBatchEndSample );

    mBatchFrameCount = 0;
}

U32 TdmAnalyzer::GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels )
{
    if( mSimulationInitilized == false )
    {
        mSimulationDataGenerator.Initialize( GetSimulationSampleRate(), mSettings.get() );
        mSimulationInitilized = true;
    }

    return mSimulationDataGenerator.GenerateSimulationData( newest_sample_requested, sample_rate, simulation_channels );
}

U32 TdmAnalyzer::GetMinimumSampleRateHz()
{
    
    return mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate * 4;
}

bool TdmAnalyzer::NeedsRerun()
{
    return false;
}

const char* TdmAnalyzer::GetAnalyzerName() const
{
    return "TDM";
}

const char* GetAnalyzerName()
{
    return "TDM";
}

Analyzer* CreateAnalyzer()
{
    return new TdmAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
    delete analyzer;
}
