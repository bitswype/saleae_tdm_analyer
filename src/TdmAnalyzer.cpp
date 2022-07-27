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

    SetupForGettingFirstBit();
    SetupForGettingFirstTdmFrame();

    for( ;; )
    {
        GetTdmFrame();
        AnalyzeTdmFrame();

        mResults->CommitResults();
        ReportProgress( mClock->GetSampleNumber() );
        CheckIfThreadShouldExit();
    }
}

void TdmAnalyzer::AnalyzeTdmFrame()
{
    U32 num_bits = mDataBits.size();

    U32 num_slots = mSettings->mSlotsPerFrame;
    U32 bits_per_slot = mSettings->mBitsPerSlot;
    U32 num_audio_bits = mSettings->mDataBitsPerSlot;

    mResultsFrame.mStartingSampleInclusive = 0;
    mResultsFrame.mEndingSampleInclusive = 0;
    mResultsFrame.mData1 = 0;
    mResultsFrame.mData2 = 0;
    mResultsFrame.mType = 0;
    mResultsFrame.mFlags = 0;

    if( num_bits < (num_slots * bits_per_slot))
    {
        //Frame frame;
        //frame.mData1 = num_bits;
        //frame.mType = U8( ErrorTooFewBitsPerFrame );
        mResultsFrame.mFlags |= TOO_FEW_BITS_PER_FRAME | DISPLAY_AS_ERROR_FLAG;
        //frame.mStartingSampleInclusive = mDataValidEdges.front();
        //frame.mEndingSampleInclusive = mDataValidEdges.back();
        //mResults->AddFrame( frame );
        //FrameV2 frame_v2;
        //frame_v2.AddString( "error", "invalid number of bits in frame" );
        //mResults->AddFrameV2( frame_v2, "error", frame.mStartingSampleInclusive, frame.mEndingSampleInclusive );
        //return;
    }
    
    U32 num_unused_bits = bits_per_slot - num_audio_bits;
    U32 starting_offset;

    if( mSettings->mDataAlignment == LEFT_ALIGNED )
        starting_offset = 0;
    else
        starting_offset = num_unused_bits;

    U32 i = 0;
    while( AnalyzeTdmSlot( i * bits_per_slot + starting_offset, num_audio_bits, i ) )
    {
        i++;
    }
}

bool TdmAnalyzer::AnalyzeTdmSlot( U32 starting_index, U32 num_audio_bits, U32 subframe_index )
{
    U64 result = 0;
    U32 target_count = starting_index + num_audio_bits;

    mResultsFrame.mFlags = 0;

    if( starting_index >= mDataBits.size() )
    {
        // case where there is enough bits of padding, but no data
        // case where there is no data
        return false;
    }

    // verify we have enough data to process a complete slot
    if( (mDataBits.size() - (starting_index + num_audio_bits)) < 0)
    {
        // how many bits can we use?
        target_count = starting_index + (mDataBits.size() - starting_index);
        mResultsFrame.mFlags |= SHORT_SLOT | DISPLAY_AS_ERROR_FLAG;
        return false;
    }

    for( U32 i = starting_index; i < target_count; i++)
    {
        mResultsFrame.mFlags |= mDataFlags[ i ];
    }

    if( mSettings->mShiftOrder == AnalyzerEnums::LsbFirst )
    {
        U64 bit_value = 1ULL;
        for( U32 i = starting_index; i < target_count; i++ )
        {
            if( mDataBits[ i ] == BIT_HIGH )
                result |= bit_value;

            bit_value <<= 1;
        }
    }
    else
    {
        U64 bit_value = 1ULL << ( num_audio_bits - 1 );
        for( U32 i = starting_index; i < target_count; i++ )
        {
            if( mDataBits[ i ] == BIT_HIGH )
                result |= bit_value;

            bit_value >>= 1;
        }
    }

    // enum TdmResultType { Channel1, Channel2, ErrorTooFewBits, ErrorDoesntDivideEvenly };
    // add result bubble
    
    mResultsFrame.mData1 = result;
    mResultsFrame.mType = U8( subframe_index );

    if( subframe_index >= mSettings->mSlotsPerFrame )
    {
        mResultsFrame.mFlags |= UNEXPECTED_BITS | DISPLAY_AS_WARNING_FLAG;
    }
    mResultsFrame.mStartingSampleInclusive = mDataValidEdges[ starting_index ];
    mResultsFrame.mEndingSampleInclusive = mDataValidEdges[ target_count - 1 ];
    mResults->AddFrame( mResultsFrame );
    FrameV2 frame_v2;
    frame_v2.AddInteger( "channel", mResultsFrame.mType );
    S64 adjusted_value = result;
    if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
    {
        adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
    }
    frame_v2.AddInteger( "data", adjusted_value );
    mResults->AddFrameV2( frame_v2, "data", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );

    return true;
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
    // mCurrentFrameState and State are the values of the first bit -- that belongs to us -- in the frame.
    // mLastFrameState and mLastDataState are the values from the bit just before.

    mDataBits.clear();
    mDataValidEdges.clear();
    mDataFlags.clear();


    mDataBits.push_back( mCurrentDataState );
    mDataValidEdges.push_back( mCurrentSample );
    mDataFlags.push_back( mBitFlag );

    mLastFrameState = mCurrentFrameState;
    mLastDataState = mCurrentDataState;
    mLastSample = mCurrentSample;

    for( ;; )
    {
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

void TdmAnalyzer::GetNextBit( BitState& data, BitState& frame, U64& sample_number )
{
    mBitFlag = 0;
    
    // we enter the function with the clock state such that on the next edge is where the data is valid.
    mClock->AdvanceToNextEdge();
    U64 data_valid_sample = mClock->GetSampleNumber();

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
    mClock->AdvanceToNextEdge(); // advance one more, so we're ready for next time this function is called.

    if ( mSettings->mEnableAdvancedAnalysis == true )
    {
        U64 next_clock_edge = mClock->GetSampleNumber();
        U32 data_tranistions = mData->AdvanceToAbsPosition( next_clock_edge );
        U32 frame_transitions = mFrame->AdvanceToAbsPosition( next_clock_edge );

        U64 next_clk_edge_sample = mClock->GetSampleOfNextEdge();
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
    return 4000000; // just enough for our simulation.  Ideally we would be smarter about this but we don't know the bit rate in advance.
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
