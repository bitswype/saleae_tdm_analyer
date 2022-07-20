#ifndef TDM_SIMULATION_DATA_GENERATOR
#define TDM_SIMULATION_DATA_GENERATOR

#include "TdmAnalyzerSettings.h"
#include "AnalyzerHelpers.h"

enum RightLeftDirection
{
    Right,
    Left
};
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

    std::unique_ptr<SineGen> mSineWaveSamplesRight;
    std::unique_ptr<SineGen> mSineWaveSamplesLeft;

    ClockGenerator mClockGenerator;

    std::vector<BitState> mFrameBits;
    U32 mCurrentFrameBitIndex;

    std::vector<U64> mBitMasks;
    U32 mNumSlots;

    RightLeftDirection mCurrentAudioChannel;
    U32 mCurrentBitIndex;
    S64 mCurrentWord;
    U32 mPaddingCount;
    BitGenerationState mBitGenerationState;

    // Fake data settings:
    double mAudioSampleRate;
    bool mUseShortFrames;
    U32 mNumPaddingBits;
};
#endif // TDM_SIMULATION_DATA_GENERATOR
