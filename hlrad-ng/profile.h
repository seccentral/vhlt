// =====================================================================================
// hlrad-ng profiling counters
// =====================================================================================
// Compile-time-off counters for the direct-lighting hot path
// (docs/research/hlrad-optimizations.md, module "profile").
//
// Enable with -DVHLT_HLRAD_NG_PROFILE=ON, which defines HLRAD_NG_PROFILE for
// the hlrad-ng target only. Without the macro the counters do not exist and
// the instrumented code costs nothing.
//
// Counters are incremented from worker threads without locking; races make
// the values approximate, which is acceptable for steering optimizations.
// =====================================================================================
#pragma once

#ifdef HLRAD_NG_PROFILE

extern unsigned long long g_profile_gsamples;     // GatherSampleLight calls
extern unsigned long long g_profile_gleafchecks;  // leaf iterations of the light walk
extern unsigned long long g_profile_glightiter;   // light records visited
extern unsigned long long g_profile_gsurvive;     // non-sky lights that reached the switch
extern unsigned long long g_profile_grays;        // TestLine calls for lights
extern unsigned long long g_profile_goccluded;    // TestLine calls that found geometry
extern unsigned long long g_profile_tsc_luxels;   // cycles inside CalcLightmapLuxel
extern unsigned long long g_profile_tsc_gather;   // cycles inside GatherSampleLight
extern unsigned long long g_profile_tsc_testline; // cycles inside TestLine from the light loop

unsigned long long ProfileTsc();

void ProfileReport();

#endif
