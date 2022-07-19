#ifndef TDM_ANALYZER_SETTINGS
#define TDM_ANALYZER_SETTINGS

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

enum PcmFrameType
{
    FRAME_TRANSITION_TWICE_EVERY_WORD,
    FRAME_TRANSITION_ONCE_EVERY_WORD,
    FRAME_TRANSITION_TWICE_EVERY_FOUR_WORDS
};
enum PcmWordAlignment
{
    LEFT_ALIGNED,
    RIGHT_ALIGNED
};
enum PcmBitAlignment
{
    BITS_SHIFTED_RIGHT_1,
    NO_SHIFT
};
enum PcmWordSelectInverted
{
    WS_INVERTED,
    WS_NOT_INVERTED
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
    U32 mBitsPerWord;

    PcmWordAlignment mWordAlignment;
    PcmFrameType mFrameType;
    PcmBitAlignment mBitAlignment;
    AnalyzerEnums::Sign mSigned;


    PcmWordSelectInverted mWordSelectInverted;

  protected:
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mClockChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mFrameChannelInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mDataChannelInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mShiftOrderInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mDataValidEdgeInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mBitsPerWordInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mFrameTypeInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mWordAlignmentInterface;
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mBitAlignmentInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mSignedInterface;

    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mWordSelectInvertedInterface;
};
#endif // TDM_ANALYZER_SETTINGS
