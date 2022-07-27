#include "TdmAnalyzer.h"
#include "TdmAnalyzerSettings.h"
#include <AnalyzerChannelData.h>

TdmAnalyzer::TdmAnalyzer() 
  : Analyzer2(), 
    mSettings( new TdmAnalyzerSettings() ),
    mSimulationInitilized( false )
{
    SetAnalyzerSettings( mSettings.get() );
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

    SetupForGettingFirstBit();
    SetupForGettingFirstTdmFrame();

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
            if( mSettings->mBitAlignment == BITS_SHIFTED_RIGHT_1 )
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
            if( mSettings->mEnableAdvancedAnalysis )
            {
                mResults->AddMarker(mCurrentSample, AnalyzerResults::MarkerType::Start, mSettings->mFrameChannel);
            }
            
            if( mSettings->mBitAlignment == BITS_SHIFTED_RIGHT_1 )
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
    mBitFlag = 0;
    
    // we enter the function with the clock state such that on the next edge is where the data is valid.
    mClock->AdvanceToNextEdge(); // low -> high
    U64 data_valid_sample = mClock->GetSampleNumber(); // high

    mData->AdvanceToAbsPosition( data_valid_sample );
    data = mData->GetBitState();

    mFrame->AdvanceToAbsPosition( data_valid_sample );
    frame = mFrame->GetBitState();

    if( mSettings->mEnableAdvancedAnalysis == false)
    {
        mResults->AddMarker(data_valid_sample,
        data == BIT_HIGH ? AnalyzerResults::MarkerType::One : AnalyzerResults::MarkerType::Zero, 
        mSettings->mDataChannel);
    }

    sample_number = data_valid_sample;

    mResults->AddMarker( data_valid_sample, mArrowMarker, mSettings->mClockChannel );
    mClock->AdvanceToNextEdge(); // high -> low, advance one more, so we're ready for next time this function is called.

    if ( mSettings->mEnableAdvancedAnalysis == true )
    {
        U64 next_clock_edge = mClock->GetSampleNumber(); // low
        U32 data_tranistions = mData->AdvanceToAbsPosition( next_clock_edge );
        U32 frame_transitions = mFrame->AdvanceToAbsPosition( next_clock_edge );
        U64 next_clk_edge_sample = mClock->GetSampleOfNextEdge(); // psuedo low -> high

        mResultsFrame.mData2 = U64(mDesiredBitClockPeriod); // mSampleRate;

        if(((next_clk_edge_sample - data_valid_sample) > (U64(mDesiredBitClockPeriod) + 1)) || ((next_clk_edge_sample - data_valid_sample) < (U64(mDesiredBitClockPeriod) - 1)))
        {
            mResultsFrame.mData2 = (next_clk_edge_sample - data_valid_sample);
            mBitFlag |= BITCLOCK_ERROR | DISPLAY_AS_ERROR_FLAG;
        }

        
        if((data_tranistions > 0) && ( mData->WouldAdvancingToAbsPositionCauseTransition( next_clk_edge_sample ) == true))
        {
            mResults->AddMarker(next_clock_edge, AnalyzerResults::MarkerType::ErrorSquare, mSettings->mDataChannel);
            mBitFlag |= MISSED_DATA | DISPLAY_AS_ERROR_FLAG;
        }

        if((frame_transitions > 0) && ( mFrame->WouldAdvancingCauseTransition( next_clk_edge_sample ) == true))
        {
            mResults->AddMarker(next_clock_edge, AnalyzerResults::MarkerType::ErrorSquare, mSettings->mFrameChannel);
            mBitFlag |= MISSED_FRAME_SYNC | DISPLAY_AS_ERROR_FLAG;
        }
    }
}

void TdmAnalyzer::AnalyzeTdmSlot()
{
    U64 result = 0;
    U32 starting_index = 0;
    U32 num_bits_to_process = mDataBits.size();

    // begin a new frame
    mResultsFrame.mFlags = 0;
    mResultsFrame.mStartingSampleInclusive = 0;
    mResultsFrame.mEndingSampleInclusive = 0;
    mResultsFrame.mData1 = 0;
    //mResultsFrame.mData2 = 0;
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
            starting_index = mSettings->mBitsPerSlot - mSettings->mDataBitsPerSlot - 1;
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

    if( mResultsFrame.mFlags & SHORT_SLOT == 0)
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
    mResults->AddFrame( mResultsFrame );

    FrameV2 frame_v2;
    char error_str[ 80 ] = "";
    char warning_str[ 32 ] = "";

    frame_v2.AddInteger( "channel", mResultsFrame.mType );
    S64 adjusted_value = result;
    if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
    {
        adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
    }
    frame_v2.AddInteger( "data", adjusted_value );
    
    if(mResultsFrame.mFlags & SHORT_SLOT)
    {
        sprintf(error_str, "E: Short Slot ");
    }
    if(mResultsFrame.mFlags & MISSED_DATA)
    {
        sprintf(error_str + strlen(error_str), "E: Data Error ");
    }
    if(mResultsFrame.mFlags & MISSED_FRAME_SYNC)
    {
        sprintf(error_str + strlen(error_str), "E: Frame Sync Missed ");
    }
    if(mResultsFrame.mFlags & BITCLOCK_ERROR)
    {
        sprintf(error_str + strlen(error_str), "E: Bitclock Error ");
    }

    if(mResultsFrame.mFlags & UNEXPECTED_BITS)
    {
        sprintf(warning_str, "W: Extra Slot ");
    }

    frame_v2.AddString("errors", error_str);
    frame_v2.AddString("warnings", warning_str);
    frame_v2.AddInteger("data2", mResultsFrame.mData2);
    mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
    
    mResults->CommitResults();
    ReportProgress( mClock->GetSampleNumber() );

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
