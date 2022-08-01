#ifndef TDM_ANALYZER_RESULTS
#define TDM_ANALYZER_RESULTS

#include <AnalyzerResults.h>

class TdmAnalyzer;
class TdmAnalyzerSettings;

#define TOO_FEW_BITS_PER_FRAME  ( 1 << 0 )
#define UNEXPECTED_BITS         ( 1 << 1 )
#define MISSED_DATA             ( 1 << 2 )
#define SHORT_SLOT              ( 1 << 3 )
#define MISSED_FRAME_SYNC       ( 1 << 4 )
#define BITCLOCK_ERROR          ( 1 << 5 )
// bits 6 & 7 are used for the warning / error flag

enum TdmResultType
{
    // leave numerical space for channels, frame mType is limited to 8 bits
    // which this is stored in, so keep the max number below 255
};


class TdmAnalyzerResults : public AnalyzerResults
{
  public:
    TdmAnalyzerResults( TdmAnalyzer* analyzer, TdmAnalyzerSettings* settings );
    virtual ~TdmAnalyzerResults();

    virtual void GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base );
    virtual void GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id );

    virtual void GenerateFrameTabularText( U64 frame_index, DisplayBase display_base );
    virtual void GeneratePacketTabularText( U64 packet_id, DisplayBase display_base );
    virtual void GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base );

  protected: // functions
    void GenerateCSV( const char* file, DisplayBase display_base );
    void GenerateWAV( const char* file );
  protected: // vars
    TdmAnalyzerSettings* mSettings;
    TdmAnalyzer* mAnalyzer;
};

#endif // TDM_ANALYZER_RESULTS
