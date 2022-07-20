#ifndef TDM_ANALYZER_SETTINGS
#define TDM_ANALYZER_SETTINGS

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

enum TdmFrameType
{
    FRAME_TRANSITION_TWICE_EVERY_WORD,
    FRAME_TRANSITION_ONCE_EVERY_WORD,
    FRAME_TRANSITION_TWICE_EVERY_FOUR_WORDS,
    FRAME_TRANSITION_ONE_BITCLOCK_PER_FRAME
};
enum TdmWordAlignment
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

    TdmWordAlignment mWordAlignment;
    TdmFrameType mFrameType;
    TdmBitAlignment mBitAlignment;
    AnalyzerEnums::Sign mSigned;

    TdmFrameSelectInverted mFrameSyncInverted;

  protected:
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mClockChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mFrameChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mDataChannelInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mSlotsPerFrameInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mBitsPerSlotInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mDataBitsPerSlotInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mShiftOrderInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mDataValidEdgeInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mFrameTypeInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mWordAlignmentInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mBitAlignmentInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mSignedInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mFrameSyncInvertedInterface;
};
#endif // TDM_ANALYZER_SETTINGS
