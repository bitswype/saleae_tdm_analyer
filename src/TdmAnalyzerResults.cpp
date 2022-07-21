#include "TdmAnalyzerResults.h"
#include <AnalyzerHelpers.h>
#include "TdmAnalyzer.h"
#include "TdmAnalyzerSettings.h"
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <cstring>

#pragma warning( disable : 4996 ) // warning C4996: 'sprintf': This function or variable may be unsafe. Consider using sprintf_s instead.


TdmAnalyzerResults::TdmAnalyzerResults( TdmAnalyzer* analyzer, TdmAnalyzerSettings* settings )
    : AnalyzerResults(), mSettings( settings ), mAnalyzer( analyzer )
{
}

TdmAnalyzerResults::~TdmAnalyzerResults()
{
}

void TdmAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& /*channel*/,
                                             DisplayBase display_base ) // unrefereced vars commented out to remove warnings.
{
    ClearResultStrings();
    Frame frame = GetFrame( frame_index );

    switch( TdmResultType( frame.mType ) )
    {
    case ErrorTooFewBits:
    {
        char bits_per_word[ 32 ];
        sprintf( bits_per_word, "%d", mSettings->mDataBitsPerSlot );

        AddResultString( "!" );
        AddResultString( "Error" );
        AddResultString( "Error: not enough bits in slot" );
        AddResultString( "Error: too few bits, expecting ", bits_per_word );
    }
    break;
    case ErrorDoesntDivideEvenly:
    {
        AddResultString( "!" );
        AddResultString( "Error" );
        AddResultString( "Error: invalid number of bits in frame" );
        AddResultString( "Error: bits don't divide evenly between subframes" );
    }
    break;
    default:
    {
        char channel_num_str[ 128 ];
        char number_str[ 128 ];
        if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
        {
            S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mDataBitsPerSlot );
            std::stringstream ss;
            ss << signed_number;
            strcpy( number_str, ss.str().c_str() );
        }
        else
        {
            AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mDataBitsPerSlot, number_str, 128 );
        }

        sprintf( channel_num_str, "%d", frame.mType );
        AddResultString( channel_num_str );

        sprintf( channel_num_str, "Ch %d", frame.mType );
        AddResultString( channel_num_str );

        sprintf( channel_num_str, "Ch %d: ", frame.mType );
        AddResultString( channel_num_str, number_str );
    }
    break;
    }
}

void TdmAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 /*export_type_user_id*/ )
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

        char channel_num_str[ 128 ];
        char time_str[ 128 ];
        AnalyzerHelpers::GetTimeString( frame.mStartingSampleInclusive, trigger_sample, sample_rate, time_str, 128 );

        char number_str[ 128 ];
        if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
        {
            S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mDataBitsPerSlot );
            std::stringstream ss;
            ss << signed_number;
            strcpy( number_str, ss.str().c_str() );
        }
        else
        {
            AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mDataBitsPerSlot, number_str, 128 );
        }

        sprintf( channel_num_str, "%d", frame.mType );
        ss << time_str << "," << channel_num_str << "," << number_str << std::endl;

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

void TdmAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
    ClearTabularText();

    Frame frame = GetFrame( frame_index );

    switch( TdmResultType( frame.mType ) )
    {
    case ErrorTooFewBits:
    {
        char bits_per_word[ 32 ];
        sprintf( bits_per_word, "%d", mSettings->mDataBitsPerSlot );

        AddTabularText( "Error: too few bits, expecting ", bits_per_word );
    }
    break;
    case ErrorDoesntDivideEvenly:
    {
        AddTabularText( "Error: bits don't divide evenly between subframes" );
    }
    break;
    default:
    {
        char channel_num_str[ 128 ];
        char number_str[ 128 ];
        if( ( display_base == Decimal ) && ( mSettings->mSigned == AnalyzerEnums::SignedInteger ) )
        {
            S64 signed_number = AnalyzerHelpers::ConvertToSignedNumber( frame.mData1, mSettings->mDataBitsPerSlot );
            std::stringstream ss;
            ss << signed_number;
            strcpy( number_str, ss.str().c_str() );
        }
        else
        {
            AnalyzerHelpers::GetNumberString( frame.mData1, display_base, mSettings->mDataBitsPerSlot, number_str, 128 );
        }

        sprintf( channel_num_str, "ch %d: ", frame.mType );
        AddTabularText( channel_num_str, number_str );
    }
    break;
    }
}

void TdmAnalyzerResults::GeneratePacketTabularText( U64 /*packet_id*/,
                                                    DisplayBase /*display_base*/ ) // unrefereced vars commented out to remove warnings.
{
    ClearResultStrings();
    AddResultString( "not supported" );
}

void
    TdmAnalyzerResults::GenerateTransactionTabularText( U64 /*transaction_id*/,
                                                        DisplayBase /*display_base*/ ) // unrefereced vars commented out to remove warnings.
{
    ClearResultStrings();
    AddResultString( "not supported" );
}
