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

    char channel_num_str[ 128 ];
    char number_str[ 128 ];
    char error_str[ 80 ] = "";
    char warning_str[ 32 ] = "";

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

    if(frame.mFlags & (DISPLAY_AS_ERROR_FLAG | DISPLAY_AS_WARNING_FLAG))
    {
        sprintf( channel_num_str, "%s%s", (frame.mFlags & DISPLAY_AS_ERROR_FLAG) > 0 ? "E" : "",
                                          (frame.mFlags & DISPLAY_AS_WARNING_FLAG) > 0 ? "W" : "");

        if(frame.mFlags & SHORT_SLOT)
        {
            sprintf(error_str, "Short Slot ");
        }
        if(frame.mFlags & MISSED_DATA)
        {
            sprintf(error_str + strlen(error_str), "Data Error ");
        }
        if(frame.mFlags & MISSED_FRAME_SYNC)
        {
            sprintf(error_str + strlen(error_str), "Frame Sync Missed ");
        }

        if(frame.mFlags & UNEXPECTED_BITS)
        {
            sprintf(warning_str, "Extra Slot ");
        }
    }
    else
    {
        sprintf( channel_num_str, "%d", frame.mType + 1 );
    }
    AddResultString( channel_num_str );


    sprintf( channel_num_str, "%s%s Ch %d", (frame.mFlags & DISPLAY_AS_ERROR_FLAG) > 0 ? "E" : "",
                                            (frame.mFlags & DISPLAY_AS_WARNING_FLAG) > 0 ? "W" : "",
                                            frame.mType + 1 );
    AddResultString( channel_num_str );

    sprintf( channel_num_str, "%s%s%s%sCh %d ", (frame.mFlags & DISPLAY_AS_ERROR_FLAG) > 0 ? "E:" : "",
                                                error_str,
                                                (frame.mFlags & DISPLAY_AS_WARNING_FLAG) > 0 ? "W:" : "",
                                                warning_str,
                                                frame.mType + 1 );
    AddResultString( channel_num_str, number_str );
}

void TdmAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 /*export_type_user_id*/ )
{
    std::stringstream ss;
    void* f = AnalyzerHelpers::StartFile( file );

    U64 trigger_sample = mAnalyzer->GetTriggerSample();
    U32 sample_rate = mAnalyzer->GetSampleRate();

    ss << "Time [s],Channel,Value,Flags" << std::endl;

    U64 num_frames = GetNumFrames();
    for( U64 i = 0; i < num_frames; i++ )
    {
        Frame frame = GetFrame( i );

        char channel_num_str[ 128 ];
        char time_str[ 128 ];
        char flag_str[ 8 ];
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

        sprintf( channel_num_str, "%d", frame.mType  + 1);
        sprintf( flag_str, "0x%02X", frame.mFlags);
        ss << time_str << "," << channel_num_str << "," << number_str << "," << flag_str << std::endl;

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
    char error_str[ 80 ] = "";
    char warning_str[ 32 ] = "";

    ClearTabularText();

    Frame frame = GetFrame( frame_index );

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

    if(frame.mFlags & SHORT_SLOT)
    {
        sprintf(error_str, "Short Slot ");
    }
    if(frame.mFlags & MISSED_DATA)
    {
        sprintf(error_str + strlen(error_str), "Data Error ");
    }
    if(frame.mFlags & MISSED_FRAME_SYNC)
    {
        sprintf(error_str + strlen(error_str), "Frame Sync Missed ");
    }

    if(frame.mFlags & UNEXPECTED_BITS)
    {
        sprintf(warning_str, "Extra Slot ");
    }

    sprintf( channel_num_str, "ch %d: ", frame.mType + 1 );
    AddTabularText( channel_num_str, number_str );
    AddTabularText( "Errors", error_str);
    AddTabularText( "Warnings", warning_str );

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
