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
    SetupForGettingFirstFrame();

    for( ;; )
    {
        GetFrame();
        AnalyzeFrame();

        mResults->CommitResults();
        ReportProgress( mClock->GetSampleNumber() );
        CheckIfThreadShouldExit();
    }
}

void TdmAnalyzer::AnalyzeFrame()
{
    U32 num_bits = mDataBits.size();

    U32 num_slots = mSettings->mSlotsPerFrame;

    if( ( num_bits % num_slots ) != 0 )
    {
        Frame frame;
        frame.mData1 = num_bits;
        frame.mType = U8( ErrorDoesntDivideEvenly );
        frame.mFlags = DISPLAY_AS_ERROR_FLAG;
        frame.mStartingSampleInclusive = mDataValidEdges.front();
        frame.mEndingSampleInclusive = mDataValidEdges.back();
        mResults->AddFrame( frame );
        FrameV2 frame_v2;
        frame_v2.AddString( "error", "invalid number of bits in frame" );
        mResults->AddFrameV2( frame_v2, "error", frame.mStartingSampleInclusive, frame.mEndingSampleInclusive );
        return;
    }

    U32 bits_per_slot = num_bits / num_slots;
    U32 num_audio_bits = mSettings->mDataBitsPerSlot;

    if( bits_per_slot < num_audio_bits )
    {
        // enum TdmResultType { Channel1, Channel2, ErrorTooFewBits, ErrorDoesntDivideEvenly };
        Frame frame;
        frame.mData1 = num_audio_bits;
        frame.mData2 = bits_per_slot;
        frame.mType = U8( ErrorTooFewBits );
        frame.mFlags = DISPLAY_AS_ERROR_FLAG;
        frame.mStartingSampleInclusive = mDataValidEdges.front();
        frame.mEndingSampleInclusive = mDataValidEdges.back();
        mResults->AddFrame( frame );
        FrameV2 frame_v2;
        frame_v2.AddString( "error", "not enough bits in slot" );
        mResults->AddFrameV2( frame_v2, "error", frame.mStartingSampleInclusive, frame.mEndingSampleInclusive );
        return;
    }

    U32 num_unused_bits = bits_per_slot - num_audio_bits;
    U32 starting_offset;

    if( mSettings->mDataAlignment == LEFT_ALIGNED )
        starting_offset = 0;
    else
        starting_offset = num_unused_bits;

    for( U32 i = 0; i < num_slots; i++ )
    {
        AnalyzeSubFrame( i * bits_per_slot + starting_offset, num_audio_bits, i );
    }
}

void TdmAnalyzer::AnalyzeSubFrame( U32 starting_index, U32 num_audio_bits, U32 subframe_index )
{
    U64 result = 0;
    U32 target_count = starting_index + num_audio_bits;

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
    Frame frame;
    frame.mData1 = result;
    
    frame.mType = U8( subframe_index);

    frame.mFlags = 0;
    frame.mStartingSampleInclusive = mDataValidEdges[ starting_index ];
    frame.mEndingSampleInclusive = mDataValidEdges[ starting_index + num_audio_bits - 1 ];
    mResults->AddFrame( frame );
    FrameV2 frame_v2;
    frame_v2.AddInteger( "channel", frame.mType );
    S64 adjusted_value = result;
    if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
    {
        adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mDataBitsPerSlot );
    }
    frame_v2.AddInteger( "data", adjusted_value );
    mResults->AddFrameV2( frame_v2, "data", frame.mStartingSampleInclusive, frame.mEndingSampleInclusive );
}

void TdmAnalyzer::SetupForGettingFirstFrame()
{
    GetNextBit( mLastData, mLastFrame, mLastSample ); // we have to throw away one bit to get enough history on the FRAME line.

    for( ;; )
    {
        GetNextBit( mCurrentData, mCurrentFrame, mCurrentSample );

        if( ((mSettings->mFrameSyncInverted == FS_NOT_INVERTED) && (mCurrentFrame == BIT_HIGH) && (mLastFrame == BIT_LOW)) ||
            ((mSettings->mFrameSyncInverted == FS_INVERTED) && (mCurrentFrame == BIT_LOW) && (mLastFrame == BIT_HIGH)))
        {
            if( mSettings->mBitAlignment == BITS_SHIFTED_RIGHT_1 )
            {
                // we need to advance to the next bit past the frame.
                mLastFrame = mCurrentFrame;
                mLastData = mCurrentData;
                mLastSample = mCurrentSample;

                GetNextBit( mCurrentData, mCurrentFrame, mCurrentSample );
            }
            return;
        }

        mLastFrame = mCurrentFrame;
        mLastData = mCurrentData;
        mLastSample = mCurrentSample;
    }
}

void TdmAnalyzer::GetFrame()
{
    // on entering this function:
    // mCurrentFrame and mCurrentData are the values of the first bit -- that belongs to us -- in the frame.
    // mLastFrame and mLastData are the values from the bit just before.

    mDataBits.clear();
    mDataValidEdges.clear();


    mDataBits.push_back( mCurrentData );
    mDataValidEdges.push_back( mCurrentSample );

    mLastFrame = mCurrentFrame;
    mLastData = mCurrentData;
    mLastSample = mCurrentSample;

    for( ;; )
    {
        GetNextBit( mCurrentData, mCurrentFrame, mCurrentSample );

        if( ((mSettings->mFrameSyncInverted == FS_NOT_INVERTED) && (mCurrentFrame == BIT_HIGH) && (mLastFrame == BIT_LOW)) ||
            ((mSettings->mFrameSyncInverted == FS_INVERTED) && (mCurrentFrame == BIT_LOW) && (mLastFrame == BIT_HIGH)))
        {
            if( mSettings->mEnableAdvancedAnalysis )
            {
                mResults->AddMarker(mCurrentSample, AnalyzerResults::MarkerType::Start, mSettings->mFrameChannel);
            }
            
            if( mSettings->mBitAlignment == BITS_SHIFTED_RIGHT_1 )
            {
                // this bit belongs to us:
                mDataBits.push_back( mCurrentData );
                mDataValidEdges.push_back( mCurrentSample );

                // we need to advance to the next bit past the frame.
                mLastFrame = mCurrentFrame;
                mLastData = mCurrentData;
                mLastSample = mCurrentSample;

                GetNextBit( mCurrentData, mCurrentFrame, mCurrentSample );
            }

            return;
        }

        mDataBits.push_back( mCurrentData );
        mDataValidEdges.push_back( mCurrentSample );

        mLastFrame = mCurrentFrame;
        mLastData = mCurrentData;
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
    // we always start off here so that the next edge is where the data is valid.
    mClock->AdvanceToNextEdge();
    U64 data_valid_sample = mClock->GetSampleNumber();

    U32 data_transitions = mData->AdvanceToAbsPosition( data_valid_sample );
    data = mData->GetBitState();

    if(data_transitions > 1 && mSettings->mEnableAdvancedAnalysis )
    {
        mResults->AddMarker(data_valid_sample, AnalyzerResults::MarkerType::ErrorSquare, mSettings->mFrameChannel);
    }

    U32 frame_transitions = mFrame->AdvanceToAbsPosition( data_valid_sample );
    frame = mFrame->GetBitState();

    if(frame_transitions > 1 && mSettings->mEnableAdvancedAnalysis )
    {
        mResults->AddMarker(data_valid_sample, AnalyzerResults::MarkerType::ErrorX, mSettings->mFrameChannel);
    }

    sample_number = data_valid_sample;

    mResults->AddMarker( data_valid_sample, mArrowMarker, mSettings->mClockChannel );
    if( mSettings->mEnableAdvancedAnalysis )
    {
        mResults->AddMarker(data_valid_sample,
        data == BIT_HIGH ? AnalyzerResults::MarkerType::One : AnalyzerResults::MarkerType::Zero, 
        mSettings->mDataChannel);
    }

    mClock->AdvanceToNextEdge(); // advance one more, so we're ready for next time this function is called.
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
