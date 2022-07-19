#ifndef I2S_ANALYZER_SETTINGS
#define I2S_ANALYZER_SETTINGS

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

class I2sAnalyzerSettings : public AnalyzerSettings
{
  public:
    I2sAnalyzerSettings();
    virtual ~I2sAnalyzerSettings();

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
    std::auto_ptr<AnalyzerSettingInterfaceChannel> mClockChannelInterface;
    std::auto_ptr<AnalyzerSettingInterfaceChannel> mFrameChannelInterface;
    std::auto_ptr<AnalyzerSettingInterfaceChannel> mDataChannelInterface;

    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mShiftOrderInterface;
    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mDataValidEdgeInterface;
    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mBitsPerWordInterface;

    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mFrameTypeInterface;
    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mWordAlignmentInterface;
    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mBitAlignmentInterface;

    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mSignedInterface;

    std::auto_ptr<AnalyzerSettingInterfaceNumberList> mWordSelectInvertedInterface;
};
#endif // I2S_ANALYZER_SETTINGS
