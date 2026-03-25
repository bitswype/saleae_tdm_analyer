#include "TdmAnalyzer.h"
#include "TdmAnalyzerSettings.h"
#include "TdmProfiler.h"
#include <AnalyzerChannelData.h>
#include <stdio.h>

TdmAnalyzer::TdmAnalyzer()
  : Analyzer2(),
    mSettings( new TdmAnalyzerSettings() ),
    mSimulationInitilized( false ),
    mLowSampleRate( false )
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

    SetupForGettingFirstBit();
    SetupForGettingFirstTdmFrame();
    mFrameNum = 0;

    for( ;; )
    {
        GetTdmFrame();
        CheckIfThreadShouldExit();
    }
}

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

    {
        TDM_PROFILE_SCOPE( "GetNextBit::Markers" );
        if( mSettings->mEnableAdvancedAnalysis == false)
        {
            mResults->AddMarker(data_valid_sample,
            data == BIT_HIGH ? AnalyzerResults::MarkerType::One : AnalyzerResults::MarkerType::Zero,
            mSettings->mDataChannel);
        }

        sample_number = data_valid_sample;

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

    {
        TDM_PROFILE_SCOPE( "AnalyzeTdmSlot::AddFrame" );
        mResults->AddFrame( mResultsFrame );
    }

    {
        TDM_PROFILE_SCOPE( "AnalyzeTdmSlot::FrameV2" );
        FrameV2 frame_v2;

        frame_v2.AddInteger( "slot", mResultsFrame.mType );
        S64 adjusted_value = result;
        if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
        {
            adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
        }
        frame_v2.AddInteger( "data", adjusted_value );
        frame_v2.AddInteger( "frame_number", mFrameNum );

        bool is_short_slot      = (mResultsFrame.mFlags & SHORT_SLOT) != 0;
        bool is_extra_slot      = (mResultsFrame.mFlags & UNEXPECTED_BITS) != 0;
        bool is_bitclock_error  = (mResultsFrame.mFlags & BITCLOCK_ERROR) != 0;
        bool is_missed_data     = (mResultsFrame.mFlags & MISSED_DATA) != 0;
        bool is_missed_frame_sync = (mResultsFrame.mFlags & MISSED_FRAME_SYNC) != 0;

        const char* severity;
        if( is_short_slot || is_bitclock_error || is_missed_data || is_missed_frame_sync )
            severity = "error";
        else if( is_extra_slot || mLowSampleRate )
            severity = "warning";
        else
            severity = "ok";

        frame_v2.AddString( "severity", severity );
        frame_v2.AddBoolean( "short_slot", is_short_slot );
        frame_v2.AddBoolean( "extra_slot", is_extra_slot );
        frame_v2.AddBoolean( "bitclock_error", is_bitclock_error );
        frame_v2.AddBoolean( "missed_data", is_missed_data );
        frame_v2.AddBoolean( "missed_frame_sync", is_missed_frame_sync );
        frame_v2.AddBoolean( "low_sample_rate", mLowSampleRate );
        mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
    }

    {
        TDM_PROFILE_SCOPE( "AnalyzeTdmSlot::Commit" );
        mResults->CommitResults();
        ReportProgress( mClock->GetSampleNumber() );
    }

    mDataBits.clear();
    mDataValidEdges.clear();
    mDataFlags.clear();

    mSlotNum++;
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
