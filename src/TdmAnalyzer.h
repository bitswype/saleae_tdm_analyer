#ifndef TDM_ANALYZER_H
#define TDM_ANALYZER_H

#include <Analyzer.h>
#include "TdmAnalyzerResults.h"
#include "TdmSimulationDataGenerator.h"
#ifdef TDM_BENCHMARK_TIMING
#include <chrono>
#endif

class TdmAnalyzerSettings;
class TdmAnalyzer : public Analyzer2
{
  public:
    TdmAnalyzer();
    virtual ~TdmAnalyzer();
    virtual void SetupResults();
    virtual void WorkerThread();

    virtual U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );
    virtual U32 GetMinimumSampleRateHz();

    const char* GetAnalyzerName() const;
    virtual bool NeedsRerun();

#pragma warning( push )
#pragma warning(                                                                                                                           \
    disable : 4251 ) // warning C4251: 'TdmAnalyzer::<...>' : class <...> needs to have dll-interface to be used by clients of class

  protected: // functions
    void AnalyzeTdmSlot();
    void SetupForGettingFirstTdmFrame();
    void GetTdmFrame();
    void SetupForGettingFirstBit();
    void GetNextBit( BitState& data, BitState& frame, U64& sample_number );
    void GetNextDataBit( BitState& data, U64& sample_number );
    void AccumulateSlotIntoBatch( S64 signed_value );
    void EmitAudioBatch();

  protected:
    // Hot-path decode state — grouped for cache locality
    std::unique_ptr<TdmAnalyzerSettings> mSettings;
    std::unique_ptr<TdmAnalyzerResults> mResults;

    AnalyzerChannelData* mClock;
    AnalyzerChannelData* mFrame;
    AnalyzerChannelData* mData;

    AnalyzerResults::MarkerType mArrowMarker;

    BitState mCurrentDataState;
    BitState mCurrentFrameState;
    U64 mCurrentSample;
    U8 mBitFlag;
    U32 mSlotNum;
    U64 mFrameNum;

    BitState mLastDataState;
    BitState mLastFrameState;
    U64 mLastSample;

    std::vector<BitState> mDataBits;
    std::vector<U64> mDataValidEdges;
    std::vector<U8> mDataFlags;

    Frame mResultsFrame;
    U64 mSampleRate;
    double mDesiredBitClockPeriod;
    bool mLowSampleRate;

    // Audio batch mode state
    std::vector<U8> mBatchBuffer;
    U32 mBatchFrameCount;
    U32 mBatchBytesPerFrame;
    U32 mBatchBytesPerSample;
    U64 mBatchStartSample;
    U64 mBatchEndSample;
    U64 mBatchStartFrameNum;

#ifdef TDM_BENCHMARK_TIMING
    // Self-timing: written to %USERPROFILE%\tdm_benchmark_timing.json on destruction.
    // Enable via: cmake -DENABLE_BENCHMARK_TIMING=ON
    std::chrono::steady_clock::time_point mDecodeStart;
    std::chrono::steady_clock::time_point mDecodeLastFrame;
    bool mDecodeStarted = false;
#endif

    // Cold members — simulation support
    bool mSimulationInitilized;
    TdmSimulationDataGenerator mSimulationDataGenerator;
#pragma warning( pop )
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif // TDM_ANALYZER_H
