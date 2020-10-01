#ifndef I2S_ANALYZER_RESULTS
#define I2S_ANALYZER_RESULTS

#include <AnalyzerResults.h>

class I2sAnalyzer;
class I2sAnalyzerSettings;

enum I2sResultType
{
    Channel1,
    Channel2,
    ErrorTooFewBits,
    ErrorDoesntDivideEvenly
};


class I2sAnalyzerResults : public AnalyzerResults
{
  public:
    I2sAnalyzerResults( I2sAnalyzer* analyzer, I2sAnalyzerSettings* settings );
    virtual ~I2sAnalyzerResults();

    virtual void GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base );
    virtual void GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id );

    virtual void GenerateFrameTabularText( U64 frame_index, DisplayBase display_base );
    virtual void GeneratePacketTabularText( U64 packet_id, DisplayBase display_base );
    virtual void GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base );

  protected: // functions
  protected: // vars
    I2sAnalyzerSettings* mSettings;
    I2sAnalyzer* mAnalyzer;
};

#endif // I2S_ANALYZER_RESULTS
