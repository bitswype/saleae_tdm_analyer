#include "TdmAnalyzerSettings.h"

#include <AnalyzerHelpers.h>
#include <sstream>
#include <cstring>
#include <stdio.h>

TdmAnalyzerSettings::TdmAnalyzerSettings()
    : mClockChannel( UNDEFINED_CHANNEL ),
      mFrameChannel( UNDEFINED_CHANNEL ),
      mDataChannel( UNDEFINED_CHANNEL ),
      mTdmFrameRate(48000),

      mSlotsPerFrame( 8 ),
      mBitsPerSlot( 16 ),
      mDataBitsPerSlot( 16 ),
      mShiftOrder( AnalyzerEnums::MsbFirst ),
      mDataValidEdge( AnalyzerEnums::PosEdge ),

      mDataAlignment( LEFT_ALIGNED ),
      mBitAlignment( DSP_MODE_A ),
      mSigned( AnalyzerEnums::UnsignedInteger ),
      mFrameSyncInverted( FS_NOT_INVERTED ),
      mExportFileType( CSV ),
      mEnableAdvancedAnalysis( false ),
      mFrameV2Detail( FV2_FULL ),
      mMarkerDensity( MARKERS_ALL ),
      mAudioBatchSize( 0 )
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

    mTdmFrameRateInterface.reset( new AnalyzerSettingInterfaceInteger() );
    mTdmFrameRateInterface->SetTitleAndTooltip("Frame Rate (Audio Sample Rate) Hz", "Set the number of frames / second");
    mTdmFrameRateInterface->SetMin(1);
    mTdmFrameRateInterface->SetInteger( mTdmFrameRate );

    mSlotsPerFrameInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mSlotsPerFrameInterface->SetTitleAndTooltip( "Number of slots (channels) per TDM frame (slots/frame)",
                                                 "Specify the number of audio channels / TDM frame.  Any additional slots will be ignored" );
    for( U32 i = 1; i <= 256; i++ )
    {
        char str[ 256 ];
        snprintf( str, sizeof( str ), "%d Slot%s/TDM Frame", i, i == 1 ? "" : "s" );
        mSlotsPerFrameInterface->AddNumber( i, str, "" );
    }
    mSlotsPerFrameInterface->SetNumber( mSlotsPerFrame );

    mBitsPerSlotInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mBitsPerSlotInterface->SetTitleAndTooltip( "Number of bits per slot in the TDM frame",
                                               "Specify the number of bits per slot.  There can be more bits in a slot than data bits" );
    for( U32 i = 2; i <= 64; i++ )
    {
        char str[ 256 ];
        snprintf( str, sizeof( str ), "%d bits/slot", i );
        mBitsPerSlotInterface->AddNumber( i, str, "" );
    }
    mBitsPerSlotInterface->SetNumber( mBitsPerSlot );

    mDataBitsPerSlotInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mDataBitsPerSlotInterface->SetTitleAndTooltip( "Audio Bit Depth (data bits/slot, must be <= bits/slot)",
                                                   "Specify the number of audio data bits/channel.  Must be equal or less than bits/slot.  Any additional bits will be ignored" );
    for( U32 i = 2; i <= 64; i++ )
    {
        char str[ 256 ];
        snprintf( str, sizeof( str ), "%d Data bits/slot", i );
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

    // DSP Mode A: data is shifted right 1 bit from frame sync (TDM typical, I2S left-justified)
    // DSP Mode B: data starts on the same clock edge as frame sync (no shift)
    mBitAlignmentInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mBitAlignmentInterface->SetTitleAndTooltip( "DATA Bits Shift relative to Frame Sync", "Specify the bit shift with respect to the FRAME edges" );
    mBitAlignmentInterface->AddNumber( DSP_MODE_A, "Right-shifted by one (TDM typical, DSP mode A)", "" );
    mBitAlignmentInterface->AddNumber( DSP_MODE_B, "No shift (DSP mode B)", "" );
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
    mEnableAdvancedAnalysisInterface->SetTitleAndTooltip("Advanced analysis of TDM signals", 
                        "Enables more in depth analysis of the TDM data stream, identifies more potential problems");
    mEnableAdvancedAnalysisInterface->SetCheckBoxText("Perform more analysis on the TDM signal, may slow down processing");
    mEnableAdvancedAnalysisInterface->SetValue( mEnableAdvancedAnalysis );

    mExportFileTypeInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mExportFileTypeInterface->SetTitleAndTooltip("Select export file type (TXT/CSV will actually be this file type)",
                        "The file export option only shows TXT/CSV, but this dropdown will change what file type is actually generated");
    mExportFileTypeInterface->AddNumber( 0, "TXT/CSV", "Export data as a TXT / CSV");
    mExportFileTypeInterface->AddNumber( 1, "WAV", "Export data as a wave file");
    mExportFileTypeInterface->SetNumber( mExportFileType );

    mFrameV2DetailInterface.reset(new AnalyzerSettingInterfaceNumberList());
    mFrameV2DetailInterface->SetTitleAndTooltip(
        "Data Table / HLA Output",
        "Controls FrameV2 output for the data table and HLA extensions (WAV export, audio streaming). "
        "'Full' includes all diagnostic fields. "
        "'Minimal' includes only fields needed by audio HLAs (faster decode). "
        "'Off' disables FrameV2 entirely for maximum decode speed -- HLA extensions will not receive data.");
    mFrameV2DetailInterface->AddNumber(FV2_FULL, "Full (all diagnostic fields)",
        "All 10 fields including severity, all error booleans, and low sample rate. "
        "Required for complete data table display.");
    mFrameV2DetailInterface->AddNumber(FV2_MINIMAL, "Minimal (audio HLA fields only)",
        "Only slot, data, frame_number, short_slot, and bitclock_error. "
        "Sufficient for WAV export and audio streaming HLAs. Faster decode.");
    mFrameV2DetailInterface->AddNumber(FV2_OFF, "Off (maximum speed, no HLA support)",
        "No FrameV2 output. Bubble text and CSV export still work. "
        "HLA extensions (WAV export, audio streaming) will not receive data.");
    mFrameV2DetailInterface->SetNumber(mFrameV2Detail);

    mMarkerDensityInterface.reset(new AnalyzerSettingInterfaceNumberList());
    mMarkerDensityInterface->SetTitleAndTooltip(
        "Waveform Markers",
        "Controls how many markers are placed on the waveform. "
        "Fewer markers = faster decode. Markers are visual only and do not affect data output.");
    mMarkerDensityInterface->AddNumber(MARKERS_ALL, "All bits (arrows + data dots on every clock edge)",
        "Full annotation: clock arrows and data value dots on every bit. Most detailed but slowest.");
    mMarkerDensityInterface->AddNumber(MARKERS_SLOT_ONLY, "Slot boundaries only",
        "One arrow marker at the start of each slot. 16x fewer markers for 16-bit slots.");
    mMarkerDensityInterface->AddNumber(MARKERS_NONE, "None (fastest)",
        "No waveform markers. Data table and HLA output still work.");
    mMarkerDensityInterface->SetNumber(mMarkerDensity);

    mAudioBatchSizeInterface.reset(new AnalyzerSettingInterfaceNumberList());
    mAudioBatchSizeInterface->SetTitleAndTooltip(
        "Audio Batch Size",
        "Batch multiple TDM frames into a single FrameV2 for real-time HLA streaming. "
        "When enabled, the 'Data Table / HLA Output' setting is ignored and only batch "
        "frames are emitted. Use 'Off' for protocol debugging and data table inspection; "
        "use batching for real-time audio streaming via the Audio Stream HLA.");
    mAudioBatchSizeInterface->AddNumber(0, "Off (one FrameV2 per slot)",
        "Current behavior. Each slot emits its own FrameV2. Best for protocol debugging.");
    for (U32 n = 1; n <= 1024; n *= 2)
    {
        char str[64];
        snprintf(str, sizeof(str), "%u TDM frame%s per FrameV2", n, n == 1 ? "" : "s");
        char tooltip[128];
        snprintf(tooltip, sizeof(tooltip),
            "Emit one FrameV2 every %u TDM frame%s with packed PCM data.",
            n, n == 1 ? "" : "s");
        mAudioBatchSizeInterface->AddNumber(n, str, tooltip);
    }
    mAudioBatchSizeInterface->SetNumber(mAudioBatchSize);

    AddInterface( mClockChannelInterface.get() );
    AddInterface( mFrameChannelInterface.get() );
    AddInterface( mDataChannelInterface.get() );
    AddInterface( mTdmFrameRateInterface.get() );
    AddInterface( mSlotsPerFrameInterface.get() );
    AddInterface( mBitsPerSlotInterface.get() );
    AddInterface( mDataBitsPerSlotInterface.get() );
    AddInterface( mShiftOrderInterface.get() );
    AddInterface( mDataValidEdgeInterface.get() );
    AddInterface( mWordAlignmentInterface.get() );
    AddInterface( mBitAlignmentInterface.get() );
    AddInterface( mSignedInterface.get() );
    AddInterface( mFrameSyncInvertedInterface.get() );
    AddInterface( mExportFileTypeInterface.get() );
    AddInterface( mEnableAdvancedAnalysisInterface.get() );
    AddInterface( mFrameV2DetailInterface.get() );
    AddInterface( mMarkerDensityInterface.get() );
    AddInterface( mAudioBatchSizeInterface.get() );

    // AddExportOption( 0, "Export as text/csv file", "text (*.txt);;csv (*.csv)" );
    AddExportOption( 0, "Export as text/csv file" );
    AddExportExtension( 0, "text", "txt" );
    AddExportExtension( 0, "csv", "csv" );

    AddExportOption( 1, "Export as wav file" );
    AddExportExtension( 1, "wav", "wav" );

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

    mTdmFrameRateInterface->SetInteger( mTdmFrameRate );
    mSlotsPerFrameInterface->SetNumber( mSlotsPerFrame );
    mBitsPerSlotInterface->SetNumber( mBitsPerSlot );
    mDataBitsPerSlotInterface->SetNumber( mDataBitsPerSlot );
    mShiftOrderInterface->SetNumber( mShiftOrder );
    mDataValidEdgeInterface->SetNumber( mDataValidEdge );

    mWordAlignmentInterface->SetNumber( mDataAlignment );
    mBitAlignmentInterface->SetNumber( mBitAlignment );

    mSignedInterface->SetNumber( mSigned );

    mFrameSyncInvertedInterface->SetNumber( mFrameSyncInverted );
    mExportFileTypeInterface->SetNumber( mExportFileType );
    mEnableAdvancedAnalysisInterface->SetValue( mEnableAdvancedAnalysis );
    mFrameV2DetailInterface->SetNumber(mFrameV2Detail);
    mMarkerDensityInterface->SetNumber(mMarkerDensity);
    mAudioBatchSizeInterface->SetNumber(mAudioBatchSize);
}

static void FormatHzString( char* buf, size_t buf_size, U64 hz )
{
    if( hz >= 1000000ULL )
        snprintf( buf, buf_size, "%llu MHz", (unsigned long long)( hz / 1000000ULL ) );
    else if( hz >= 1000ULL )
        snprintf( buf, buf_size, "%llu kHz", (unsigned long long)( hz / 1000ULL ) );
    else
        snprintf( buf, buf_size, "%llu Hz", (unsigned long long)hz );
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

    mTdmFrameRate = U32( mTdmFrameRateInterface->GetInteger() );
    mSlotsPerFrame = U32( mSlotsPerFrameInterface->GetNumber() );
    mBitsPerSlot = U32( mBitsPerSlotInterface->GetNumber() );
    mDataBitsPerSlot = U32( mDataBitsPerSlotInterface->GetNumber() );

    // Phase 6: Zero-parameter guards — reject before any arithmetic
    if( mTdmFrameRate == 0 )
    {
        SetErrorText( "Frame rate must be greater than zero" );
        return false;
    }
    if( mSlotsPerFrame == 0 )
    {
        SetErrorText( "Slots per frame must be greater than zero" );
        return false;
    }
    if( mBitsPerSlot == 0 )
    {
        SetErrorText( "Bits per slot must be greater than zero" );
        return false;
    }

    if( mDataBitsPerSlot > mBitsPerSlot )
    {
        SetErrorText( "Number of data bits per slot must be less than or equal to number of bits per slot" );
        return false;
    }

    // Phase 6: Hard block — physically impossible bit clock rate
    U64 bit_clock_hz = U64( mTdmFrameRate ) * U64( mSlotsPerFrame ) * U64( mBitsPerSlot );
    if( bit_clock_hz > kMaxBitClockHz )
    {
        char rate_str[ 32 ];
        FormatHzString( rate_str, sizeof( rate_str ), bit_clock_hz );
        char msg[ 256 ];
        snprintf( msg, sizeof( msg ),
            "TDM configuration requires %s bit clock "
            "(%llu Hz x %u slots x %u bits), exceeding maximum 500 MHz. "
            "Reduce frame rate, slots per frame, or bits per slot.",
            rate_str,
            (unsigned long long)mTdmFrameRate,
            (unsigned)mSlotsPerFrame,
            (unsigned)mBitsPerSlot );
        SetErrorText( msg );
        return false;
    }

    mShiftOrder = AnalyzerEnums::ShiftOrder( U32( mShiftOrderInterface->GetNumber() ) );
    mDataValidEdge = AnalyzerEnums::EdgeDirection( U32( mDataValidEdgeInterface->GetNumber() ) );

    mDataAlignment = TdmDataAlignment( U32( mWordAlignmentInterface->GetNumber() ) );
    mBitAlignment = TdmBitAlignment( U32( mBitAlignmentInterface->GetNumber() ) );

    mSigned = AnalyzerEnums::Sign( U32( mSignedInterface->GetNumber() ) );

    mFrameSyncInverted = TdmFrameSelectInverted( U32( mFrameSyncInvertedInterface->GetNumber() ) );
    mExportFileType = ExportFileType( U32( mExportFileTypeInterface->GetNumber() ) );
    mEnableAdvancedAnalysis = bool(mEnableAdvancedAnalysisInterface->GetValue() );
    mFrameV2Detail = TdmFrameV2Detail(U32(mFrameV2DetailInterface->GetNumber()));
    mMarkerDensity = TdmMarkerDensity(U32(mMarkerDensityInterface->GetNumber()));
    mAudioBatchSize = U32(mAudioBatchSizeInterface->GetNumber());

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

    U32 tdm_frame_rate;
    if( text_archive >> tdm_frame_rate )
    {
        mTdmFrameRate = tdm_frame_rate;
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

    ExportFileType export_file_type;
    if( text_archive >> *( U32* )&export_file_type )
    {
        mExportFileType = export_file_type;
    }

    bool enable_advanced;
    if (text_archive >> enable_advanced)
    {
        mEnableAdvancedAnalysis = enable_advanced;
    }

    U32 fv2_detail;
    if (text_archive >> fv2_detail)
    {
        mFrameV2Detail = TdmFrameV2Detail(fv2_detail);
    }

    U32 marker_density;
    if (text_archive >> marker_density)
    {
        mMarkerDensity = TdmMarkerDensity(marker_density);
    }

    U32 audio_batch_size;
    if (text_archive >> audio_batch_size)
    {
        mAudioBatchSize = audio_batch_size;
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

    text_archive << mTdmFrameRate;
    text_archive << mSlotsPerFrame;
    text_archive << mBitsPerSlot;
    text_archive << mDataBitsPerSlot;
    text_archive << mShiftOrder;
    text_archive << mDataValidEdge;

    text_archive << mDataAlignment;
    text_archive << mBitAlignment;

    text_archive << mSigned;

    text_archive << mFrameSyncInverted;
    text_archive << mExportFileType;
    text_archive << mEnableAdvancedAnalysis;
    text_archive << mFrameV2Detail;
    text_archive << mMarkerDensity;
    text_archive << mAudioBatchSize;

    return SetReturnString( text_archive.GetString() );
}
