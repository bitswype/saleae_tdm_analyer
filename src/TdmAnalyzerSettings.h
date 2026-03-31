#ifndef TDM_ANALYZER_SETTINGS
#define TDM_ANALYZER_SETTINGS

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

enum TdmDataAlignment
{
    LEFT_ALIGNED,
    RIGHT_ALIGNED
};
enum TdmBitAlignment
{
    DSP_MODE_A,  // data shifted right 1 bit from frame sync (TDM typical / I2S left-justified)
    DSP_MODE_B   // data starts on same clock edge as frame sync (no shift)
};
enum TdmFrameSelectInverted
{
    FS_INVERTED,
    FS_NOT_INVERTED
};

enum ExportFileType
{
  CSV,
  WAV,
};

enum TdmFrameV2Detail
{
    FV2_FULL,      // All 10 FrameV2 fields (default)
    FV2_MINIMAL,   // Only: slot, data, frame_number, short_slot, bitclock_error
    FV2_OFF        // No FrameV2 output (V1 Frame only)
};

enum TdmMarkerDensity
{
    MARKERS_ALL,        // Per-bit markers (current behavior, default)
    MARKERS_SLOT_ONLY,  // One marker per slot boundary
    MARKERS_NONE        // No markers
};

// Sample rate validation thresholds (Phase 6)
static constexpr U64 kMaxBitClockHz = 500000000ULL;   // 500 MHz — Logic 2 Pro maximum
static constexpr U32 kMinOversampleRatio = 4;          // 4x oversampling for reliable edge detection

class TdmAnalyzerSettings : public AnalyzerSettings
{
  public:
    TdmAnalyzerSettings();
    virtual ~TdmAnalyzerSettings();

    virtual bool
    SetSettingsFromInterfaces(); // Get the settings out of the interfaces, validate them, and save them to your local settings vars.
    virtual void LoadSettings( const char* settings ); // Load your settings from a string.
    virtual const char* SaveSettings();                // Save your settings to a string.

    void UpdateInterfacesFromSettings();

    Channel mClockChannel;
    Channel mFrameChannel;
    Channel mDataChannel;

    U32 mTdmFrameRate;
    AnalyzerEnums::ShiftOrder mShiftOrder;
    AnalyzerEnums::EdgeDirection mDataValidEdge;
    U32 mBitsPerSlot;
    U32 mDataBitsPerSlot;
    U32 mSlotsPerFrame;

    TdmDataAlignment mDataAlignment;
    TdmBitAlignment mBitAlignment;
    AnalyzerEnums::Sign mSigned;

    TdmFrameSelectInverted mFrameSyncInverted;
    bool mEnableAdvancedAnalysis;
    ExportFileType mExportFileType;

    TdmFrameV2Detail mFrameV2Detail;
    TdmMarkerDensity mMarkerDensity;
    U32 mAudioBatchSize;  // 0 = off, 1..1024 = TDM frames per FrameV2

  protected:
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mClockChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mFrameChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mDataChannelInterface;

    std::unique_ptr<AnalyzerSettingInterfaceInteger> mTdmFrameRateInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mSlotsPerFrameInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mBitsPerSlotInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mDataBitsPerSlotInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mShiftOrderInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mDataValidEdgeInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mWordAlignmentInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mBitAlignmentInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mSignedInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mFrameSyncInvertedInterface;
    std::unique_ptr<AnalyzerSettingInterfaceBool> mEnableAdvancedAnalysisInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mExportFileTypeInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mFrameV2DetailInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mMarkerDensityInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mAudioBatchSizeInterface;
};
#endif // TDM_ANALYZER_SETTINGS
