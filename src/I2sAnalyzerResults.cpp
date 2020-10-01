#include "I2sAnalyzerResults.h"
#include <AnalyzerHelpers.h>
#include "I2sAnalyzer.h"
#include "I2sAnalyzerSettings.h"
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <cstring>

#pragma warning( disable : 4996 ) // warning C4996: 'sprintf': This function or variable may be unsafe. Consider using sprintf_s instead.


I2sAnalyzerResults::I2sAnalyzerResults( I2sAnalyzer* analyzer, I2sAnalyzerSettings* settings )
    : AnalyzerResults(), mSettings( settings ), mAnalyzer( analyzer )
{
}

I2sAnalyzerResults::~I2sAnalyzerResults()
{
}

void I2sAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& /*channel*/,
                                             DisplayBase display_base ) // unrefereced vars commented out to remove warnings.
{
    ClearResultStrings();
    Frame frame = GetFrame( frame_index );

    switch( I2sResultType( frame.mType ) )
    {
    case Channel1:
    {
        char number_str[ 128 ];
        if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
        {
            S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mBitsPerWord );
            std::stringstream ss;
            ss << signed_number;
            strcpy( number_str, ss.str().c_str() );
        }
        else
        {
            AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mBitsPerWord, number_str, 128 );
        }
        AddResultString( "1" );
        AddResultString( "Ch 1" );
        AddResultString( "Ch 1: ", number_str );
    }
    break;
    case Channel2:
    {
        char number_str[ 128 ];
        if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
        {
            S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mBitsPerWord );
            std::stringstream ss;
            ss << signed_number;
            strcpy( number_str, ss.str().c_str() );
        }
        else
        {
            AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mBitsPerWord, number_str, 128 );
        }
        AddResultString( "2" );
        AddResultString( "Ch 2" );
        AddResultString( "Ch 2: ", number_str );
    }
    break;
    case ErrorTooFewBits:
    {
        char bits_per_word[ 32 ];
        sprintf( bits_per_word, "%d", mSettings->mBitsPerWord );

        AddResultString( "!" );
        AddResultString( "Error" );
        AddResultString( "Error: too few bits" );
        AddResultString( "Error: too few bits, expecting ", bits_per_word );
    }
    break;
    case ErrorDoesntDivideEvenly:
    {
        AddResultString( "!" );
        AddResultString( "Error" );
        AddResultString( "Error: bits don't divide evenly" );
        AddResultString( "Error: bits don't divide evenly between subframes" );
    }
    break;
    }
}

void I2sAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 /*export_type_user_id*/ )
{
    std::stringstream ss;
    void* f = AnalyzerHelpers::StartFile( file );

    U64 trigger_sample = mAnalyzer->GetTriggerSample();
    U32 sample_rate = mAnalyzer->GetSampleRate();

    ss << "Time [s],Channel,Value" << std::endl;

    U64 num_frames = GetNumFrames();
    for( U64 i = 0; i < num_frames; i++ )
    {
        Frame frame = GetFrame( i );

        if( I2sResultType( frame.mType ) == Channel1 )
        {
            char time_str[ 128 ];
            AnalyzerHelpers::GetTimeString( frame.mStartingSampleInclusive, trigger_sample, sample_rate, time_str, 128 );

            char number_str[ 128 ];
            if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
            {
                S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mBitsPerWord );
                std::stringstream ss;
                ss << signed_number;
                strcpy( number_str, ss.str().c_str() );
            }
            else
            {
                AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mBitsPerWord, number_str, 128 );
            }

            ss << time_str << ",1," << number_str << std::endl;
        }

        if( I2sResultType( frame.mType ) == Channel2 )
        {
            char time_str[ 128 ];
            AnalyzerHelpers::GetTimeString( frame.mStartingSampleInclusive, trigger_sample, sample_rate, time_str, 128 );

            char number_str[ 128 ];
            if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
            {
                S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mBitsPerWord );
                std::stringstream ss;
                ss << signed_number;
                strcpy( number_str, ss.str().c_str() );
            }
            else
            {
                AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mBitsPerWord, number_str, 128 );
            }


            ss << time_str << ",2," << number_str << std::endl;
        }

        AnalyzerHelpers::AppendToFile( ( U8* )ss.str().c_str(), ss.str().length(), f );
        ss.str( std::string() );

        if( UpdateExportProgressAndCheckForCancel( i, num_frames ) == true )
        {
            AnalyzerHelpers::EndFile( f );
            return;
        }
    }

    UpdateExportProgressAndCheckForCancel( num_frames, num_frames );
    AnalyzerHelpers::EndFile( f );
}

void I2sAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
    ClearTabularText();

    Frame frame = GetFrame( frame_index );

    switch( I2sResultType( frame.mType ) )
    {
    case Channel1:
    {
        char number_str[ 128 ];
        if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
        {
            S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mBitsPerWord );
            std::stringstream ss;
            ss << signed_number;
            strcpy( number_str, ss.str().c_str() );
        }
        else
        {
            AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mBitsPerWord, number_str, 128 );
        }

        AddTabularText( "Ch 1: ", number_str );
    }
    break;
    case Channel2:
    {
        char number_str[ 128 ];
        if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
        {
            S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mBitsPerWord );
            std::stringstream ss;
            ss << signed_number;
            strcpy( number_str, ss.str().c_str() );
        }
        else
        {
            AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mBitsPerWord, number_str, 128 );
        }

        AddTabularText( "Ch 2: ", number_str );
    }
    break;
    case ErrorTooFewBits:
    {
        char bits_per_word[ 32 ];
        sprintf( bits_per_word, "%d", mSettings->mBitsPerWord );

        AddTabularText( "Error: too few bits, expecting ", bits_per_word );
    }
    break;
    case ErrorDoesntDivideEvenly:
    {
        AddTabularText( "Error: bits don't divide evenly between subframes" );
    }
    break;
    }
}

void I2sAnalyzerResults::GeneratePacketTabularText( U64 /*packet_id*/,
                                                    DisplayBase /*display_base*/ ) // unrefereced vars commented out to remove warnings.
{
    ClearResultStrings();
    AddResultString( "not supported" );
}

void
    I2sAnalyzerResults::GenerateTransactionTabularText( U64 /*transaction_id*/,
                                                        DisplayBase /*display_base*/ ) // unrefereced vars commented out to remove warnings.
{
    ClearResultStrings();
    AddResultString( "not supported" );
}
