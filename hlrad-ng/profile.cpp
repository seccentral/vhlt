// =====================================================================================
// hlrad-ng profiling counters and timers
// =====================================================================================
#include "qrad.h"
#include "profile.h"

#ifdef HLRAD_NG_PROFILE

unsigned long long g_profile_gsamples = 0;
unsigned long long g_profile_gleafchecks = 0;
unsigned long long g_profile_glightiter = 0;
unsigned long long g_profile_gsurvive = 0;
unsigned long long g_profile_tracenodes = 0;
unsigned long long g_profile_tracepackets = 0;
unsigned long long g_profile_tsc[PROFILE_COUNT];

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
    static const char* const names[PROFILE_COUNT] = {"CalcLightmapLuxel", "BuildFacelightsPost", "GatherSampleLight",
                                                     "  sky rays",        "  sun rays",          "  light rays",
                                                     "  sight area",      "  packet kernel"};
    unsigned long long total = g_profile_tsc[PROFILE_LUXEL] + g_profile_tsc[PROFILE_POST];
    int i;

    Log("hlrad-ng profile (exact with -threads 1 only):\n");
    Log("  GatherSampleLight calls : %llu\n", g_profile_gsamples);
    Log("  leaf checks             : %llu\n", g_profile_gleafchecks);
    Log("  light records visited   : %llu\n", g_profile_glightiter);
    Log("  lights reached switch   : %llu\n", g_profile_gsurvive);
    Log("  packets traced          : %llu\n", g_profile_tracepackets);
    Log("  nodes visited by packets: %llu\n", g_profile_tracenodes);
    if (total == 0)
    {
        total = 1;
    }
    Log("  kernel cycles per node  : %.1f\n",
        g_profile_tracenodes ? (double)g_profile_tsc[PROFILE_PACKETS] / (double)g_profile_tracenodes : 0.0);
    for (i = 0; i < PROFILE_COUNT; i++)
    {
        Log("  %-22s: %6.2f%% of luxel+post time\n", names[i], 100.0 * (double)g_profile_tsc[i] / (double)total);
    }
}

#endif
