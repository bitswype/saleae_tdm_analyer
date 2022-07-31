#ifndef TDM_SIMULATION_DATA_GENERATOR
#define TDM_SIMULATION_DATA_GENERATOR

#define _USE_MATH_DEFINES
#include "TdmAnalyzerSettings.h"
#include "AnalyzerHelpers.h"
#include <math.h>

enum BitGenerationState
{
    Init,
    LeftPadding,
    Data,
    RightPadding
};

class SineGen
{
  public:
  SineGen(double sample_rate = 1000.0, double frequency_hz = 1.0, double scale = 1.0, double phase_degrees = 0.0);
  ~SineGen();

  double GetNextValue();
  void Reset();

  protected:
  double mSampleRate;
  double mFrequency;
  double mScale;
  double mPhase;
  U32 mSample;
};

class CountGen
{
  public:
  CountGen(U64 start_val, U64 max_val);
  ~CountGen();

  U64 GetNextValue();
  void Reset();

  protected:
  U64 mMaxVal;
  U64 mVal;
};

class StaticGen
{
  public:
  StaticGen(U64 val);
  ~StaticGen();

  U64 GetNextValue();
  void Reset();

  protected:
  U64 mVal;
};

class TdmSimulationDataGenerator
{
  public:
    TdmSimulationDataGenerator();
    ~TdmSimulationDataGenerator();

    void Initialize( U32 simulation_sample_rate, TdmAnalyzerSettings* settings );
    U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );

  protected:
    TdmAnalyzerSettings* mSettings;
    U32 mSimulationSampleRateHz;

    SimulationChannelDescriptorGroup mSimulationChannels;
    SimulationChannelDescriptor* mClock;
    SimulationChannelDescriptor* mFrame;
    SimulationChannelDescriptor* mData;

  protected: // TDM specific
    void InitSineWave();
    void WriteBit( BitState data, BitState frame );
    S64 GetNextAudioWord();
    BitState GetNextAudioBit();
    BitState GetNextFrameBit();

    std::vector<std::unique_ptr<SineGen>> mVecSineGen;
    std::vector<std::unique_ptr<CountGen>> mVecCountGen;

    ClockGenerator mClockGenerator;

    std::vector<BitState> mFrameBits;
    U32 mCurrentFrameBitIndex;

    std::vector<U64> mBitMasks;

    // TDM decode parameters
    AnalyzerEnums::ShiftOrder mShiftOrder;
    AnalyzerEnums::EdgeDirection mDataValidEdge;
    U32 mNumSlots;
    U32 mBitsPerSlot;
    U32 mDataBitsPerSlot;

    TdmDataAlignment mDataAlignment;
    TdmBitAlignment mBitAlignment;
    AnalyzerEnums::Sign mSigned;
    TdmFrameSelectInverted mFrameSyncInverted;

    U32 mCurrentAudioChannel;
    U32 mCurrentBitIndex;
    S64 mCurrentWord;
    U32 mPaddingCount;
    BitGenerationState mBitGenerationState;

    // Fake data settings:
    double mAudioSampleRate;
};
#endif // TDM_SIMULATION_DATA_GENERATOR
