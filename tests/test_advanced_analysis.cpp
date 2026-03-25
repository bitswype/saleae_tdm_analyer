// Tests advanced analysis error detection using hand-crafted signals with
// specific timing anomalies. Each test verifies the analyzer flags the
// correct error when a stretched clock, data glitch, or frame sync glitch
// is present. Signal construction is in RunBitclockErrorSignal(),
// RunMissedDataSignal(), and RunMissedFrameSyncSignal() in
// tdm_test_helpers.cpp.

#include "tdm_test_helpers.h"

void test_bitclock_error_detection()
{
    auto frames = RunBitclockErrorSignal();

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & BITCLOCK_ERROR )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected BITCLOCK_ERROR flag from stretched clock cycle" );
}

// MISSED_DATA detection: signal has a data glitch between clock edges.
void test_missed_data_detection()
{
    auto frames = RunMissedDataSignal();

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & MISSED_DATA )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected MISSED_DATA flag from data glitch between clock edges" );
}

// MISSED_FRAME_SYNC detection: signal has a frame sync glitch between
// clock edges.
void test_missed_frame_sync_detection()
{
    auto frames = RunMissedFrameSyncSignal();

    bool found = false;
    for( const auto& f : frames )
    {
        if( f.flags & MISSED_FRAME_SYNC )
        {
            found = true;
            break;
        }
    }
    CHECK( found, "Expected MISSED_FRAME_SYNC flag from FS glitch between clock edges" );
}
