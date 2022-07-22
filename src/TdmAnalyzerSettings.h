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
    BITS_SHIFTED_RIGHT_1,
    NO_SHIFT
};
enum TdmFrameSelectInverted
{
    FS_INVERTED,
    FS_NOT_INVERTED
};

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

  protected:
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mClockChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mFrameChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mDataChannelInterface;

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
};
#endif // TDM_ANALYZER_SETTINGS
