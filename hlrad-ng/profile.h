// =====================================================================================
// hlrad-ng profiling counters and timers
// =====================================================================================
// Compile-time-off instrumentation for the direct-lighting hot path.
//
// Enable with -DVHLT_HLRAD_NG_PROFILE=ON, which defines HLRAD_NG_PROFILE for
// the hlrad-ng target only. Without the macro nothing here exists and the
// instrumented code costs nothing.
//
// Counters and timers are updated from worker threads without locking. Run
// with -threads 1 for exact numbers; with more threads they are approximate.
// =====================================================================================
#pragma once

#ifdef HLRAD_NG_PROFILE

extern unsigned long long g_profile_gsamples;     // GatherSampleLight calls
extern unsigned long long g_profile_gleafchecks;  // leaf iterations of the light walk
extern unsigned long long g_profile_glightiter;   // light records visited
extern unsigned long long g_profile_gsurvive;     // non-sky lights that reached the switch
extern unsigned long long g_profile_tracenodes;   // tree nodes and leaves visited by packets
extern unsigned long long g_profile_tracepackets; // packets traced

// Timed sections. A section nested in another is counted in both.
enum
{
    PROFILE_LUXEL,     // CalcLightmapLuxel
    PROFILE_POST,      // BuildFacelightsPost
    PROFILE_GATHER,    // GatherSampleLight
    PROFILE_SKY,       // sky visibility rays
    PROFILE_SUN,       // sun rays
    PROFILE_LIGHTRAYS, // light visibility rays and their accumulation
    PROFILE_SIGHTAREA, // near-range texlight integration
    PROFILE_PACKETS,   // packet kernel (part of the three ray sections)
    PROFILE_COUNT
};
extern unsigned long long g_profile_tsc[PROFILE_COUNT];

unsigned long long ProfileTsc();

// Adds the time between construction and destruction to one section.
struct ProfileScope
{
    int section;
    unsigned long long start;
    explicit ProfileScope(const int id) : section(id), start(ProfileTsc())
    {
    }
    ~ProfileScope()
    {
        g_profile_tsc[section] += ProfileTsc() - start;
    }
};
#define PROFILE_SCOPE(id) ProfileScope profile_scope_##id(id)

void ProfileReport();

#else

#define PROFILE_SCOPE(id)

#endif
