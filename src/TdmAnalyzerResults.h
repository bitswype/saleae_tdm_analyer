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

#pragma scalar_storage_order little-endian
#pragma pack(push, 1)
typedef struct
{
    char mRiffCkId[4] = {'R', 'I', 'F', 'F'};   // @ 0
    U32 mRiffCkSize = 36;                       // @ 4
    char mRiffWaveId[4] = {'W', 'A', 'V', 'E'}; // @ 8, 4
    char mFmtCkId[4] = {'f', 'm', 't', ' '};    // @ 12, 8
    U32 mFmtCkSize = 16;                        // @ 16, 12
    U16 mFormatTag = 0x0001;                    // @ 20, 14 + 2
    U16 mNumChannels = 1;                       // @ 22, 16 + 4
    U32 mSamplesPerSec = 8000;                  // @ 24, 20 + 8
    U32 mBytesPerSec = 16000;                   // @ 28, 24 + 12
    U16 mBlockSizeBytes = 2;                    // @ 32, 26 + 14
    U16 mBitsPerSample = 16;                    // @ 34, 28 + 16
    char mDataCkId[4] = {'d' , 'a', 't', 'a'};  // @ 36, 32
    U32 mDataCkSize = 0;                        // @ 40, 36
    /* data */                                  // @ 44
} WavePCMHeader;
#pragma pack(pop)
#pragma scalar_storage_order default

class PCMWaveFileHandler
{
  public:
    PCMWaveFileHandler(std::ofstream & file, U32 sample_rate = 48000, U32 num_channels = 2, U32 bits_per_channel = 32);
    ~PCMWaveFileHandler();

    void addSample(U64 sample);
    void close(void);

  private:
    void writeLittleEndianData(U64 value, U8 num_bytes);
    void updateFileSize();

  private:
    WavePCMHeader mWaveHeader;
    U32 mNumChannels;
    U32 mBitsPerChannel;
    U8 mBitShift;
    U32 mBytesPerChannel;
    U32 mSampleRate;
    U32 mFrameSizeBytes;
    U32 mSampleCount;
    U32 mTotalFrames;
    char mBuf[128];
    std::ofstream & mFile;
    std::streampos mWtPosSaved;

    constexpr static U64 RIFF_CKSIZE_POS = 4;
    constexpr static U64 DATA_CKSIZE_POS = 40;
};

#endif // TDM_ANALYZER_RESULTS
