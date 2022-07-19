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
    disable : 4251 ) // warning C4251: 'I2sAnalyzer::<...>' : class <...> needs to have dll-interface to be used by clients of class

  protected: // functions
    void AnalyzeSubFrame( U32 starting_index, U32 num_bits, U32 subframe_index );
    void AnalyzeFrame();
    void SetupForGettingFirstFrame();
    void GetFrame();
    void SetupForGettingFirstBit();
    void GetNextBit( BitState& data, BitState& frame, U64& sample_number );

  protected:
    std::auto_ptr<TdmAnalyzerSettings> mSettings;
    std::auto_ptr<TdmAnalyzerResults> mResults;
    bool mSimulationInitilized;
    I2sSimulationDataGenerator mSimulationDataGenerator;

    AnalyzerChannelData* mClock;
    AnalyzerChannelData* mFrame;
    AnalyzerChannelData* mData;

    AnalyzerResults::MarkerType mArrowMarker;

    BitState mCurrentData;
    BitState mCurrentFrame;
    U64 mCurrentSample;

    BitState mLastData;
    BitState mLastFrame;
    U64 mLastSample;

    std::vector<BitState> mDataBits;
    std::vector<U64> mDataValidEdges;
#pragma warning( pop )
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif // TDM_ANALYZER_H
