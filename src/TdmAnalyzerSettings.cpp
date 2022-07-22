#include "TdmAnalyzerSettings.h"

#include <AnalyzerHelpers.h>
#include <sstream>
#include <cstring>
#include <stdio.h>

#pragma warning( disable : 4996 ) // warning C4996: 'sprintf': This function or variable may be unsafe. Consider using sprintf_s instead.

TdmAnalyzerSettings::TdmAnalyzerSettings()
    : mClockChannel( UNDEFINED_CHANNEL ),
      mFrameChannel( UNDEFINED_CHANNEL ),
      mDataChannel( UNDEFINED_CHANNEL ),

      mSlotsPerFrame( 8 ),
      mBitsPerSlot( 16 ),
      mDataBitsPerSlot( 16 ),
      mShiftOrder( AnalyzerEnums::MsbFirst ),
      mDataValidEdge( AnalyzerEnums::PosEdge ),

      mDataAlignment( LEFT_ALIGNED ),
      mBitAlignment( BITS_SHIFTED_RIGHT_1 ),
      mSigned( AnalyzerEnums::UnsignedInteger ),
      mFrameSyncInverted( FS_NOT_INVERTED ),
      mEnableAdvancedAnalysis( false )
{
    mClockChannelInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mClockChannelInterface->SetTitleAndTooltip( "CLOCK channel", "Clock, aka TDM SCK or BCLK - Continuous Serial Clock, aka Bit Clock" );
    mClockChannelInterface->SetChannel( mClockChannel );

    mFrameChannelInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mFrameChannelInterface->SetTitleAndTooltip( "FRAME", "Frame Delimiter / aka TDM FS - Frame Sync aka Sampling Clock" );
    mFrameChannelInterface->SetChannel( mFrameChannel );

    mDataChannelInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mDataChannelInterface->SetTitleAndTooltip( "DATA", "Data, aka TDM SDx - Serial Data In/Out" );
    mDataChannelInterface->SetChannel( mDataChannel );

    mSlotsPerFrameInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mSlotsPerFrameInterface->SetTitleAndTooltip( "Number of slots (channels) per TDM frame (slots/frame)",
                                                 "Specify the number of audio channels / TDM frame.  Any additional slots will be ignored" );
    for( U32 i = 1; i <= 128; i++ )
    {
        char str[ 256 ];
        sprintf( str, "%d Slot%s/TDM Frame", i , i == 1 ? "" : "s");
        mSlotsPerFrameInterface->AddNumber( i, str, "" );
    }
    mSlotsPerFrameInterface->SetNumber( mSlotsPerFrame );

    mBitsPerSlotInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mBitsPerSlotInterface->SetTitleAndTooltip( "Number of bits per slot in the TDM frame",
                                               "Specify the number of bits per slot.  There can be more bits in a slot than data bits" );
    for( U32 i = 1; i <= 64; i++ )
    {
        char str[ 256 ];
        sprintf( str, "%d bits/slot", i );
        mBitsPerSlotInterface->AddNumber( i, str, "" );
    }
    mBitsPerSlotInterface->SetNumber( mBitsPerSlot );

    mDataBitsPerSlotInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mDataBitsPerSlotInterface->SetTitleAndTooltip( "Audio Bit Depth (data bits/slot, must be <= bits/slot)",
                                                   "Specify the number of audio data bits/channel.  Must be equal or less than bits/slot.  Any additional bits will be ignored" );
    for( U32 i = 2; i <= 64; i++ )
    {
        char str[ 256 ];
        sprintf( str, "%d Data bits/slot", i );
        mDataBitsPerSlotInterface->AddNumber( i, str, "" );
    }
    mDataBitsPerSlotInterface->SetNumber( mDataBitsPerSlot );

    mShiftOrderInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mShiftOrderInterface->SetTitleAndTooltip( "DATA Significant Bit",
                                              "Select if the most significant bit or least significant bit is transmitted first" );
    mShiftOrderInterface->AddNumber( AnalyzerEnums::MsbFirst, "Most Significant Bit Sent First", "" );
    mShiftOrderInterface->AddNumber( AnalyzerEnums::LsbFirst, "Least Significant Bit Sent First", "" );
    mShiftOrderInterface->SetNumber( mShiftOrder );

    mDataValidEdgeInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mDataValidEdgeInterface->SetTitleAndTooltip( "Data Valid CLOCK edge",
                                                 "Specify if data is valid (should be read) on the rising, or falling clock edge." );
    mDataValidEdgeInterface->AddNumber( AnalyzerEnums::NegEdge, "Falling edge", "" );
    mDataValidEdgeInterface->AddNumber( AnalyzerEnums::PosEdge, "Rising edge", "" );
    mDataValidEdgeInterface->SetNumber( mDataValidEdge );

    mWordAlignmentInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mWordAlignmentInterface->SetTitleAndTooltip( "DATA Bits Alignment",
                                                 "Specify whether data bits are left or right aligned with respect to FRAME edges. Only "
                                                 "needed if more bits are sent than needed each frame, and additional bits are ignored." );
    mWordAlignmentInterface->AddNumber( LEFT_ALIGNED, "Left aligned", "" );
    mWordAlignmentInterface->AddNumber( RIGHT_ALIGNED, "Right aligned", "" );
    mWordAlignmentInterface->SetNumber( mDataAlignment );

    // enum TdmBitAlignment { FIRST_FRAME_BIT_BELONGS_TO_PREVIOUS_WORD, FIRST_FRAME_BIT_BELONGS_TO_CURRENT_WORD };
    mBitAlignmentInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mBitAlignmentInterface->SetTitleAndTooltip( "DATA Bits Shift relative to Frame Sync", "Specify the bit shift with respect to the FRAME edges" );
    mBitAlignmentInterface->AddNumber( BITS_SHIFTED_RIGHT_1, "Right-shifted by one (TDM typical, DSP mode A)", "" );
    mBitAlignmentInterface->AddNumber( NO_SHIFT, "No shift (DSP mode B)", "" );
    mBitAlignmentInterface->SetNumber( mBitAlignment );

    mSignedInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mSignedInterface->SetTitleAndTooltip(
        "Signed/Unsigned", "Select whether samples are unsigned or signed values (only shows up if the display type is decimal)" );
    mSignedInterface->AddNumber( AnalyzerEnums::UnsignedInteger, "Unsigned", "Interpret samples as unsigned integers" );
    mSignedInterface->AddNumber( AnalyzerEnums::SignedInteger, "Signed (two's complement)",
                                 "Interpret samples as signed integers -- only when display type is set to decimal" );
    mSignedInterface->SetNumber( mSigned );


    mFrameSyncInvertedInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mFrameSyncInvertedInterface->SetTitleAndTooltip( "Frame Sync Inverted", "Select whether Frame Sync is active high (normal) or active low (inverted)" );
    mFrameSyncInvertedInterface->AddNumber( FS_NOT_INVERTED, "Active High (TDM normal))",
                                             "when frame sync (FRAME) is logic 1, indicate the start of a new frame (normal)" );
    mFrameSyncInvertedInterface->AddNumber( FS_INVERTED, "Active Low (TDM inverted)",
                                             "when frame sync (FRAME) is logic 0, indicate the start of a new frame (inverted)" );
    mFrameSyncInvertedInterface->SetNumber( mFrameSyncInverted );

    mEnableAdvancedAnalysisInterface.reset(new AnalyzerSettingInterfaceBool() );
    mEnableAdvancedAnalysisInterface->SetTitleAndTooltip(
        "Advanced analysis of TDM signals", "Enables more in depth analysis of the TDM data stream, identifies more potential problems"
    );
    mEnableAdvancedAnalysisInterface->SetCheckBoxText("Perform more analysis on the TDM signal, may slow down processing");
    mEnableAdvancedAnalysisInterface->SetValue( mEnableAdvancedAnalysis );

    AddInterface( mClockChannelInterface.get() );
    AddInterface( mFrameChannelInterface.get() );
    AddInterface( mDataChannelInterface.get() );
    AddInterface( mSlotsPerFrameInterface.get() );
    AddInterface( mBitsPerSlotInterface.get() );
    AddInterface( mDataBitsPerSlotInterface.get() );
    AddInterface( mShiftOrderInterface.get() );
    AddInterface( mDataValidEdgeInterface.get() );
    AddInterface( mWordAlignmentInterface.get() );
    AddInterface( mBitAlignmentInterface.get() );
    AddInterface( mSignedInterface.get() );
    AddInterface( mFrameSyncInvertedInterface.get() );
    AddInterface( mEnableAdvancedAnalysisInterface.get() );

    // AddExportOption( 0, "Export as text/csv file", "text (*.txt);;csv (*.csv)" );
    AddExportOption( 0, "Export as text/csv file" );
    AddExportExtension( 0, "text", "txt" );
    AddExportExtension( 0, "csv", "csv" );

    ClearChannels();
    AddChannel( mClockChannel, "TDM CLOCK", false );
    AddChannel( mFrameChannel, "TDM FRAME", false );
    AddChannel( mDataChannel, "TDM DATA", false );
}

TdmAnalyzerSettings::~TdmAnalyzerSettings()
{
}

void TdmAnalyzerSettings::UpdateInterfacesFromSettings()
{
    mClockChannelInterface->SetChannel( mClockChannel );
    mFrameChannelInterface->SetChannel( mFrameChannel );
    mDataChannelInterface->SetChannel( mDataChannel );

    mSlotsPerFrameInterface->SetNumber( mSlotsPerFrame );
    mBitsPerSlotInterface->SetNumber( mBitsPerSlot );
    mDataBitsPerSlotInterface->SetNumber( mDataBitsPerSlot );
    mShiftOrderInterface->SetNumber( mShiftOrder );
    mDataValidEdgeInterface->SetNumber( mDataValidEdge );

    mWordAlignmentInterface->SetNumber( mDataAlignment );
    mBitAlignmentInterface->SetNumber( mBitAlignment );

    mSignedInterface->SetNumber( mSigned );

    mFrameSyncInvertedInterface->SetNumber( mFrameSyncInverted );
    mEnableAdvancedAnalysisInterface->SetValue( mEnableAdvancedAnalysis );
}

bool TdmAnalyzerSettings::SetSettingsFromInterfaces()
{
    Channel clock_channel = mClockChannelInterface->GetChannel();
    if( clock_channel == UNDEFINED_CHANNEL )
    {
        SetErrorText( "Please select a channel for TDM CLOCK signal" );
        return false;
    }

    Channel frame_channel = mFrameChannelInterface->GetChannel();
    if( frame_channel == UNDEFINED_CHANNEL )
    {
        SetErrorText( "Please select a channel for TDM FRAME signal" );
        return false;
    }

    Channel data_channel = mDataChannelInterface->GetChannel();
    if( data_channel == UNDEFINED_CHANNEL )
    {
        SetErrorText( "Please select a channel for TDM DATA signal" );
        return false;
    }

    if( ( clock_channel == frame_channel ) || ( clock_channel == data_channel ) || ( frame_channel == data_channel ) )
    {
        SetErrorText( "Please select different channels for the TDM signals" );
        return false;
    }

    mClockChannel = clock_channel;
    mFrameChannel = frame_channel;
    mDataChannel = data_channel;

    mSlotsPerFrame = U32( mSlotsPerFrameInterface->GetNumber() );
    mBitsPerSlot = U32( mBitsPerSlotInterface->GetNumber() );
    mDataBitsPerSlot = U32( mDataBitsPerSlotInterface->GetNumber() );

    if( mDataBitsPerSlot > mBitsPerSlot )
    {
        SetErrorText( "Number of data bits per slot must be less than or equal to number of bits per slot" );
        return false;
    }

    mShiftOrder = AnalyzerEnums::ShiftOrder( U32( mShiftOrderInterface->GetNumber() ) );
    mDataValidEdge = AnalyzerEnums::EdgeDirection( U32( mDataValidEdgeInterface->GetNumber() ) );

    mDataAlignment = TdmDataAlignment( U32( mWordAlignmentInterface->GetNumber() ) );
    mBitAlignment = TdmBitAlignment( U32( mBitAlignmentInterface->GetNumber() ) );

    mSigned = AnalyzerEnums::Sign( U32( mSignedInterface->GetNumber() ) );

    mFrameSyncInverted = TdmFrameSelectInverted( U32( mFrameSyncInvertedInterface->GetNumber() ) );
    mEnableAdvancedAnalysis = bool(mEnableAdvancedAnalysisInterface->GetValue() );

    // AddExportOption( 0, "Export as text/csv file", "text (*.txt);;csv (*.csv)" );

    ClearChannels();
    AddChannel( mClockChannel, "TDM CLOCK", true );
    AddChannel( mFrameChannel, "TDM FRAME", true );
    AddChannel( mDataChannel, "TDM DATA", true );

    return true;
}

void TdmAnalyzerSettings::LoadSettings( const char* settings )
{
    SimpleArchive text_archive;
    text_archive.SetString( settings );

    const char* name_string; // the first thing in the archive is the name of the protocol analyzer that the data belongs to.
    text_archive >> &name_string;
    if( strcmp( name_string, "SaleaeTdmAnalyzer" ) != 0 )
        AnalyzerHelpers::Assert( "SaleaeTdmAnalyzer: Provided with a settings string that doesn't belong to us;" );

    // check to make sure loading it actual works befor assigning the result -- do this when adding settings to an anylzer which has been
    // previously released.
    Channel clock_channel;
    if( text_archive >> clock_channel )
    {
        mClockChannel = clock_channel;
    }

    Channel frame_channel;
    if( text_archive >> frame_channel )
    {
        mFrameChannel = frame_channel;
    }

    Channel data_channel;
    if( text_archive >> data_channel )
    {
        mDataChannel = data_channel;
    }

    U32 slots_per_frame;
    if( text_archive >> slots_per_frame )
    {
        mSlotsPerFrame = slots_per_frame;
    }

    U32 bits_per_slot;
    if( text_archive >> bits_per_slot )
    {
        mBitsPerSlot = bits_per_slot;
    }

    U32 data_bits_per_slot;
    if( text_archive >> data_bits_per_slot )
    {
        mDataBitsPerSlot = data_bits_per_slot;
    }

    U32 shift_order;
    if( text_archive >> shift_order )
    {
        mShiftOrder = AnalyzerEnums::ShiftOrder( shift_order );
    }

    U32 data_valid_edge;
    if( text_archive >> data_valid_edge )
    {
        mDataValidEdge = AnalyzerEnums::EdgeDirection( data_valid_edge );
    }

    U32 data_alignment;
    if( text_archive >> data_alignment )
    {
        mDataAlignment = TdmDataAlignment( data_alignment );
    }

    U32 bit_alignment;
    if( text_archive >> bit_alignment )
    {
        mBitAlignment = TdmBitAlignment( bit_alignment );
    }

    AnalyzerEnums::Sign sign;
    if( text_archive >> *( U32* )&sign )
    {
        mSigned = sign;
    }

    TdmFrameSelectInverted fs_inverted;
    if( text_archive >> *( U32* )&fs_inverted )
    {
        mFrameSyncInverted = fs_inverted;
    }

    bool enable_advanced;
    if (text_archive >> enable_advanced)
    {
        mEnableAdvancedAnalysis = enable_advanced;
    }

    ClearChannels();
    AddChannel( mClockChannel, "TDM CLOCK", true );
    AddChannel( mFrameChannel, "TDM FRAME", true );
    AddChannel( mDataChannel, "TDM DATA", true );

    UpdateInterfacesFromSettings();
}

const char* TdmAnalyzerSettings::SaveSettings()
{ // SaleaeTdmmAnalyzer
    SimpleArchive text_archive;

    text_archive << "SaleaeTdmAnalyzer";

    text_archive << mClockChannel;
    text_archive << mFrameChannel;
    text_archive << mDataChannel;

    text_archive << mSlotsPerFrame;
    text_archive << mBitsPerSlot;
    text_archive << mDataBitsPerSlot;
    text_archive << mShiftOrder;
    text_archive << mDataValidEdge;

    text_archive << mDataAlignment;
    text_archive << mBitAlignment;

    text_archive << mSigned;

    text_archive << mFrameSyncInverted;
    text_archive << mEnableAdvancedAnalysis;

    return SetReturnString( text_archive.GetString() );
}
