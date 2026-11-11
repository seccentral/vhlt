#include "qrad.h"
#include "gpu.h"

#include <algorithm>
#include <utility>
#include <vector>

////////////////////////////
// begin old vismat.c
//
#define HALFBIT

// =====================================================================================
//
//      VISIBILITY MATRIX
//      Determine which patches can see each other
//      Use the PVS to accelerate if available
//
// =====================================================================================

static byte* s_vismatrix;


#ifndef HLRAD_TRANSPARENCY_CPP
#ifdef HLRAD_HULLU
// =====================================================================================
//      OPACITY ARRAY
// =====================================================================================

typedef struct
{
    size_t bitpos;
    vec3_t transparency;
} transparency_t;

static transparency_t* s_transparency_list = NULL;
static unsigned long s_transparency_count = 0;
static unsigned long s_max_transparency_count = 0;

static void FindOpacity(const size_t bitpos, vec3_t& out)
{
    for (unsigned long i = 0; i < s_transparency_count; i++)
    {
        if (s_transparency_list[i].bitpos == bitpos)
        {
            VectorCopy(s_transparency_list[i].transparency, out);
            return;
        }
    }
    VectorFill(out, 1.0);
}

#endif /*HLRAD_HULLU*/
#endif

// hlrad-ng: while set, a close pair that turns out visible is listed here
// (lower patch, higher patch) instead of being set in the matrix: the
// radiosity is on the GPU then, and these pairs keep their transfers on the CPU.
static std::vector<unsigned>* g_gpurad_pairs = NULL;

// hlrad-ng: pairs of one patch whose world trace is waiting for a packet.
typedef struct
{
    vec3_t stops[TESTLINE_PACKET_MAX];
    unsigned patches[TESTLINE_PACKET_MAX];
    int count;
} pendingpairs_t;

// Second half of the pair test, for a pair the world does not block: the
// opaque entities, then the vis bit.
#if !defined(HLRAD_ACCURATEBOUNCE_ALTERNATEORIGIN) || !defined(HLRAD_HULLU) || !defined(HLRAD_OPAQUE_STYLE) ||         \
    !defined(HLRAD_OPAQUE_STYLE_BOUNCE) || !defined(HLRAD_TRANSPARENCY_CPP) || !defined(HLRAD_WATERBLOCKLIGHT) ||      \
    !defined(HLRAD_ENTITYBOUNCE_FIX) || !defined(HLRAD_VISMATRIX_NOMARKSURFACES)
#error "hlrad-ng packet tracing needs the default hlrad feature set"
#endif
static void FinishVisiblePair(const unsigned patchnum, const unsigned m, const size_t bitpos, const vec3_t origin1,
                              const vec3_t origin2)
{
    vec3_t transparency = {1.0, 1.0, 1.0};
    int opaquestyle = -1;

    if (TestSegmentAgainstOpaqueList(origin1, origin2, transparency, opaquestyle))
    {
        return;
    }

    if (opaquestyle != -1)
    {
        AddStyleToStyleArray(m, patchnum, opaquestyle);
        AddStyleToStyleArray(patchnum, m, opaquestyle);
    }

    if (g_gpurad_pairs)
    {
        ThreadLock();
        g_gpurad_pairs->push_back(patchnum);
        g_gpurad_pairs->push_back(m);
        ThreadUnlock();
        return;
    }

    // patchnum can see patch m
    const size_t bitset = bitpos + m;

    if (g_customshadow_with_bouncelight && !VectorCompare(transparency, vec3_one))
    // zhlt3.4: if(g_customshadow_with_bouncelight && VectorCompare(transparency, vec3_one)) . --vluzacn
    {
        AddTransparencyToRawArray(patchnum, m, transparency);
    }

    // Rows of different threads can share a byte, so the update is atomic; a
    // global lock here serialized the whole phase.
    __atomic_fetch_or(&s_vismatrix[bitset >> 3], (byte)(1 << (bitset & 7)), __ATOMIC_RELAXED);
}

// Traces the waiting pairs of a patch as one packet from its origin.
static void FlushPendingPairs(const unsigned patchnum, const size_t bitpos, pendingpairs_t* pending)
{
    int results[TESTLINE_PACKET_MAX];
    vec_t fracs[TESTLINE_PACKET_MAX];
    const vec_t* origin = g_patches[patchnum].origin;

    if (pending->count == 0)
    {
        return;
    }
    if (pending->count == 1)
    {
        results[0] = TestLine(origin, pending->stops[0]);
    }
    else
    {
        TestLinePacket(origin, pending->stops, pending->count, results, fracs);
    }
    for (int k = 0; k < pending->count; k++)
    {
        if (results[k] == CONTENTS_EMPTY)
        {
            FinishVisiblePair(patchnum, pending->patches[k], bitpos, origin, pending->stops[k]);
        }
    }
    pending->count = 0;
}

// Decides whether patch m is visible from patch patchnum, for a pair that
// passed the tests of TestPatchToFace (m > patchnum, the face plane, the PVS).
// With pending set, a pair traced from the patch's own origin waits there for
// a packet; otherwise it is traced at once.
static void TestPatchPair(const unsigned patchnum, const unsigned m, const size_t bitpos, pendingpairs_t* pending)
{
    const patch_t* patch = &g_patches[patchnum];
    const patch_t* patch2 = &g_patches[m];
    const dplane_t* plane = getPlaneFromFaceNumber(patch->faceNumber);
    const dplane_t* plane2 = getPlaneFromFaceNumber(patch2->faceNumber);
    vec3_t origin1, origin2;
    vec3_t delta;
    vec_t dist;

    VectorSubtract(patch->origin, patch2->origin, delta);
    dist = VectorLength(delta);
    if (dist < patch2->emitter_range - ON_EPSILON)
    {
        GetAlternateOrigin(patch->origin, plane->normal, patch2, origin2);
    }
    else
    {
        VectorCopy(patch2->origin, origin2);
    }
    if (DotProduct(origin2, plane->normal) <= PatchPlaneDist(patch) + MINIMUM_PATCH_DISTANCE)
    {
        return;
    }
    if (dist < patch->emitter_range - ON_EPSILON)
    {
        GetAlternateOrigin(patch2->origin, plane2->normal, patch, origin1);
    }
    else
    {
        VectorCopy(patch->origin, origin1);
    }
    if (DotProduct(origin1, plane2->normal) <= PatchPlaneDist(patch2) + MINIMUM_PATCH_DISTANCE)
    {
        return;
    }
    // hlrad-ng: pairs traced from the patch's own origin wait for a
    // packet; a pair with an alternate start point is traced alone
    if (pending && origin1[0] == patch->origin[0] && origin1[1] == patch->origin[1] && origin1[2] == patch->origin[2])
    {
        VectorCopy(origin2, pending->stops[pending->count]);
        pending->patches[pending->count] = m;
        pending->count++;
        if (pending->count == TESTLINE_PACKET_MAX)
        {
            FlushPendingPairs(patchnum, bitpos, pending);
        }
        return;
    }
    if (TestLine(origin1, origin2) != CONTENTS_EMPTY)
    {
        return;
    }
    FinishVisiblePair(patchnum, m, bitpos, origin1, origin2);
}

// =====================================================================================
//  TestPatchToFace
//      Sets vis bits for all patches in the face
// =====================================================================================
static void TestPatchToFace(const unsigned patchnum, const int facenum, const int head, const size_t bitpos, byte* pvs,
                            pendingpairs_t* pending)
{
    patch_t* patch = &g_patches[patchnum];
    patch_t* patch2 = g_face_patches[facenum];

    // if emitter is behind that face plane, skip all patches

    if (patch2)
    {
        const dplane_t* plane2 = getPlaneFromFaceNumber(facenum);

        if (DotProduct(patch->origin, plane2->normal) > PatchPlaneDist(patch2) + ON_EPSILON - patch->emitter_range)
        {
            // we need to do a real test
            for (; patch2; patch2 = patch2->next)
            {
                unsigned m = patch2 - g_patches;

                // check vis between patch and patch2
                // if bit has not already been set
                //  && v2 is not behind light plane
                //  && v2 is visible from v1
                if (m > patchnum)
                {
                    if (patch2->leafnum == 0 || !(pvs[(patch2->leafnum - 1) >> 3] & (1 << ((patch2->leafnum - 1) & 7))))
                    {
                        continue;
                    }
                    TestPatchPair(patchnum, m, bitpos, pending);
                }
            }
        }
    }
}

#ifndef HLRAD_VISMATRIX_NOMARKSURFACES
// =====================================================================================
//  BuildVisRow
//      Calc vis bits from a single patch
// =====================================================================================
static void BuildVisRow(const int patchnum, byte* pvs, const int head, const size_t bitpos)
{
    int j, k, l;
    byte face_tested[MAX_MAP_FACES];
    dleaf_t* leaf;

    memset(face_tested, 0, g_numfaces);

    // leaf 0 is the solid leaf (skipped)
#ifdef HLRAD_VIS_FIX
    for (j = 1, leaf = g_dleafs + 1; j < 1 + g_dmodels[0].visleafs; j++, leaf++)
#else
    for (j = 1, leaf = g_dleafs + 1; j < g_numleafs; j++, leaf++)
#endif
    {
        if (!(pvs[(j - 1) >> 3] & (1 << ((j - 1) & 7))))
            continue; // not in pvs
        for (k = 0; k < leaf->nummarksurfaces; k++)
        {
            l = g_dmarksurfaces[leaf->firstmarksurface + k];

            // faces can be marksurfed by multiple leaves, but
            // don't bother testing again
            if (face_tested[l])
                continue;
            face_tested[l] = 1;

            TestPatchToFace(patchnum, l, head, bitpos
#ifdef HLRAD_ENTITYBOUNCE_FIX
                            ,
                            pvs
#endif
            );
        }
    }
}
#endif

// =====================================================================================
// BuildVisLeafs
//      This is run by multiple threads
// =====================================================================================
static void BuildVisLeafs(int threadnum)
{
    int i;
    int facenum, facenum2;
#ifndef HLRAD_VISMATRIX_NOMARKSURFACES
    int lface;
    dleaf_t* leaf;
#endif
    byte pvs[(MAX_MAP_LEAFS + 7) / 8];
    dleaf_t* srcleaf;
    patch_t* patch;
    int head;
    size_t bitpos;
    unsigned patchnum;

    while (1)
    {
        //
        // build a minimal BSP tree that only
        // covers areas relevent to the PVS
        //
        i = GetThreadWork();
        if (i == -1)
            break;
        i++; // skip leaf 0
        srcleaf = &g_dleafs[i];
#ifdef HLRAD_WITHOUTVIS
        if (!g_visdatasize)
        {
#ifdef ZHLT_DecompressVis_FIX
            memset(pvs, 255, (g_dmodels[0].visleafs + 7) / 8);
#else
            memset(pvs, 255, (g_numleafs + 7) / 8);
#endif
        }
        else
        {
#endif
#ifdef HLRAD_VIS_FIX
            if (srcleaf->visofs == -1)
            {
                Developer(DEVELOPER_LEVEL_ERROR, "Error: No visdata for leaf %d\n", i);
                continue;
            }
#endif
            DecompressVis(&g_dvisdata[srcleaf->visofs], pvs, sizeof(pvs));
#ifdef HLRAD_WITHOUTVIS
        }
#endif
        head = 0;

        //
        // go through all the faces inside the
        // leaf, and process the patches that
        // actually have origins inside
        //
#ifdef HLRAD_VISMATRIX_NOMARKSURFACES
        for (facenum = 0; facenum < g_numfaces; facenum++)
        {
            for (patch = g_face_patches[facenum]; patch; patch = patch->next)
            {
                if (patch->leafnum != i)
                    continue;
                patchnum = patch - g_patches;
#ifdef HALFBIT
                bitpos = (size_t)patchnum * g_num_patches - ((size_t)patchnum * (patchnum + 1)) / 2;
#else
                bitpos = (size_t)patchnum * g_num_patches;
#endif
                pendingpairs_t pending;
                pending.count = 0;
                for (facenum2 = facenum + 1; facenum2 < g_numfaces; facenum2++)
                    TestPatchToFace(patchnum, facenum2, head, bitpos, pvs, &pending);
                FlushPendingPairs(patchnum, bitpos, &pending);
            }
        }
#else
        for (lface = 0; lface < srcleaf->nummarksurfaces; lface++)
        {
            facenum = g_dmarksurfaces[srcleaf->firstmarksurface + lface];
            for (patch = g_face_patches[facenum]; patch; patch = patch->next)
            {
#ifdef HLRAD_ENTITYBOUNCE_FIX
                if (patch->leafnum != i)
                    continue;
#else
                leaf = PointInLeaf(patch->origin);
                if (leaf != srcleaf)
                    continue;
#endif

                patchnum = patch - g_patches;

#ifdef HALFBIT
                bitpos = (size_t)patchnum * g_num_patches - ((size_t)patchnum * (patchnum + 1)) / 2;
#else
                bitpos = (size_t)patchnum * g_num_patches;
#endif
                // build to all other world leafs
                BuildVisRow(patchnum, pvs, head, bitpos);

                // build to bmodel faces
                if (g_nummodels < 2)
                    continue;
                for (facenum2 = g_dmodels[1].firstface; facenum2 < g_numfaces; facenum2++)
                    TestPatchToFace(patchnum, facenum2, head, bitpos
#ifdef HLRAD_ENTITYBOUNCE_FIX
                                    ,
                                    pvs
#endif
                    );
            }
        }
#ifdef HLRAD_ENTITYBOUNCE_FIX
        if (g_nummodels >= 2)
        {
            for (facenum = g_dmodels[1].firstface; facenum < g_numfaces; facenum++)
            {
                for (patch = g_face_patches[facenum]; patch; patch = patch->next)
                {
                    if (patch->leafnum != i)
                        continue;
                    patchnum = patch - g_patches;
#ifdef HALFBIT
                    bitpos = (size_t)patchnum * g_num_patches - ((size_t)patchnum * (patchnum + 1)) / 2;
#else
                    bitpos = (size_t)patchnum * g_num_patches;
#endif
                    // skip BuildVisRow here because entity patchnums are always bigger than world patchnums.
                    for (facenum2 = g_dmodels[1].firstface; facenum2 < g_numfaces; facenum2++)
                        TestPatchToFace(patchnum, facenum2, head, bitpos, pvs);
                }
            }
        }
#endif
#endif
    }
}


// =====================================================================================
// hlrad-ng: patch visibility and radiosity on the GPU (gpu.h, gpuvis.comp, gpurad.comp)
// =====================================================================================
// The visibility shader decides every pair of patches that are far enough
// apart to be tested origin to origin; the CPU tests the pairs the shader
// marked as close (TestPatchPair). There are two ways to go on from there:
//
//   - The rows come back to the CPU, which builds the matrix, the transfer
//     lists and the bounces as before (BuildVisMatrixGpu).
//   - The rows stay on the device as a full matrix, and gpurad.comp computes
//     the transfers and runs the bounces there. The close pairs stay on the
//     CPU with their transfers (GpuRadiosityBuild). The device holds the
//     transfers in the 16-bit format of compress.h.

// const uint32_t g_gpuvis_rq_spv[], g_gpuvis_bsp_spv[]: SPIR-V of gpuvis.comp
#include "gpuvis_rq_spv.inc"
#include "gpuvis_bsp_spv.inc"
// const uint32_t g_gpurad_spv[]: SPIR-V of gpurad.comp
#include "gpurad_spv.inc"

// What the shaders read (std430 layout).
typedef struct
{
    float origin[3];
    float range;
    float normal[3];
    float planedist;
    int face;
    int leaf;
    int contents;
    int pad;
} gpupatch_t;

typedef struct
{
    unsigned numpatches;
    unsigned firstsky;
    unsigned firstrow;
    unsigned numrows;
    unsigned firstword;
    unsigned wordsperrow;
    unsigned leafwords;
    unsigned matrixwords;
    float onepsilon;
    float minpatchdistance;
    float nearmargin;
} gpuvisparams_t;

typedef struct
{
    float origin[3];
    float area;
    float normal[3];
    float exposure;
    float power;
    float scale;
    float pad[2];
} gpuradpatch_t;

typedef struct
{
    unsigned mode;
    unsigned numpatches;
    unsigned firstrow;
    unsigned numrows;
    unsigned rowwords;
    unsigned numslots;
    float normalepsilon;
    float patchhuntoffset;
    float minpatchdistance;
} gpuradparams_t;

static_assert(sizeof(gpupatch_t) == 48 && sizeof(gpuvisparams_t) == 44 && sizeof(gpuradpatch_t) == 48 &&
                  sizeof(gpuradparams_t) == 36,
              "records shared with gpuvis.comp and gpurad.comp");

#define GPU_VIS_GROUP_SIZE 64 // local_size_x of gpuvis.comp and gpurad.comp
#define GPU_RAD_COUNT 0       // modes of gpurad.comp
#define GPU_RAD_FILL 1
#define GPU_RAD_BOUNCE 2
#define GPU_RAD_MAX_SLOTS 8 // MAX_SLOTS of gpurad.comp
// Starting the GPU costs a fraction of a second; below this many patches the CPU is done sooner.
#define GPU_VIS_MIN_PATCHES 4096
// A dispatch has to finish well inside the display driver's watchdog time.
#define GPU_VIS_DISPATCH_SECONDS 0.2
// Most words one dispatch computes and reads back.
#define GPU_VIS_MAX_WORDS (16u * 1024 * 1024)
// A pair closer than the emitter range of one of its patches needs the patch
// shapes, which the CPU has. The shader hands over pairs up to this much
// farther apart, so that every pair MakeScales treats as close is among them.
#define GPU_VIS_NEAR_MARGIN 1.0f

// The rows the worker threads are taking over from the GPU.
static const unsigned* g_gpuvis_visible = NULL; // NULL when the rows stay on the device
static const unsigned* g_gpuvis_near = NULL;
static unsigned g_gpuvis_firstrow = 0;
static unsigned g_gpuvis_firstword = 0;
static unsigned g_gpuvis_wordsperrow = 0;

// Radiosity on the device.
static gpujob_t* g_gpuvis_job = NULL;
static unsigned g_gpuvis_firstsky = 0;
static unsigned g_gpuvis_leafwords = 0;
static gpujob_t* g_gpurad_job = NULL;              // set while the transfers are on the device
static std::vector<unsigned> g_gpurad_rows;        // per patch, where its factors start; one more entry closes the last
static std::vector<unsigned> g_gpurad_nearfirst;   // per patch, where its close emitters start; one more entry
static std::vector<unsigned> g_gpurad_nearindex;   // the close emitters, ascending per patch
static std::vector<float> g_gpurad_nearvalue;      // and their transfers
static double g_gpurad_budget = 8.0 * 1024 * 1024; // factors per dispatch, tuned to the device as it runs

// Takes over one row from the GPU: copies its bits into the matrix when the
// rows come back, and tests its close pairs.
static void GpuVisRow(const int index)
{
    const unsigned patchnum = g_gpuvis_firstrow + (unsigned)index;
    const size_t bitpos = (size_t)patchnum * g_num_patches - ((size_t)patchnum * (patchnum + 1)) / 2;
    const unsigned* near = g_gpuvis_near + (size_t)index * g_gpuvis_wordsperrow;

    for (unsigned word = 0; word < g_gpuvis_wordsperrow; word++)
    {
        const unsigned first = (g_gpuvis_firstword + word) * 32;
        const unsigned seen = g_gpuvis_visible ? g_gpuvis_visible[(size_t)index * g_gpuvis_wordsperrow + word] : 0;
        if (seen)
        {
            // 32 bits at a bit position: up to five bytes, which other rows may share
            const size_t bit = bitpos + first;
            const unsigned long long shifted = (unsigned long long)seen << (bit & 7);
            byte* dest = s_vismatrix + (bit >> 3);
            for (int k = 0; k < 5; k++)
            {
                const byte value = (byte)(shifted >> (8 * k));
                if (value)
                {
                    __atomic_fetch_or(&dest[k], value, __ATOMIC_RELAXED);
                }
            }
        }
        unsigned close = near[word];
        while (close)
        {
            const unsigned bit = (unsigned)__builtin_ctz(close);
            close &= close - 1;
            TestPatchPair(patchnum, first + bit, bitpos, NULL);
        }
    }
}

// Builds the pipeline of the visibility shader and gives it the patches and
// the leaf visibility, once. Returns NULL when the GPU is not used.
static gpujob_t* GpuVisJob()
{
    const unsigned numpatches = g_num_patches;
    const int numleafs = 1 + g_dmodels[0].visleafs;
    const unsigned leafwords = (unsigned)(numleafs + 31) / 32;

    if (g_gpuvis_job)
    {
        return g_gpuvis_job;
    }
    {
        const char* mode = getenv("HLRAD_NG_GPU");
        if (numpatches < GPU_VIS_MIN_PATCHES && !(mode && mode[0] == '1'))
        {
            return NULL;
        }
    }
    if (numpatches < 2 || !TraceGpuScene(&g_gpuvis_firstsky))
    {
        return NULL;
    }

    std::vector<gpupatch_t> patches((size_t)numpatches);
    for (unsigned i = 0; i < numpatches; i++)
    {
        const patch_t* patch = &g_patches[i];
        gpupatch_t* out = &patches[i];
        VectorCopy(patch->origin, out->origin);
        out->range = patch->emitter_range;
        VectorCopy(getPlaneFromFaceNumber(patch->faceNumber)->normal, out->normal);
        out->planedist = PatchPlaneDist(patch);
        out->face = patch->faceNumber;
        out->leaf = (patch->leafnum > 0 && patch->leafnum < numleafs) ? patch->leafnum : 0;
        out->contents = TracePointContents(patch->origin);
        out->pad = 0;
    }
    // which leaves each leaf sees, as BuildVisLeafs decides it; bit k of a row is leaf k
    std::vector<unsigned> leafvis((size_t)numleafs * leafwords, 0u);
    for (int leaf = 1; leaf < numleafs; leaf++)
    {
        byte pvs[(MAX_MAP_LEAFS + 7) / 8];
        if (!g_visdatasize)
        {
            memset(pvs, 255, sizeof(pvs));
        }
        else if (g_dleafs[leaf].visofs == -1)
        {
            continue; // no vis data: its patches see nothing
        }
        else
        {
            DecompressVis(&g_dvisdata[g_dleafs[leaf].visofs], pvs, sizeof(pvs));
        }
        for (int other = 1; other < numleafs; other++)
        {
            if (pvs[(other - 1) >> 3] & (1 << ((other - 1) & 7)))
            {
                leafvis[(size_t)leaf * leafwords + (size_t)(other >> 5)] |= 1u << (other & 31);
            }
        }
    }

    gpujob_t* job = GpuCreateJob(g_gpuvis_rq_spv, sizeof(g_gpuvis_rq_spv), g_gpuvis_bsp_spv, sizeof(g_gpuvis_bsp_spv),
                                 5, sizeof(gpuvisparams_t));
    if (!job || !GpuJobUpload(job, 1, &patches[0], patches.size() * sizeof(gpupatch_t)) ||
        !GpuJobUpload(job, 2, &leafvis[0], leafvis.size() * sizeof(unsigned)))
    {
        return NULL;
    }
    g_gpuvis_leafwords = leafwords;
    g_gpuvis_job = job;
    return job;
}

// Runs the visibility shader over every patch. With matrixwords set, the rows
// stay on the device (binding 5 of the job) and only the close pairs come back.
static bool GpuVisRun(gpujob_t* job, const unsigned matrixwords)
{
    const unsigned numpatches = g_num_patches;
    const unsigned words = (numpatches + 31) / 32;
    gpuvisparams_t params;
    std::vector<unsigned> visible;
    std::vector<unsigned> near;
    double perdispatch = 1024.0 * 1024.0; // words per dispatch, tuned to the device as it runs
    unsigned row = 0;

    memset(&params, 0, sizeof(params));
    params.numpatches = numpatches;
    params.firstsky = g_gpuvis_firstsky;
    params.leafwords = g_gpuvis_leafwords;
    params.matrixwords = matrixwords;
    params.onepsilon = (float)ON_EPSILON;
    params.minpatchdistance = (float)MINIMUM_PATCH_DISTANCE;
    params.nearmargin = GPU_VIS_NEAR_MARGIN;

    g_threadquiet = true;
    while (row < numpatches)
    {
        // rows from this one on have nothing below this word
        const unsigned firstword = row / 32;
        const unsigned wordsperrow = words - firstword;
        const double budget =
            qmin(perdispatch, qmin((double)GPU_VIS_MAX_WORDS, (double)GpuMaxGroups() * GPU_VIS_GROUP_SIZE));
        const unsigned numrows = qmax(1u, qmin(numpatches - row, (unsigned)(budget / wordsperrow)));
        const size_t count = (size_t)numrows * wordsperrow;
        gpuoutput_t outputs[2];
        int numoutputs = 0;

        near.resize(count);
        params.firstrow = row;
        params.numrows = numrows;
        params.firstword = firstword;
        params.wordsperrow = wordsperrow;
        outputs[numoutputs].binding = 4;
        outputs[numoutputs].data = &near[0];
        outputs[numoutputs].size = count * sizeof(unsigned);
        outputs[numoutputs].clear = 0;
        numoutputs++;
        if (!matrixwords)
        {
            visible.resize(count);
            outputs[numoutputs].binding = 3;
            outputs[numoutputs].data = &visible[0];
            outputs[numoutputs].size = count * sizeof(unsigned);
            outputs[numoutputs].clear = 0;
            numoutputs++;
        }

        const double start = I_FloatTime();
        if (!GpuJobRun(job, &params, (unsigned)((count + GPU_VIS_GROUP_SIZE - 1) / GPU_VIS_GROUP_SIZE), NULL, 0,
                       outputs, numoutputs))
        {
            g_threadquiet = false;
            return false;
        }
        const double elapsed = I_FloatTime() - start;

        g_gpuvis_visible = matrixwords ? NULL : &visible[0];
        g_gpuvis_near = &near[0];
        g_gpuvis_firstrow = row;
        g_gpuvis_firstword = firstword;
        g_gpuvis_wordsperrow = wordsperrow;
        RunThreadsOnIndividual((int)numrows, false, GpuVisRow);

        row += numrows;
        // steer the next dispatch toward the time budget, a factor of four at most per step
        perdispatch = qmax(4096.0, count * qmax(0.25, qmin(4.0, GPU_VIS_DISPATCH_SECONDS / qmax(elapsed, 1e-4))));
        {
            const double left = (double)(numpatches - row) / numpatches;
            PhaseProgress(1.0 - left * left);
        }
    }
    g_threadquiet = false;
    return true;
}

// Fills the matrix with the GPU's help. Returns false when the GPU is not
// used or stops; the CPU then builds the matrix (bits set so far stay valid).
static bool BuildVisMatrixGpu()
{
    gpujob_t* job = GpuVisJob();

    // this path reads the rows back; the device needs no matrix of its own
    if (!job || !GpuJobReserve(job, 5, sizeof(unsigned), false))
    {
        return false;
    }
    PhaseBegin("Patch visibility (GPU)");
    if (!GpuVisRun(job, 0))
    {
        Log("\n");
        Warning("The GPU stopped working; patch visibility continues on the CPU.");
        return false;
    }
    PhaseEnd();
    return true;
}

// The close pairs whose transfers the worker threads are computing.
static const unsigned* g_gpurad_work = NULL;
static float* g_gpurad_workvalues = NULL;
#define GPU_RAD_PAIRS_PER_ITEM 256

static void GpuRadiosityNearPairs(const int item)
{
    const size_t first = (size_t)item * GPU_RAD_PAIRS_PER_ITEM;
    for (size_t k = first; k < first + GPU_RAD_PAIRS_PER_ITEM; k++)
    {
        const unsigned a = g_gpurad_work[2 * k];
        const unsigned b = g_gpurad_work[2 * k + 1];
        if (a == b)
        {
            continue; // padding
        }
        g_gpurad_workvalues[2 * k] = MakeScalesPair(a, b);
        g_gpurad_workvalues[2 * k + 1] = MakeScalesPair(b, a);
    }
}

// Runs one mode of gpurad.comp over all patches, in dispatches that hold
// about g_gpurad_budget factors each. sums, when set, receives numslots * 4
// floats per patch.
static bool GpuRadiosityRun(gpuradparams_t* params, float* sums)
{
    const unsigned numpatches = g_num_patches;
    unsigned row = 0;

    while (row < numpatches)
    {
        unsigned last = row + 1;
        gpuoutput_t output;

        while (last < numpatches && last - row < GpuMaxGroups() * GPU_VIS_GROUP_SIZE &&
               (double)(g_gpurad_rows[last + 1] - g_gpurad_rows[row]) <= g_gpurad_budget)
        {
            last++;
        }
        params->firstrow = row;
        params->numrows = last - row;
        output.binding = 6;
        output.data = sums ? sums + (size_t)row * params->numslots * 4 : NULL;
        output.size = sums ? (size_t)(last - row) * params->numslots * 4 * sizeof(float) : 0;
        output.clear = 0;

        const double start = I_FloatTime();
        if (!GpuJobRun(g_gpurad_job, params, (last - row + GPU_VIS_GROUP_SIZE - 1) / GPU_VIS_GROUP_SIZE, NULL, 0,
                       &output, sums ? 1 : 0))
        {
            return false;
        }
        const double elapsed = I_FloatTime() - start;
        const double done = (double)(g_gpurad_rows[last] - g_gpurad_rows[row]);
        g_gpurad_budget = qmax(65536.0, done * qmax(0.25, qmin(4.0, GPU_VIS_DISPATCH_SECONDS / qmax(elapsed, 1e-4))));
        row = last;
    }
    return true;
}

static void GpuRadiosityFreeLists()
{
    g_gpurad_job = NULL;
    std::vector<unsigned>().swap(g_gpurad_rows);
    std::vector<unsigned>().swap(g_gpurad_nearfirst);
    std::vector<unsigned>().swap(g_gpurad_nearindex);
    std::vector<float>().swap(g_gpurad_nearvalue);
}

// Patch visibility and transfers on the device, for the bounces to run there
// too. Returns false when this map or this device takes the CPU path instead;
// nothing is left behind then.
static bool GpuRadiosityBuild()
{
    const unsigned numpatches = g_num_patches;
    const unsigned rowwords = (numpatches + 31) / 32;
    const unsigned long long matrixbytes = (unsigned long long)numpatches * rowwords * sizeof(unsigned);
    gpuradparams_t params;
    std::vector<unsigned> pairs;
    unsigned long long total = 0;
    unsigned i;

    // HLRAD_NG_GPU_RADIOSITY=0 keeps the transfers and the bounces on the CPU, for comparing the two
    {
        const char* mode = getenv("HLRAD_NG_GPU_RADIOSITY");
        if (mode && mode[0] == '0')
        {
            return false;
        }
    }
    if (g_incremental || !MakeScalesBlocksUsable(false))
    {
        return false; // the transfer file, and the cases MakeScalesBlocks leaves to MakeScales, need the lists
    }
    gpujob_t* visjob = GpuVisJob();
    if (!visjob || matrixbytes > GpuMaxBufferBytes() || matrixbytes > GpuDeviceMemory() / 3 ||
        !GpuJobReserve(visjob, 5, (size_t)matrixbytes, true) || !GpuJobReserve(visjob, 3, sizeof(unsigned), false))
    {
        return false;
    }

    PhaseBegin("Patch visibility (GPU)");
    g_gpurad_pairs = &pairs;
    const bool seen = GpuVisRun(visjob, rowwords);
    g_gpurad_pairs = NULL;
    if (!seen)
    {
        Log("\n");
        Warning("The GPU stopped working; patch visibility starts over on the CPU.");
        return false;
    }
    PhaseEnd();

    PhaseBegin("Transfers (GPU)");
    gpujob_t* job =
        GpuCreateJob(g_gpurad_spv, sizeof(g_gpurad_spv), g_gpurad_spv, sizeof(g_gpurad_spv), 6, sizeof(gpuradparams_t));
    std::vector<gpuradpatch_t> patches((size_t)numpatches);
    for (i = 0; i < numpatches; i++)
    {
        const patch_t* patch = &g_patches[i];
        const int miptex = g_texinfo[g_dfaces[patch->faceNumber].texinfo].miptex;
        gpuradpatch_t* out = &patches[i];
        VectorCopy(patch->origin, out->origin);
        out->area = patch->area;
        VectorCopy(getPlaneFromFaceNumber(patch->faceNumber)->normal, out->normal);
        out->exposure = patch->exposure;
        out->power = (float)g_lightingconeinfo[miptex][0];
        out->scale = (float)g_lightingconeinfo[miptex][1];
        out->pad[0] = 0;
        out->pad[1] = 0;
    }
    memset(&params, 0, sizeof(params));
    params.numpatches = numpatches;
    params.rowwords = rowwords;
    params.numslots = 1;
    params.normalepsilon = (float)NORMAL_EPSILON;
    params.patchhuntoffset = (float)PATCH_HUNT_OFFSET;
    params.minpatchdistance = (float)MINIMUM_PATCH_DISTANCE;

    // how many patches every patch sees, to lay out the rows of factors
    std::vector<unsigned> counts((size_t)numpatches);
    {
        gpuoutput_t output;
        output.binding = 3;
        output.data = &counts[0];
        output.size = counts.size() * sizeof(unsigned);
        output.clear = 0;
        params.mode = GPU_RAD_COUNT;
        params.firstrow = 0;
        params.numrows = numpatches;
        if (!job || !GpuJobShare(job, 2, visjob, 5) ||
            !GpuJobUpload(job, 1, &patches[0], patches.size() * sizeof(gpuradpatch_t)) ||
            !GpuJobReserve(job, 4, sizeof(unsigned), false) || !GpuJobReserve(job, 5, 4 * sizeof(float), false) ||
            !GpuJobReserve(job, 6, 4 * sizeof(float), false) ||
            !GpuJobRun(job, &params, (numpatches + GPU_VIS_GROUP_SIZE - 1) / GPU_VIS_GROUP_SIZE, NULL, 0, &output, 1))
        {
            Log("\n");
            return false;
        }
    }
    g_gpurad_rows.resize((size_t)numpatches + 1);
    for (i = 0; i < numpatches; i++)
    {
        g_gpurad_rows[i] = (unsigned)total;
        total += (counts[i] + 1ull) & ~1ull; // rows start at a whole word of two factors
    }
    // two bytes per factor, in one buffer, next to the matrix
    if (total >= 0xFFFFFFF0ull || total * 2 > GpuMaxBufferBytes() ||
        matrixbytes + total * 2 > GpuDeviceMemory() / 10 * 8)
    {
        Log("\n");
        GpuRadiosityFreeLists();
        return false;
    }
    g_gpurad_rows[numpatches] = (unsigned)total;
    g_gpurad_job = job;
    params.mode = GPU_RAD_FILL;
    if (!GpuJobUpload(job, 3, &g_gpurad_rows[0], (size_t)numpatches * sizeof(unsigned)) ||
        !GpuJobReserve(job, 4, (size_t)qmax(total * 2, 4ull), false) || !GpuRadiosityRun(&params, NULL))
    {
        Log("\n");
        GpuRadiosityFreeLists();
        return false;
    }
    PhaseProgress(0.5);

    // the close pairs: both transfers of each, with the patch shapes, on all threads
    {
        const size_t numpairs = pairs.size() / 2;
        const size_t items = (numpairs + GPU_RAD_PAIRS_PER_ITEM - 1) / GPU_RAD_PAIRS_PER_ITEM;
        std::vector<std::pair<unsigned, unsigned>> sorted(numpairs);
        size_t k;

        // in patch order, so that the result does not depend on which thread found a pair
        for (k = 0; k < numpairs; k++)
        {
            sorted[k] = std::make_pair(pairs[2 * k], pairs[2 * k + 1]);
        }
        std::sort(sorted.begin(), sorted.end());
        pairs.assign(items * GPU_RAD_PAIRS_PER_ITEM * 2, 0u);
        for (k = 0; k < numpairs; k++)
        {
            pairs[2 * k] = sorted[k].first;
            pairs[2 * k + 1] = sorted[k].second;
        }
        std::vector<float> values(pairs.size(), 0.0f);
        MakeScalesBlocksBegin();
        g_gpurad_work = pairs.empty() ? NULL : &pairs[0];
        g_gpurad_workvalues = values.empty() ? NULL : &values[0];
        g_threadquiet = true;
        RunThreadsOnIndividual((int)items, false, GpuRadiosityNearPairs);
        g_threadquiet = false;
        MakeScalesBlocksEnd();

        // per receiver, its close emitters in ascending order
        g_gpurad_nearfirst.assign((size_t)numpatches + 1, 0u);
        for (k = 0; k < numpairs; k++)
        {
            if (values[2 * k] > 0)
            {
                g_gpurad_nearfirst[(size_t)pairs[2 * k] + 1]++;
            }
            if (values[2 * k + 1] > 0)
            {
                g_gpurad_nearfirst[(size_t)pairs[2 * k + 1] + 1]++;
            }
        }
        for (i = 0; i < numpatches; i++)
        {
            g_gpurad_nearfirst[(size_t)i + 1] += g_gpurad_nearfirst[i];
        }
        g_gpurad_nearindex.resize((size_t)g_gpurad_nearfirst[numpatches] + 1);
        g_gpurad_nearvalue.resize((size_t)g_gpurad_nearfirst[numpatches] + 1);
        std::vector<unsigned> cursor(g_gpurad_nearfirst.begin(), g_gpurad_nearfirst.end() - 1);
        // a receiver's lower emitters come from pairs where it is the higher patch, and those
        // are sorted by the lower patch; its higher emitters follow in the second pass
        for (int pass = 0; pass < 2; pass++)
        {
            for (k = 0; k < numpairs; k++)
            {
                const unsigned receiver = pass == 0 ? pairs[2 * k + 1] : pairs[2 * k];
                const unsigned emitter = pass == 0 ? pairs[2 * k] : pairs[2 * k + 1];
                const float value = pass == 0 ? values[2 * k + 1] : values[2 * k];
                if (value > 0)
                {
                    g_gpurad_nearindex[cursor[receiver]] = emitter;
                    g_gpurad_nearvalue[cursor[receiver]] = value;
                    cursor[receiver]++;
                }
            }
        }
    }
    PhaseEnd();
    {
        unsigned long long transfers = g_gpurad_nearfirst[numpatches];
        for (i = 0; i < numpatches; i++)
        {
            transfers += counts[i];
        }
        Log("Transfers on the GPU: %.2fM, %.1f megs with the visibility matrix\n", transfers / 1e6,
            (double)(matrixbytes + total * 2) / (1024 * 1024));
    }
    return true;
}

bool GpuRadiosityActive()
{
    return g_gpurad_job != NULL;
}

bool GpuRadiosityBounce(const float* emit, const int slots, float* sums)
{
    gpuradparams_t params;

    if (!g_gpurad_job || slots < 1 || slots > GPU_RAD_MAX_SLOTS)
    {
        return false;
    }
    memset(&params, 0, sizeof(params));
    params.mode = GPU_RAD_BOUNCE;
    params.numpatches = g_num_patches;
    params.rowwords = (g_num_patches + 31) / 32;
    params.numslots = (unsigned)slots;
    params.normalepsilon = (float)NORMAL_EPSILON;
    params.patchhuntoffset = (float)PATCH_HUNT_OFFSET;
    params.minpatchdistance = (float)MINIMUM_PATCH_DISTANCE;
    return GpuJobUpload(g_gpurad_job, 5, emit, (size_t)g_num_patches * slots * 4 * sizeof(float)) &&
           GpuRadiosityRun(&params, sums);
}

unsigned GpuRadiosityNear(const unsigned patch, const unsigned** index, const float** value)
{
    *index = &g_gpurad_nearindex[g_gpurad_nearfirst[patch]];
    *value = &g_gpurad_nearvalue[g_gpurad_nearfirst[patch]];
    return g_gpurad_nearfirst[(size_t)patch + 1] - g_gpurad_nearfirst[patch];
}

void GpuRadiosityFree()
{
    GpuRadiosityFreeLists();
}

// =====================================================================================
// BuildVisMatrix
// =====================================================================================
static void BuildVisMatrix()
{
    size_t c;

#ifdef HALFBIT
    c = ((size_t)(g_num_patches + 1) * (g_num_patches + 1)) / 16;
    c += 1; //--vluzacn
#else
    c = (size_t)g_num_patches * ((g_num_patches + 7) / 8);
#endif

    Log("%-20s: %5.1f megs\n", "visibility matrix", c / (1024 * 1024.0));

    // hlrad-ng: plain calloc, because AllocBlock takes a 32-bit size on Win64
    // and the matrix of a large map is bigger than that. A few bytes more,
    // so that it can be read eight bytes at a time (MakeScalesBlocks).
    s_vismatrix = (byte*)calloc(c + 8, 1);

    if (!s_vismatrix)
    {
        Error("The visibility matrix of %u patches needs %.1f GB of memory. Use larger -chop/-texchop values or "
              "-vismatrix sparse.",
              g_num_patches, c / (1024.0 * 1024.0 * 1024.0));
    }

    if (!BuildVisMatrixGpu())
    {
        PhaseBegin("Patch visibility");
        RunThreadsOn(g_dmodels[0].visleafs, g_estimate, BuildVisLeafs);
    }
}

const byte* VisMatrixBits()
{
    return s_vismatrix;
}

static void FreeVisMatrix()
{
    if (s_vismatrix)
    {
        free(s_vismatrix);
        s_vismatrix = NULL;
    }

#ifndef HLRAD_TRANSPARENCY_CPP
#ifdef HLRAD_HULLU
    if (s_transparency_list)
    {
        free(s_transparency_list);
        s_transparency_list = NULL;
    }
    s_transparency_count = s_max_transparency_count = 0;
#endif
#endif
}

// =====================================================================================
// CheckVisBit
// =====================================================================================
#ifdef HLRAD_TRANSPARENCY_CPP
static bool CheckVisBitVismatrix(unsigned p1, unsigned p2
#ifdef HLRAD_HULLU
                                 ,
                                 vec3_t& transparency_out, unsigned int& next_index
#endif
)
{
    size_t bitpos;

#ifdef HLRAD_HULLU
    const unsigned a = p1;
    const unsigned b = p2;

    VectorFill(transparency_out, 1.0);
#endif

    if (p1 > p2)
    {
#ifndef HLRAD_HULLU
        const unsigned a = p1;
        const unsigned b = p2;
#endif
        p1 = b;
        p2 = a;
    }

    if (p1 > g_num_patches)
    {
        Warning("in CheckVisBit(), p1 > num_patches");
    }
    if (p2 > g_num_patches)
    {
        Warning("in CheckVisBit(), p2 > num_patches");
    }

#ifdef HALFBIT
    bitpos = (size_t)p1 * g_num_patches - ((size_t)p1 * (p1 + 1)) / 2 + p2;
#else
    bitpos = (size_t)p1 * g_num_patches + p2;
#endif

    if (s_vismatrix[bitpos >> 3] & (1 << (bitpos & 7)))
    {
#ifdef HLRAD_HULLU
        if (g_customshadow_with_bouncelight)
        {
            GetTransparency(a, b, transparency_out, next_index);
        }
#endif
        return true;
    }

    return false;
}
#else /*HLRAD_TRANSPARENCY_CPP*/
static bool CheckVisBitVismatrix(unsigned p1, unsigned p2
#ifdef HLRAD_HULLU
                                 ,
                                 vec3_t& transparency_out
#endif
)
{
    size_t bitpos;

    if (p1 > p2)
    {
        const unsigned a = p1;
        const unsigned b = p2;
        p1 = b;
        p2 = a;
    }

    if (p1 > g_num_patches)
    {
        Warning("in CheckVisBit(), p1 > num_patches");
    }
    if (p2 > g_num_patches)
    {
        Warning("in CheckVisBit(), p2 > num_patches");
    }

#ifdef HALFBIT
    bitpos = (size_t)p1 * g_num_patches - ((size_t)p1 * (p1 + 1)) / 2 + p2;
#else
    bitpos = (size_t)p1 * g_num_patches + p2;
#endif

    if (s_vismatrix[bitpos >> 3] & (1 << (bitpos & 7)))
    {
#ifdef HLRAD_HULLU
        if (g_customshadow_with_bouncelight)
        {
            vec3_t getvalue = {1.0, 1.0, 1.0};
            FindOpacity(bitpos, getvalue);
            VectorCopy(getvalue, transparency_out);
        }
        else
        {
            VectorFill(transparency_out, 1.0);
        }
#endif
        return true;
    }

#ifdef HLRAD_HULLU
    VectorFill(transparency_out, 0.0);
#endif

    return false;
}
#endif /*HLRAD_TRANSPARENCY_CPP*/

//
// end old vismat.c
////////////////////////////

// =====================================================================================
// MakeScalesVismatrix
// =====================================================================================
void MakeScalesVismatrix()
{
    char transferfile[_MAX_PATH];

    hlassume(g_num_patches < MAX_VISMATRIX_PATCHES, assume_MAX_PATCHES);

#ifdef ZHLT_DEFAULTEXTENSION_FIX
    safe_snprintf(transferfile, _MAX_PATH, "%s.inc", g_Mapname);
#else
    safe_strncpy(transferfile, g_source, _MAX_PATH);
    StripExtension(transferfile);
    DefaultExtension(transferfile, ".inc", sizeof(transferfile));
#endif

    if (!g_incremental || !readtransfers(transferfile, g_num_patches))
    {
        // hlrad-ng: visibility, transfers and bounces on the GPU when that applies
        if (GpuRadiosityBuild())
        {
            _unlink(transferfile);
            return;
        }

        // determine visibility between g_patches
        BuildVisMatrix();
        g_CheckVisBit = CheckVisBitVismatrix;

#ifdef HLRAD_HULLU
#ifdef HLRAD_TRANSPARENCY_CPP
        CreateFinalTransparencyArrays("custom shadow array");
#else
        if ((s_max_transparency_count * sizeof(transparency_t)) >= (1024 * 1024))
            Log("%-20s: %5.1f megs\n", "custom shadow array",
                (s_max_transparency_count * sizeof(transparency_t)) / (1024 * 1024.0));
        else if (s_transparency_count)
            Log("%-20s: %5.1f kilos\n", "custom shadow array",
                (s_max_transparency_count * sizeof(transparency_t)) / 1024.0);
#endif
#endif

        PhaseBegin("Transfers");
        if (MakeScalesBlocksUsable(true))
        {
            const int blocks = MakeScalesBlocksBegin();
            RunThreadsOn(blocks, g_estimate, MakeScalesBlocks);
            MakeScalesBlocksEnd();
        }
        else if (g_rgb_transfers)
        {
            RunThreadsOn(g_num_patches, g_estimate, MakeRGBScales);
        }
        else
        {
            RunThreadsOn(g_num_patches, g_estimate, MakeScales);
        }
        FreeVisMatrix();
#ifdef HLRAD_HULLU
#ifdef HLRAD_TRANSPARENCY_CPP
        FreeTransparencyArrays();
#endif
#endif

#ifndef HLRAD_NOSWAP
        // invert the transfers for gather vs scatter
#ifndef HLRAD_HULLU
        PhaseBegin("Transfer swap");
        RunThreadsOnIndividual(g_num_patches, g_estimate, SwapTransfers);
#else
        if (g_rgb_transfers)
        {
            PhaseBegin("Transfer swap");
            RunThreadsOnIndividual(g_num_patches, g_estimate, SwapRGBTransfers);
        }
        else
        {
            PhaseBegin("Transfer swap");
            RunThreadsOnIndividual(g_num_patches, g_estimate, SwapTransfers);
        }
#endif
#endif /*HLRAD_NOSWAP*/
        if (g_incremental)
            writetransfers(transferfile, g_num_patches);
        else
            _unlink(transferfile);
        DumpTransfersMemoryUsage();
#ifdef HLRAD_OPAQUE_STYLE_BOUNCE
        CreateFinalStyleArrays("dynamic shadow array");
#endif
    }
}
