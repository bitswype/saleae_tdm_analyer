// Compile-time performance instrumentation for the TDM analyzer.
//
// When TDM_PROFILE is defined, macros expand to scoped timers that
// accumulate per-section call counts and elapsed time. When not
// defined, macros expand to nothing (zero overhead).
//
// Usage in analyzer code:
//   TDM_PROFILE_SCOPE("SectionName")   -- time the enclosing scope
//
// Usage in benchmark:
//   TDM_PROFILE_PRINT()   -- print summary table
//   TDM_PROFILE_RESET()   -- clear all counters for next test
//
// Enable via: cmake -DENABLE_PROFILING=ON

#pragma once

#ifdef TDM_PROFILE

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

struct ProfileCounter
{
    const char* name;
    uint64_t calls;
    double total_us; // microseconds
};

inline std::vector<ProfileCounter>& GetProfileCounters()
{
    static std::vector<ProfileCounter> counters;
    return counters;
}

inline size_t RegisterProfileCounter( const char* name )
{
    auto& counters = GetProfileCounters();
    counters.push_back( { name, 0, 0.0 } );
    return counters.size() - 1;
}

inline void ResetProfileCounters()
{
    for( auto& c : GetProfileCounters() )
    {
        c.calls = 0;
        c.total_us = 0.0;
    }
}

inline void PrintProfileSummary()
{
    auto& counters = GetProfileCounters();
    if( counters.empty() )
        return;
    printf( "    %-32s %12s %12s %12s\n",
            "Section", "Calls", "Total (ms)", "Per-call (us)" );
    for( const auto& c : counters )
    {
        if( c.calls == 0 )
            continue;
        printf( "    %-32s %12llu %12.1f %12.3f\n",
                c.name,
                (unsigned long long)c.calls,
                c.total_us / 1000.0,
                c.total_us / double( c.calls ) );
    }
}

class ScopedProfileTimer
{
  public:
    ScopedProfileTimer( size_t idx )
      : mIdx( idx ), mStart( std::chrono::steady_clock::now() )
    {
    }

    ~ScopedProfileTimer()
    {
        auto end = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>( end - mStart ).count();
        auto& c = GetProfileCounters()[ mIdx ];
        c.calls++;
        c.total_us += us;
    }

  private:
    size_t mIdx;
    std::chrono::steady_clock::time_point mStart;
};

// Each TDM_PROFILE_SCOPE at a unique source location registers its counter
// once (via static local) and times the enclosing scope on every call.
#define TDM_PROFILE_CONCAT2( a, b ) a##b
#define TDM_PROFILE_CONCAT( a, b ) TDM_PROFILE_CONCAT2( a, b )

#define TDM_PROFILE_SCOPE( name )                                                        \
    static size_t TDM_PROFILE_CONCAT( _prof_idx_, __LINE__ ) = RegisterProfileCounter( name ); \
    ScopedProfileTimer TDM_PROFILE_CONCAT( _prof_timer_, __LINE__ )(                     \
        TDM_PROFILE_CONCAT( _prof_idx_, __LINE__ ) )

#define TDM_PROFILE_RESET() ResetProfileCounters()
#define TDM_PROFILE_PRINT() PrintProfileSummary()

#else // TDM_PROFILE not defined

#define TDM_PROFILE_SCOPE( name )
#define TDM_PROFILE_RESET()
#define TDM_PROFILE_PRINT()

#endif // TDM_PROFILE
