#include "TdmSimulationDataGenerator.h"
#include <math.h>

#include <algorithm>
#include <fstream>
#include <iostream>

TdmSimulationDataGenerator::TdmSimulationDataGenerator()
    : mAudioSampleRate( 10000 )
{
}

TdmSimulationDataGenerator::~TdmSimulationDataGenerator()
{
}

void TdmSimulationDataGenerator::Initialize( U32 simulation_sample_rate, TdmAnalyzerSettings* settings )
{
    mSimulationSampleRateHz = simulation_sample_rate;
    mSettings = settings;

    mNumSlots = mSettings->mSlotsPerFrame;
    mBitsPerSlot = mSettings->mBitsPerSlot;
    mDataBitsPerSlot = mSettings->mDataBitsPerSlot;

    mShiftOrder = mSettings->mShiftOrder;
    mDataValidEdge = mSettings->mDataValidEdge;

    mDataAlignment = mSettings->mDataAlignment;
    mBitAlignment = mSettings->mBitAlignment;
    mSigned = mSettings->mSigned;
    mFrameSyncInverted = mSettings->mFrameSyncInverted;

    if( mDataValidEdge == AnalyzerEnums::NegEdge )
        mClock = mSimulationChannels.Add( mSettings->mClockChannel, mSimulationSampleRateHz, BIT_LOW );
    else
        mClock = mSimulationChannels.Add( mSettings->mClockChannel, mSimulationSampleRateHz, BIT_HIGH );

    mFrame = mSimulationChannels.Add( mSettings->mFrameChannel, mSimulationSampleRateHz, mFrameSyncInverted == TdmFrameSelectInverted::FS_NOT_INVERTED ? BIT_LOW : BIT_HIGH );
    mData = mSimulationChannels.Add( mSettings->mDataChannel, mSimulationSampleRateHz, BIT_LOW );

    mVecCountGen.clear();
    mVecSineGen.clear();

    for(U32 i = 0; i < mNumSlots; i++)
    {
        mVecCountGen.push_back(std::unique_ptr<CountGen>(new CountGen(i, U64(pow(2, mDataBitsPerSlot)))));
    }

    for(U32 i = 0; i < mNumSlots; i++)
    {
        mVecSineGen.push_back(std::unique_ptr<SineGen>(new SineGen(mAudioSampleRate, i*100.0, (1<<(mDataBitsPerSlot-2))-1, 0)));
    }

    double bits_per_s = mAudioSampleRate * double(2.0 * mNumSlots * (mBitsPerSlot) );
    mClockGenerator.Init( bits_per_s, mSimulationSampleRateHz );

    mCurrentAudioChannel = 0;
    mPaddingCount = 0;

    if( mShiftOrder == AnalyzerEnums::MsbFirst )
    {
        U64 mask = 1ULL << ( mDataBitsPerSlot - 1 );
        for( U32 i = 0; i < mDataBitsPerSlot; i++ )
        {
            mBitMasks.push_back( mask );
            mask = mask >> 1;
        }
    }
    else
    {
        U64 mask = 1;
        for( U32 i = 0; i < mDataBitsPerSlot; i++ )
        {
            mBitMasks.push_back( mask );
            mask = mask << 1;
        }
    }

    mFrameBits.push_back( mFrameSyncInverted == TdmFrameSelectInverted::FS_NOT_INVERTED ? BIT_HIGH : BIT_LOW);
    for( U32 i = 1; i < ( mNumSlots * mBitsPerSlot); i++ )
        mFrameBits.push_back(  mFrameSyncInverted == TdmFrameSelectInverted::FS_NOT_INVERTED ? BIT_LOW : BIT_HIGH);

    // add some padding to the beginning of the simulation so that the analyzer can catch the first FS pulse
    for(U32 i = 0; i < 16; i++)
    {
        WriteBit(BIT_LOW, mFrameSyncInverted == TdmFrameSelectInverted::FS_NOT_INVERTED ? BIT_LOW : BIT_HIGH);
    }

    mCurrentWord = GetNextAudioWord();
    mCurrentBitIndex = 0;
    mCurrentFrameBitIndex = 0;
    mBitGenerationState = Init;
}

U32 TdmSimulationDataGenerator::GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate,
                                                        SimulationChannelDescriptor** simulation_channels )
{
    U64 adjusted_largest_sample_requested =
        AnalyzerHelpers::AdjustSimulationTargetSample( newest_sample_requested, sample_rate, mSimulationSampleRateHz );

    while( mData->GetCurrentSampleNumber() < adjusted_largest_sample_requested )
    {
        WriteBit( GetNextAudioBit(), GetNextFrameBit() );
    }

    *simulation_channels = mSimulationChannels.GetArray();
    return mSimulationChannels.GetCount();
}


BitState TdmSimulationDataGenerator::GetNextFrameBit()
{
    BitState bit_state = mFrameBits[ mCurrentFrameBitIndex ];
    mCurrentFrameBitIndex++;
    if( mCurrentFrameBitIndex >= mFrameBits.size() )
        mCurrentFrameBitIndex = 0;
    return bit_state;
}

// enum BitGenerationState { Init, LeftPadding, Data, RightPadding };
BitState TdmSimulationDataGenerator::GetNextAudioBit()
{
    switch( mBitGenerationState )
    {
    case Init:
        if( mBitAlignment == BITS_SHIFTED_RIGHT_1 )
        {
            mBitGenerationState = LeftPadding;
            return BIT_LOW; // just once, we'll insert a 1-bit offset.
        }
        else
        {
            mBitGenerationState = LeftPadding;
            return GetNextAudioBit();
        }
        break;
    case LeftPadding:
        if( mDataAlignment == RIGHT_ALIGNED )
        {
            if( mPaddingCount < (mBitsPerSlot - mDataBitsPerSlot) )
            {
                mPaddingCount++;
                return BIT_LOW;
            }
            else
            {
                mBitGenerationState = Data;
                mPaddingCount = 0;
                return GetNextAudioBit();
            }
        }
        else
        {
            mBitGenerationState = Data;
            return GetNextAudioBit();
        }
        break;
    case Data:
        if( mCurrentBitIndex == mDataBitsPerSlot )
        {
            mCurrentBitIndex = 0;
            mCurrentWord = GetNextAudioWord();
            mBitGenerationState = RightPadding;
            return GetNextAudioBit();
        }
        else
        {
            BitState bit_state;

            if( ( mCurrentWord & mBitMasks[ mCurrentBitIndex ] ) == 0 )
                bit_state = BIT_LOW;
            else
                bit_state = BIT_HIGH;

            mCurrentBitIndex++;
            return bit_state;
        }
        break;
    case RightPadding:
        if( mDataAlignment == LEFT_ALIGNED )
        {
            if( mPaddingCount < (mBitsPerSlot - mDataBitsPerSlot) )
            {
                mPaddingCount++;
                return BIT_LOW;
            }
            else
            {
                mBitGenerationState = Data;
                mPaddingCount = 0;
                return GetNextAudioBit();
            }
        }
        else
        {
            mBitGenerationState = LeftPadding;
            return GetNextAudioBit();
        }
        break;
    default:
        AnalyzerHelpers::Assert( "unexpected" );
        return BIT_LOW; // eliminate warning
        break;
    }
}

S64 TdmSimulationDataGenerator::GetNextAudioWord()
{
    U64 value;

    value = mVecCountGen[mCurrentAudioChannel]->GetNextValue();
    mCurrentAudioChannel = ++mCurrentAudioChannel % mNumSlots;

    return S64(value);
}

inline void TdmSimulationDataGenerator::WriteBit( BitState data, BitState frame )
{
    // start 'low', pause 1/2 period:
    mSimulationChannels.AdvanceAll( mClockGenerator.AdvanceByHalfPeriod( 1.0 ) );

    //'posedge' on clock, write update data lines:
    mClock->Transition();

    mFrame->TransitionIfNeeded( frame );

    mData->TransitionIfNeeded( data );

    mSimulationChannels.AdvanceAll( mClockGenerator.AdvanceByHalfPeriod( 1.0 ) );

    //'negedge' on clock, data is valid.
    mClock->Transition();
}

SineGen::SineGen(double sample_rate, double frequency_hz, double scale, double phase_degrees)
    : mSampleRate( sample_rate ),
      mFrequency( frequency_hz ),
      mScale( scale ),
      mPhase( phase_degrees ),
      mSample( 0 )
{
}

SineGen::~SineGen()
{
}

double SineGen::GetNextValue()
{
    double t = double( mSample++ ) / double( mSampleRate );
    return mScale * sin(t * mFrequency * (2.0 * M_PI) + (mPhase / 180.0 * M_PI));
}

void SineGen::Reset()
{
    mSample = 0;
}

CountGen::CountGen(U64 start_val, U64 max_val)
  : mVal( start_val ),
    mMaxVal( max_val )
{
};

CountGen::~CountGen()
{
};

U64 CountGen::GetNextValue()
{
    U64 val = mVal++;
    mVal = mVal % mMaxVal;
    return val;
};

void CountGen::Reset()
{
    mVal = 0;
};

StaticGen::StaticGen(U64 val)
  : mVal( val )
{
};

StaticGen::~StaticGen()
{
};

U64 StaticGen::GetNextValue()
{
    return (mVal);
};

void StaticGen::Reset()
{
};
