#include "TdmSimulationDataGenerator.h"
#include <math.h>

#include <algorithm>
#include <fstream>
#include <iostream>

TdmSimulationDataGenerator::TdmSimulationDataGenerator()
    : mNumPaddingBits( 0 ),
      /*mAudioSampleRate( 44000*2 ),*/
      mAudioSampleRate( 10000 ),
      mUseShortFrames( false )
{
}

TdmSimulationDataGenerator::~TdmSimulationDataGenerator()
{
}

void TdmSimulationDataGenerator::Initialize( U32 simulation_sample_rate, TdmAnalyzerSettings* settings )
{
    mSimulationSampleRateHz = simulation_sample_rate;
    mSettings = settings;

    if( mSettings->mDataValidEdge == AnalyzerEnums::NegEdge )
        mClock = mSimulationChannels.Add( mSettings->mClockChannel, mSimulationSampleRateHz, BIT_LOW );
    else
        mClock = mSimulationChannels.Add( mSettings->mClockChannel, mSimulationSampleRateHz, BIT_HIGH );

    mFrame = mSimulationChannels.Add( mSettings->mFrameChannel, mSimulationSampleRateHz, BIT_LOW );
    mData = mSimulationChannels.Add( mSettings->mDataChannel, mSimulationSampleRateHz, BIT_LOW );

    mNumSlots = mSettings->mSlotsPerFrame;

    mSineWaveSamplesLeft = std::unique_ptr<SineGen>(new SineGen(mAudioSampleRate, 100.0, 0.99, 0));
    mSineWaveSamplesRight = std::unique_ptr<SineGen>(new SineGen(mAudioSampleRate, 200.0, 0.99, 0));

    double bits_per_s = mAudioSampleRate * double(2.0 * mNumSlots * (mSettings->mDataBitsPerSlot + mNumPaddingBits) );
    mClockGenerator.Init( bits_per_s, mSimulationSampleRateHz );

    mCurrentAudioChannel = Left;
    mPaddingCount = 0;

    U64 audio_bit_depth = mSettings->mDataBitsPerSlot;

    if( mSettings->mShiftOrder == AnalyzerEnums::MsbFirst )
    {
        U64 mask = 1ULL << ( audio_bit_depth - 1 );
        for( U32 i = 0; i < audio_bit_depth; i++ )
        {
            mBitMasks.push_back( mask );
            mask = mask >> 1;
        }
    }
    else
    {
        U64 mask = 1;
        for( U32 i = 0; i < audio_bit_depth; i++ )
        {
            mBitMasks.push_back( mask );
            mask = mask << 1;
        }
    }

    // enum TdmFrameType { FRAME_TRANSITION_TWICE_EVERY_WORD, FRAME_TRANSITION_ONCE_EVERY_WORD, FRAME_TRANSITION_TWICE_EVERY_FOUR_WORDS };
    U32 bits_per_word = audio_bit_depth + mNumPaddingBits;
    switch( mSettings->mFrameType )
    {
    case FRAME_TRANSITION_TWICE_EVERY_WORD:
        if( mUseShortFrames == false )
        {
            U32 high_count = bits_per_word / 2;
            U32 low_count = bits_per_word - high_count;
            for( U32 i = 0; i < high_count; i++ )
                mFrameBits.push_back( BIT_HIGH );
            for( U32 i = 0; i < low_count; i++ )
                mFrameBits.push_back( BIT_LOW );
        }
        else
        {
            mFrameBits.push_back( BIT_HIGH );
            for( U32 i = 1; i < bits_per_word; i++ )
                mFrameBits.push_back( BIT_LOW );
        }
        break;
    case FRAME_TRANSITION_ONCE_EVERY_WORD:
        if( mUseShortFrames == false )
        {
            for( U32 i = 0; i < bits_per_word; i++ )
                mFrameBits.push_back( BIT_HIGH );
            for( U32 i = 0; i < bits_per_word; i++ )
                mFrameBits.push_back( BIT_LOW );
        }
        else
        {
            mFrameBits.push_back( BIT_HIGH );
            for( U32 i = 1; i < ( bits_per_word * 2 ); i++ )
                mFrameBits.push_back( BIT_LOW );
        }
        break;
    case FRAME_TRANSITION_TWICE_EVERY_FOUR_WORDS:
        if( mUseShortFrames == false )
        {
            for( U32 i = 0; i < ( bits_per_word * 2 ); i++ )
                mFrameBits.push_back( BIT_HIGH );
            for( U32 i = 0; i < ( bits_per_word * 2 ); i++ )
                mFrameBits.push_back( BIT_LOW );
        }
        else
        {
            mFrameBits.push_back( BIT_HIGH );
            for( U32 i = 1; i < ( bits_per_word * 4 ); i++ )
                mFrameBits.push_back( BIT_LOW );
        }
        break;
    default:
        AnalyzerHelpers::Assert( "unexpected" );
        break;
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
        if( mSettings->mBitAlignment == BITS_SHIFTED_RIGHT_1 )
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
        if( mSettings->mWordAlignment == RIGHT_ALIGNED )
        {
            if( mPaddingCount < mNumPaddingBits )
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
        if( mCurrentBitIndex == mSettings->mDataBitsPerSlot )
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
        if( mSettings->mWordAlignment == LEFT_ALIGNED )
        {
            if( mPaddingCount < mNumPaddingBits )
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
    double value;
    S64 max_amplitude = ( 1 << ( mSettings->mDataBitsPerSlot - 2 ) ) - 1;

    if( mCurrentAudioChannel == Left )
    {
        value = mSineWaveSamplesLeft->GetNextValue();
        mCurrentAudioChannel = Right;
    }
    else
    {
        value = mSineWaveSamplesRight->GetNextValue();
        mCurrentAudioChannel = Left;
    }

    //return S64(double(max_amplitude) * value);
    return S64( double(max_amplitude) * value);
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