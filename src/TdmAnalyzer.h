#ifndef TDM_ANALYZER_H
#define TDM_ANALYZER_H

#include <Analyzer.h>
#include "TdmAnalyzerResults.h"
#include "TdmSimulationDataGenerator.h"

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

  protected:
    std::unique_ptr<TdmAnalyzerSettings> mSettings;
    std::unique_ptr<TdmAnalyzerResults> mResults;
    U64 mSampleRate;
    double mDesiredBitClockPeriod;
    Frame mResultsFrame;
    FrameV2 mResultsFrameV2;
    bool mSimulationInitilized;
    TdmSimulationDataGenerator mSimulationDataGenerator;

    AnalyzerChannelData* mClock;
    AnalyzerChannelData* mFrame;
    AnalyzerChannelData* mData;

    AnalyzerResults::MarkerType mArrowMarker;

    BitState mCurrentDataState;
    BitState mCurrentFrameState;
    U64 mCurrentSample;
    U8 mBitFlag;
    U32 mSlotNum;

    BitState mLastDataState;
    BitState mLastFrameState;
    U64 mLastSample;

    std::vector<BitState> mDataBits;
    std::vector<U64> mDataValidEdges;
    std::vector<U8> mDataFlags;
#pragma warning( pop )
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif // TDM_ANALYZER_H
