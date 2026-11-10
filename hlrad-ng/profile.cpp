// =====================================================================================
// hlrad-ng profiling counters
// =====================================================================================
#include "qrad.h"
#include "profile.h"

#ifdef HLRAD_NG_PROFILE

unsigned long long g_profile_gsamples = 0;
unsigned long long g_profile_gleafchecks = 0;
unsigned long long g_profile_glightiter = 0;
unsigned long long g_profile_gsurvive = 0;
unsigned long long g_profile_grays = 0;
unsigned long long g_profile_goccluded = 0;
unsigned long long g_profile_tsc_luxels = 0;
unsigned long long g_profile_tsc_gather = 0;
unsigned long long g_profile_tsc_testline = 0;

unsigned long long ProfileTsc()
{
#if defined(__x86_64__) || defined(_M_X64)
    return __builtin_ia32_rdtsc();
#else
    return 0;
#endif
}

void ProfileReport()
{
    Log("hlrad-ng profile (approximate, racy counters):\n");
    Log("  GatherSampleLight calls : %llu\n", g_profile_gsamples);
    Log("  leaf checks             : %llu\n", g_profile_gleafchecks);
    Log("  light records visited   : %llu\n", g_profile_glightiter);
    Log("  lights reached switch   : %llu\n", g_profile_gsurvive);
    Log("  TestLine calls          : %llu\n", g_profile_grays);
    Log("  occluded                : %llu\n", g_profile_goccluded);
    Log("  TSC luxels              : %llu\n", g_profile_tsc_luxels);
    Log("  TSC gather              : %llu\n", g_profile_tsc_gather);
    Log("  TSC TestLine            : %llu\n", g_profile_tsc_testline);
}

#endif
