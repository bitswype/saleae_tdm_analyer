#include "TdmAnalyzerResults.h"
#include <AnalyzerHelpers.h>
#include "TdmAnalyzer.h"
#include "TdmAnalyzerSettings.h"
#include <iostream>
#include <fstream>
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
        if(frame.mFlags & BITCLOCK_ERROR)
        {
            sprintf(error_str + strlen(error_str), "Bitclock Error ");
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

void TdmAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 file_type/*export_type_user_id*/ )
{
    // replace with file_type when Saleae fixes the export options menu
    switch(mSettings->mExportFileType)
    {
        case ExportFileType::WAV:
        {
            GenerateWAV( file );
            break;
        }
        case ExportFileType::CSV:
        default:
        {
            GenerateCSV( file, display_base );
            break;
        }
    }

}

void TdmAnalyzerResults::GenerateCSV( const char* file, DisplayBase display_base )
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

void TdmAnalyzerResults::GenerateWAV( const char* file )
{
    std::ofstream f;
    f.open( file , std::ios::out | std::ios::binary );
    U64 num_frames = GetNumFrames();

    if( f.is_open() )
    {
        PCMWaveFileHandler wave_file_handler(f, mSettings->mTdmFrameRate, mSettings->mSlotsPerFrame, mSettings->mDataBitsPerSlot);
        //PCMExtendedWaveFileHandler wave_file_handler(f, mSettings->mTdmFrameRate, mSettings->mSlotsPerFrame, mSettings->mDataBitsPerSlot);
        U16 num_slots_per_frame = mSettings->mSlotsPerFrame;
        
        for( U64 i = 0; i < num_frames; i++ )
        {
            Frame frame = GetFrame( i );
            
            // only populate slots we expect, extra slots are not stored in the wave file
            if(frame.mType < num_slots_per_frame)
            {
                wave_file_handler.addSample( frame.mData1 );
                if( UpdateExportProgressAndCheckForCancel( i, num_frames ) == true )
                {
                    wave_file_handler.close();
                    return;
                }
            }
        }
        UpdateExportProgressAndCheckForCancel( num_frames, num_frames );
        wave_file_handler.close();
    }
}

PCMWaveFileHandler::PCMWaveFileHandler(std::ofstream & file, U32 sample_rate, U32 num_channels, U32 bits_per_channel) : 
  mFile( file ),
  mSampleRate( sample_rate ),
  mNumChannels( num_channels ),
  mBitsPerChannel( bits_per_channel ),
  mSampleCount( 0 ),
  mTotalFrames( 0 ),
  mWtPosSaved( 0 ),
  mSampleData( nullptr ),
  mSampleIndex( 0 )
{
    if(bits_per_channel <= 8){
        mWaveHeader.mBitsPerSample = 8;
        mBytesPerChannel = 1;
    }else if(bits_per_channel <= 16){
        mWaveHeader.mBitsPerSample = 16;
        mBytesPerChannel = 2;
    }else if(bits_per_channel <= 32){
        mWaveHeader.mBitsPerSample = 32;
        mBytesPerChannel = 4;
    // sizes above 32 bits might not work
    }else if(bits_per_channel <= 40){
        mWaveHeader.mBitsPerSample = 40;
        mBytesPerChannel = 5;
    }else if(bits_per_channel <= 48){
        mWaveHeader.mBitsPerSample = 48;
        mBytesPerChannel = 6;
    }else if(bits_per_channel <= 64){
        mWaveHeader.mBitsPerSample = 64;
        mBytesPerChannel = 8;
    }

    mWaveHeader.mBlockSizeBytes = mBytesPerChannel * num_channels;
    mFrameSizeBytes = mWaveHeader.mBlockSizeBytes;
    mBitShift = mWaveHeader.mBitsPerSample - bits_per_channel;

    mWaveHeader.mNumChannels = num_channels;
    mWaveHeader.mSamplesPerSec = sample_rate;
    mWaveHeader.mBytesPerSec = mFrameSizeBytes * sample_rate;

    if(mFile.is_open())
    {
        mFile.seekp(0);
        mFile.write((const char *) (&mWaveHeader), sizeof(mWaveHeader));
        mSampleData = new U64[mNumChannels];
    }
}

PCMWaveFileHandler::~PCMWaveFileHandler()
{
    if(mFile.is_open())
    {
        close();
    }
}

void PCMWaveFileHandler::addSample(U64 sample)
{
    if(mBytesPerChannel == 1)
    {
        // in a wave file, 8 bit data is stored as "offset" instead of 2's compliment
        // for offset data, 0 = min value, 255 = max value which means for 8 data bits
        // 0x80 -> -128 (min value), we need to add 128 to get 0
        // for less than 8 bits, we have to do a little manipulation,
        // 8 data bits, add 128 (1 << 7)
        // 7 data bits, add 64 ( 1 << 6)
        // ...
        // 2 data bits, add 2 ( 1 << 1)

        sample = sample + (1ULL << (7 - mBitShift));
    }

    // writeLittleEndianData(sample << mBitShift, mBytesPerChannel);
    mSampleData[mSampleIndex++] = sample;
    
    //if(((++mSampleCount) % mNumChannels) == 0){
    if(mSampleIndex >= mNumChannels){ // we have another complete frame
        mTotalFrames++;
        mSampleCount += mNumChannels;
        mSampleIndex = 0;
        for (U32 i = 0; i < mNumChannels; i++)
            writeLittleEndianData(mSampleData[i] << mBitShift, mBytesPerChannel);

        if((mTotalFrames % (mSampleRate / 100)) == 0){
            updateFileSize();
        }
    }
    return;
}

void PCMWaveFileHandler::writeLittleEndianData(U64 value, U8 num_bytes)
{
    char data_byte;

    for(U8 i = 0; i < num_bytes; i++)
    {
        data_byte = value & 0xFF;
        mFile.write(&data_byte, 1);
        value >>= 8;
    }
}

void PCMWaveFileHandler::close(void)
{
    updateFileSize();
    if(((mTotalFrames * mFrameSizeBytes) % 2) == 1){
        char zero = 0;
        mFile.write(&zero, 1);
    }
    mFile.close();
    delete[] mSampleData;
    mSampleData = nullptr;
    return;
}

void PCMWaveFileHandler::updateFileSize(void)
{
    U32 data_size_bytes = mTotalFrames * mFrameSizeBytes;

    if((data_size_bytes % 2) == 1){
        data_size_bytes += 1;
    }

    mWtPosSaved = mFile.tellp();

    mFile.seekp(RIFF_CKSIZE_POS);
    writeLittleEndianData( 36 + data_size_bytes, 4 );

    mFile.seekp(DATA_CKSIZE_POS);
    writeLittleEndianData( data_size_bytes, 4 );
    
    mFile.seekp(mWtPosSaved);
    return;
}

PCMExtendedWaveFileHandler::PCMExtendedWaveFileHandler(std::ofstream & file, U32 sample_rate, U32 num_channels, U32 bits_per_channel) : 
  mFile( file ),
  mSampleRate( sample_rate ),
  mNumChannels( num_channels ),
  mBitsPerChannel( bits_per_channel ),
  mSampleCount( 0 ),
  mTotalFrames( 0 ),
  mWtPosSaved( 0 ),
  mSampleData( nullptr ),
  mSampleIndex( 0 )
{
    if(bits_per_channel <= 8){
        mWaveHeader.mBitsPerSample = 8;
        mBytesPerChannel = 1;
    }else if(bits_per_channel <= 16){
        mWaveHeader.mBitsPerSample = 16;
        mBytesPerChannel = 2;
    }else if(bits_per_channel <= 32){
        mWaveHeader.mBitsPerSample = 32;
        mBytesPerChannel = 4;
    }else if(bits_per_channel <= 40){
        mWaveHeader.mBitsPerSample = 40;
        mBytesPerChannel = 5;
    }else if(bits_per_channel <= 48){
        mWaveHeader.mBitsPerSample = 48;
        mBytesPerChannel = 6;
    }else if(bits_per_channel <= 64){
        mWaveHeader.mBitsPerSample = 64;
        mBytesPerChannel = 8;
    }

    mWaveHeader.mDataBitsPerSample = bits_per_channel;
    mWaveHeader.mBlockSizeBytes = mBytesPerChannel * num_channels;
    mFrameSizeBytes = mWaveHeader.mBlockSizeBytes;
    mBitShift = mWaveHeader.mBitsPerSample - bits_per_channel;

    mWaveHeader.mNumChannels = num_channels;
    mWaveHeader.mSamplesPerSec = sample_rate;
    mWaveHeader.mBytesPerSec = mFrameSizeBytes * sample_rate;

    if(mFile.is_open())
    {
        mFile.seekp(0);
        mFile.write((const char *) (&mWaveHeader), sizeof(mWaveHeader));
        mSampleData = new U64[mNumChannels];
    }
}

PCMExtendedWaveFileHandler::~PCMExtendedWaveFileHandler()
{
    if(mFile.is_open())
    {
        close();
    }
}

void PCMExtendedWaveFileHandler::addSample(U64 sample)
{
    if(mBytesPerChannel == 1)
    {
        // in a wave file, 8 bit data is stored as "offset" instead of 2's compliment
        // for offset data, 0 = min value, 255 = max value which means for 8 data bits
        // 0x80 -> -128 (min value), we need to add 128 to get 0
        // for less than 8 bits, we have to do a little manipulation,
        // 8 data bits, add 128 (1 << 7)
        // 7 data bits, add 64 ( 1 << 6)
        // ...
        // 2 data bits, add 2 ( 1 << 1)

        sample = sample + (1ULL << (7 - mBitShift));
    }

    // writeLittleEndianData(sample << mBitShift, mBytesPerChannel);
    mSampleData[mSampleIndex++] = sample;
    
    //if(((++mSampleCount) % mNumChannels) == 0){
    if(mSampleIndex >= mNumChannels){ // we have another complete frame
        mTotalFrames++;
        mSampleCount += mNumChannels;
        mSampleIndex = 0;
        for (U32 i = 0; i < mNumChannels; i++)
            writeLittleEndianData(mSampleData[i] << mBitShift, mBytesPerChannel);

        if((mTotalFrames % (mSampleRate / 100)) == 0){
            updateFileSize();
        }
    }
    return;
}

void PCMExtendedWaveFileHandler::writeLittleEndianData(U64 value, U8 num_bytes)
{
    char data_byte;

    for(U8 i = 0; i < num_bytes; i++)
    {
        data_byte = value & 0xFF;
        mFile.write(&data_byte, 1);
        value >>= 8;// sample >> 8;
    }
}

void PCMExtendedWaveFileHandler::close(void)
{
    updateFileSize();
    if(((mTotalFrames * mFrameSizeBytes) % 2) == 1){
        char zero = 0;
        mFile.write(&zero, 1);
    }
    mFile.close();
    delete[] mSampleData;
    mSampleData = nullptr;
    return;
}

void PCMExtendedWaveFileHandler::updateFileSize(void)
{
    U32 data_size_bytes = mTotalFrames * mFrameSizeBytes;

    if((data_size_bytes % 2) == 1){
        data_size_bytes += 1;
    }

    mWtPosSaved = mFile.tellp();

    mFile.seekp(EXT_RIFF_CKSIZE_POS);
    writeLittleEndianData( 72 + data_size_bytes, 4 );

    mFile.seekp(EXT_FACT_CKSIZE_POS);
    writeLittleEndianData( mNumChannels * mTotalFrames, 4 );

    mFile.seekp(EXT_DATA_CKSIZE_POS);
    writeLittleEndianData( data_size_bytes, 4 );
    
    mFile.seekp(mWtPosSaved);
    return;
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
        sprintf(error_str, " Short Slot");
    }
    if(frame.mFlags & MISSED_DATA)
    {
        sprintf(error_str + strlen(error_str), " Data Error");
    }
    if(frame.mFlags & MISSED_FRAME_SYNC)
    {
        sprintf(error_str + strlen(error_str), " Frame Sync Missed");
    }
    if(frame.mFlags & BITCLOCK_ERROR)
    {
        sprintf(error_str + strlen(error_str), " Bitclock Error");
    }

    if(frame.mFlags & UNEXPECTED_BITS)
    {
        sprintf(warning_str, " Extra Slot");
    }

    sprintf( channel_num_str, "ch %d: ", frame.mType + 1 );
    AddTabularText( channel_num_str, number_str, error_str, warning_str, "\n" );
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
