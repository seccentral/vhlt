#include "cmdlib.h"
#include "mathlib.h"
#include "bspfile.h"
#include "log.h" //--vluzacn
#ifdef HLRAD_OPAQUE_NODE
#include "winding.h"
#endif
#include "qrad.h"
#include "profile.h"
#include "gpu.h"

#include <algorithm>
#include <utility>
#include <vector>
#ifdef __x86_64__
#include <cpuid.h>
#endif

// #define      ON_EPSILON      0.001

typedef struct tnode_s
{
    planetypes type;
    vec3_t normal;
    float dist;
    int children[2];
    int pad;
} tnode_t;

static tnode_t* tnodes;
static tnode_t* tnode_p;

static void InitPacketTrace();

/*
 * ==============
 * MakeTnode
 *
 * Converts the disk node structure into the efficient tracing structure
 * ==============
 */
static void MakeTnode(const int nodenum)
{
    tnode_t* t;
    dplane_t* plane;
    int i;
    dnode_t* node;

    t = tnode_p++;

    node = g_dnodes + nodenum;
    plane = g_dplanes + node->planenum;

    t->type = plane->type;
    VectorCopy(plane->normal, t->normal);
#ifdef ZHLT_PLANETYPE_FIX //debug
    if (plane->normal[(plane->type) % 3] < 0)
    {
        if (plane->type < 3)
            Warning("MakeTnode: negative plane");
        else
            Developer(DEVELOPER_LEVEL_MESSAGE, "Warning: MakeTnode: negative plane\n");
    }
#endif
    t->dist = plane->dist;

    for (i = 0; i < 2; i++)
    {
        if (node->children[i] < 0)
            t->children[i] = g_dleafs[-node->children[i] - 1].contents;
        else
        {
            t->children[i] = tnode_p - tnodes;
            MakeTnode(node->children[i]);
        }
    }
}

/*
 * =============
 * MakeTnodes
 *
 * Loads the node structure out of a .bsp file to be used for light occlusion
 * =============
 */
#if 0 // turn on for debugging. --vluzacn
#include <windows.h>
#define PERR(bSuccess, api)                                                                                            \
    {                                                                                                                  \
        if (!(bSuccess))                                                                                               \
            printf("%s:Error %d from %s \
    on line %d\n",                                                                                                     \
                   __FILE__, GetLastError(), api, __LINE__);                                                           \
    }
 void cls( HANDLE hConsole )
 {
    COORD coordScreen = { 0, 0 };    /* here's where we'll home the
                                        cursor */
    BOOL bSuccess;
    DWORD cCharsWritten;
    CONSOLE_SCREEN_BUFFER_INFO csbi; /* to get buffer info */
    DWORD dwConSize;                 /* number of character cells in
                                        the current buffer */

    /* get the number of character cells in the current buffer */

    bSuccess = GetConsoleScreenBufferInfo( hConsole, &csbi );
    PERR( bSuccess, "GetConsoleScreenBufferInfo" );
    dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

    /* fill the entire screen with blanks */

    bSuccess = FillConsoleOutputCharacter( hConsole, (TCHAR) ' ',
       dwConSize, coordScreen, &cCharsWritten );
    PERR( bSuccess, "FillConsoleOutputCharacter" );

    /* get the current text attribute */

    bSuccess = GetConsoleScreenBufferInfo( hConsole, &csbi );
    PERR( bSuccess, "ConsoleScreenBufferInfo" );

    /* now set the buffer's attributes accordingly */

    bSuccess = FillConsoleOutputAttribute( hConsole, csbi.wAttributes,
       dwConSize, coordScreen, &cCharsWritten );
    PERR( bSuccess, "FillConsoleOutputAttribute" );

    /* put the cursor at (0, 0) */

    bSuccess = SetConsoleCursorPosition( hConsole, coordScreen );
    PERR( bSuccess, "SetConsoleCursorPosition" );
    return;
 }
#include <conio.h>
void ViewTNode ()
{
	int nodecount;
	int nodes[128];
	int parents[128];
	int selection;
	int linestart;
	nodes[0] = 0;
	parents[0] = -1;
	nodecount = 1;
	selection = 0;
	linestart = 0;
	HANDLE hConsole = CreateFile("CONOUT$", GENERIC_WRITE|GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
	while (1)
	{
		cls (hConsole);
		cprintf ("\nTNodes:\n");
		{
			int i;
			for (i=0; i<nodecount; i++)
				if (i>=linestart && i<linestart+30)
				{
					int j;
					if (i==selection)
						cprintf (">  ");
					else
						cprintf ("%3d", i);
					for (j=i; j=parents[j], j>=0;)
						cprintf ("|");
					if (nodes[i] == CONTENTS_SOLID)
						cprintf ("SOLID");
					else if (nodes[i] == CONTENTS_SKY)
						cprintf ("SKY");
					else if (nodes[i] < 0)
						cprintf ("EMPTY (%d)", nodes[i]);
					else
					{
						cprintf ("NODE %d", nodes[i]);
						tnode_t* tnode;
						tnode = &tnodes[nodes[i]];
						planetypes d[6] = {plane_x, plane_y, plane_z, plane_anyx, plane_anyy, plane_anyz};
						char s[6][16] = {"_X", "_Y", "_Z", "anyX", "anyY", "anyZ"};
						int k;
						for (k=0; k<6; k++)
							if (tnode->type == d[k])
							{
								cprintf (" %s", s[k]);
								break;
							}
						if (k == 6)
							cprintf (" type=%d", tnode->type);
						cprintf (" N,D=(%g,%g,%g),%g", tnode->normal[0], tnode->normal[1], tnode->normal[2], tnode->dist);
						cprintf (" C=(%d,%d)", tnode->children[0], tnode->children[1]);
						cprintf (" pad=%d", tnode->pad);
					}
					cprintf ("\n");
				}
			}
		int c;
		while (1)
		{
			switch (c=getch ())
			{
			case 'q':
				exit (0);
				break;
			case 224:
				break;
			case 72:
				cprintf ("KEY: UP\n");
				selection--;
				if (selection < 0)
					selection = 0;
				break;
			case 80:
				cprintf ("KEY: DOWN\n");
				selection++;
				if (selection > nodecount-1)
					selection = nodecount-1;
				break;
			case 73:
				cprintf ("KEY: PGUP\n");
				linestart -= 10;
				if (linestart < 0)
					linestart = 0;
				if (linestart >= nodecount)
					linestart = nodecount /10 *10;
				break;
			case 81:
				cprintf ("KEY: PGDOWN\n");
				linestart += 10;
				if (linestart < 0)
					linestart = 0;
				if (linestart >= nodecount)
					linestart = nodecount /10 *10;
				break;
			case 75:
				cprintf ("KEY: LEFT\n");
				{
					int i, j;
					for (i=0; i<nodecount; i++)
					{
						for (j=i; j=parents[j], j>=0;)
							if (j==selection)
								break;
						if (j>=0)
							break;
					}
					if (i>=nodecount)
					{
						if (parents[selection] >= 0)
							selection = parents[selection];
						break;
					}
				}
				{
					int map[128];
					int mcount = 0;
					int i;
					int j;
					for (i=0; i<nodecount; i++)
					{
						map[i] = mcount;
						for (j=i; j=parents[j], j>=0;)
							if (j==selection)
								map[i] = -1;
						if (map[i] >= 0)
							mcount++;
					}
					for (i=0; i<nodecount; i++)
					{
						if (map[i] >= 0)
						{
							nodes[map[i]] = nodes[i];
							if (parents[i] >= 0)
								parents[map[i]] = map[parents[i]];
						}
					}
					nodecount = mcount;
					selection = map[selection];
				}
				break;
			case 77:
				cprintf ("KEY: RIGHT\n");
				{
					if (nodes[selection] < 0)
						break;
					int j;
					for (j=0; j<nodecount; j++)
						if (parents[j] >= 0 && parents[j] == selection)
							break;
					if (j<nodecount)
						break;
					tnode_t* tnode;
					tnode = &tnodes[nodes[selection]];
					for (j=0; j<nodecount; j++)
						if (parents[j] >= 0 && parents[j] > selection)
							parents[j] += 2;
					for (j = nodecount-1; j > selection; j--)
					{
						nodes[j+2] = nodes[j];
						parents[j+2] = parents[j];
					}
					nodecount += 2;
					nodes[selection+1] = tnode->children[0];
					nodes[selection+2] = tnode->children[1];
					parents[selection+1] = selection;
					parents[selection+2] = selection;
				}
				break;
			default:
				cprintf ("KEY: U%d\n", c);
				continue;
			}
			break;
		}
	}
}
#endif
void MakeTnodes(dmodel_t* /*bm*/)
{
    // 32 byte align the structs
    tnodes = (tnode_t*)calloc((g_numnodes + 1), sizeof(tnode_t));

    // The alignment doesn't have any effect at all. --vluzacn
#ifdef ZHLT_64BIT_FIX
    int ofs = 31 - (int)(((uintptr_t)tnodes + (uintptr_t)31) & (uintptr_t)31);
    tnodes = (tnode_t*)((byte*)tnodes + ofs);
#else
    tnodes = (tnode_t*)(((int)tnodes + 31) & ~31);
#endif
    tnode_p = tnodes;

    MakeTnode(0);
    InitPacketTrace();
#if 0 //debug. vluzacn
	ViewTNode ();
#endif
}

//==========================================================

int TestLine_r(const int node, const vec3_t start, const vec3_t stop
#ifdef HLRAD_WATERBLOCKLIGHT
               ,
               int& linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
               ,
               vec_t* skyhit
#endif
)
{
    tnode_t* tnode;
    float front, back;
    vec3_t mid;
    float frac;
    int side;
    int r;

#ifdef HLRAD_WATERBLOCKLIGHT
    if (node < 0)
    {
        if (node == linecontent)
            return CONTENTS_EMPTY;
#ifdef HLRAD_OPAQUEINSKY_FIX
        if (node == CONTENTS_SOLID)
        {
            return CONTENTS_SOLID;
        }
        if (node == CONTENTS_SKY)
        {
            if (skyhit)
            {
                VectorCopy(start, skyhit);
            }
            return CONTENTS_SKY;
        }
#else
        if (node == CONTENTS_SOLID || node == CONTENTS_SKY)
            return node;
#endif
        if (linecontent)
        {
            return CONTENTS_SOLID;
        }
        linecontent = node;
        return CONTENTS_EMPTY;
    }
#else
#ifdef HLRAD_OPAQUEINSKY_FIX
    if (node == CONTENTS_SKY)
    {
        VectorCopy(start, skyhit);
    }
#endif
    if ((node == CONTENTS_SOLID) || (node == CONTENTS_SKY)
        /*|| (node == CONTENTS_NULL ) */
    )
        return node;

    if (node < 0)
        return CONTENTS_EMPTY;
#endif

    tnode = &tnodes[node];
    switch (tnode->type)
    {
    case plane_x:
        front = start[0] - tnode->dist;
        back = stop[0] - tnode->dist;
        break;
    case plane_y:
        front = start[1] - tnode->dist;
        back = stop[1] - tnode->dist;
        break;
    case plane_z:
        front = start[2] - tnode->dist;
        back = stop[2] - tnode->dist;
        break;
    default:
        front = (start[0] * tnode->normal[0] + start[1] * tnode->normal[1] + start[2] * tnode->normal[2]) - tnode->dist;
        back = (stop[0] * tnode->normal[0] + stop[1] * tnode->normal[1] + stop[2] * tnode->normal[2]) - tnode->dist;
        break;
    }

#ifdef HLRAD_TestLine_EDGE_FIX
    if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2)
    {
        return TestLine_r(tnode->children[0], start, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                          ,
                          linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                          ,
                          skyhit
#endif
        );
    }
    if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2)
    {
        return TestLine_r(tnode->children[1], start, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                          ,
                          linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                          ,
                          skyhit
#endif
        );
    }
    if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON)
    {
        int r1 = TestLine_r(tnode->children[0], start, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                            ,
                            linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                            ,
                            skyhit
#endif
        );
        if (r1 == CONTENTS_SOLID)
            return CONTENTS_SOLID;
        int r2 = TestLine_r(tnode->children[1], start, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                            ,
                            linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                            ,
                            skyhit
#endif
        );
        if (r2 == CONTENTS_SOLID)
            return CONTENTS_SOLID;
        if (r1 == CONTENTS_SKY || r2 == CONTENTS_SKY)
            return CONTENTS_SKY;
        return CONTENTS_EMPTY;
    }
    side = (front - back) < 0;
    frac = front / (front - back);
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    mid[0] = start[0] + (stop[0] - start[0]) * frac;
    mid[1] = start[1] + (stop[1] - start[1]) * frac;
    mid[2] = start[2] + (stop[2] - start[2]) * frac;
    r = TestLine_r(tnode->children[side], start, mid
#ifdef HLRAD_WATERBLOCKLIGHT
                   ,
                   linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                   ,
                   skyhit
#endif
    );
    if (r != CONTENTS_EMPTY)
        return r;
    return TestLine_r(tnode->children[!side], mid, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                      ,
                      linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                      ,
                      skyhit
#endif
    );
#else //bug: light can go through edges of solid brushes
    if (front >= -ON_EPSILON && back >= -ON_EPSILON)
        return TestLine_r(tnode->children[0], start, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                          ,
                          linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                          ,
                          skyhit
#endif
        );

    if (front < ON_EPSILON && back < ON_EPSILON)
        return TestLine_r(tnode->children[1], start, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                          ,
                          linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                          ,
                          skyhit
#endif
        );

    side = front < 0;

    frac = front / (front - back);

    mid[0] = start[0] + (stop[0] - start[0]) * frac;
    mid[1] = start[1] + (stop[1] - start[1]) * frac;
    mid[2] = start[2] + (stop[2] - start[2]) * frac;

    r = TestLine_r(tnode->children[side], start, mid
#ifdef HLRAD_WATERBLOCKLIGHT
                   ,
                   linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                   ,
                   skyhit
#endif
    );
    if (r != CONTENTS_EMPTY)
        return r;
    return TestLine_r(tnode->children[!side], mid, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                      ,
                      linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                      ,
                      skyhit
#endif
    );
#endif
}

int TestLine(const vec3_t start, const vec3_t stop
#ifdef HLRAD_OPAQUEINSKY_FIX
             ,
             vec_t* skyhit
#endif
)
{
#ifdef HLRAD_WATERBLOCKLIGHT
    int linecontent = 0;
#endif
    return TestLine_r(0, start, stop
#ifdef HLRAD_WATERBLOCKLIGHT
                      ,
                      linecontent
#endif
#ifdef HLRAD_OPAQUEINSKY_FIX
                      ,
                      skyhit
#endif
    );
}

// =====================================================================================
// hlrad-ng packet tracing
// =====================================================================================
// Rays that share a start point are traced several at a time through the
// world BSP with the kernel in tracepacket.h. The 4-wide build runs on every
// target; the 8-wide build is used when the CPU has AVX2. Both do the same
// IEEE operations per ray, so the result does not depend on which one runs.

// Explicit stack of the packet walk; one entry per level of the tree.
#define TRACE_STACK_DEPTH 256

#define TP_W 4
#define TP_NAME(x) Tp4##x
#define TP_ATTR
#include "tracepacket.h"
#undef TP_W
#undef TP_NAME
#undef TP_ATTR

#ifdef __x86_64__
#define TRACE_HAVE_WIDE 1
#define TP_W 8
#define TP_NAME(x) Tp8##x
#define TP_ATTR __attribute__((target("avx2")))
#include "tracepacket.h"
#undef TP_W
#undef TP_NAME
#undef TP_ATTR

__attribute__((target("xsave"))) static bool TraceCpuHasAVX2()
{
    unsigned int a = 0;
    unsigned int b = 0;
    unsigned int c = 0;
    unsigned int d = 0;
    const unsigned int osxsave_avx = (1u << 27) | (1u << 28);

    if (!__get_cpuid(1, &a, &b, &c, &d) || (c & osxsave_avx) != osxsave_avx)
    {
        return false;
    }
    // the OS must save the XMM and YMM state
    if ((__builtin_ia32_xgetbv(0) & 6) != 6)
    {
        return false;
    }
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d))
    {
        return false;
    }
    return (b & (1u << 5)) != 0;
}
#endif

static bool g_trace_packets = false;                  // the tree fits the packet kernel's stack
static bool g_trace_wide = false;                     // the 8-wide kernel is usable
static vec_t g_trace_skyrange = 0;                    // length of a sky ray
static int g_trace_skycount = 0;                      // sky normals
static const vec3_t* g_trace_skynormals = NULL;       // sky normals in their own order
static int g_trace_depth = 0;                         // depth of the world tree
static int g_trace_skyslots = 0;                      // g_trace_skycount rounded up to a full packet
static int* g_trace_skyindex = NULL;                  // slot -> sky normal index, -1 for padding
static float* g_trace_skydir[3] = {NULL, NULL, NULL}; // sky normals in slot order

// Depth of the subtree below a tnode: the number of nodes on its longest path.
static int TnodeDepth(const int node)
{
    if (node < 0)
    {
        return 0;
    }
    const int front = TnodeDepth(tnodes[node].children[0]);
    const int back = TnodeDepth(tnodes[node].children[1]);
    return 1 + qmax(front, back);
}

static void InitPacketTrace()
{
    const int numtnodes = (int)(tnode_p - tnodes);
    int i;

    // The packet kernel evaluates every plane with its normal; make the
    // normals of axial planes exact so that matches TestLine_r, which reads
    // the coordinate directly.
    for (i = 0; i < numtnodes; i++)
    {
        tnode_t* tnode = &tnodes[i];
        if (tnode->type == plane_x || tnode->type == plane_y || tnode->type == plane_z)
        {
            VectorClear(tnode->normal);
            tnode->normal[tnode->type] = 1;
        }
    }

    // Deeper trees than the kernel's stack fall back to the scalar walk.
    g_trace_depth = numtnodes > 0 ? TnodeDepth(0) : 0;
    g_trace_packets = numtnodes > 0 && g_trace_depth < TRACE_STACK_DEPTH && getenv("HLRAD_NG_NOPACKETS") == NULL;
    g_trace_wide = false;
#ifdef TRACE_HAVE_WIDE
    g_trace_wide = TraceCpuHasAVX2() && getenv("HLRAD_NG_NOAVX2") == NULL;
#endif
}

// Scalar stand-in for one packet lane: TestLine plus the fraction of the
// segment at which the sky was entered.
static void TraceScalar(const vec3_t start, const vec3_t stop, int* result, vec_t* frac)
{
    vec3_t skyhit;
    vec3_t whole;
    vec3_t part;

    VectorCopy(stop, skyhit);
    *result = TestLine(start, stop, skyhit);
    VectorSubtract(stop, start, whole);
    VectorSubtract(skyhit, start, part);
    const vec_t length = DotProduct(whole, whole);
    *frac = (*result == CONTENTS_SKY && length > 0) ? DotProduct(part, whole) / length : 0;
}

// Contents of the leaf that holds the point.
static int TraceStartContents(const vec3_t point)
{
    int node = 0;

    while (node >= 0)
    {
        const tnode_t* tnode = &tnodes[node];
        float front;
        switch (tnode->type)
        {
        case plane_x:
            front = point[0] - tnode->dist;
            break;
        case plane_y:
            front = point[1] - tnode->dist;
            break;
        case plane_z:
            front = point[2] - tnode->dist;
            break;
        default:
            front =
                (point[0] * tnode->normal[0] + point[1] * tnode->normal[1] + point[2] * tnode->normal[2]) - tnode->dist;
            break;
        }
        node = tnode->children[front < 0 ? 1 : 0];
    }
    return node;
}

// Traces one full packet (TESTLINE_PACKET_MAX lanes) with the widest kernel.
static void TraceDispatch(const vec3_t origin, const int startcontents, const float* dx, const float* dy,
                          const float* dz, const int* active, int* results, float* fracs)
{
#ifdef HLRAD_NG_PROFILE
    g_profile_tracepackets++;
#endif
    PROFILE_SCOPE(PROFILE_PACKETS);
    if (!g_trace_packets)
    {
        for (int lane = 0; lane < TESTLINE_PACKET_MAX; lane++)
        {
            if (active[lane])
            {
                const vec3_t stop = {origin[0] + dx[lane], origin[1] + dy[lane], origin[2] + dz[lane]};
                TraceScalar(origin, stop, &results[lane], &fracs[lane]);
            }
        }
        return;
    }
#ifdef TRACE_HAVE_WIDE
    if (g_trace_wide)
    {
        Tp8TracePacket(origin, startcontents, dx, dy, dz, active, results, fracs);
        return;
    }
#endif
    for (int half = 0; half < TESTLINE_PACKET_MAX; half += 4)
    {
        if (active[half] | active[half + 1] | active[half + 2] | active[half + 3])
        {
            Tp4TracePacket(origin, startcontents, dx + half, dy + half, dz + half, active + half, results + half,
                           fracs + half);
        }
    }
}

void TestLinePacket(const vec3_t start, const vec3_t* stops, const int count, int* results, vec_t* fracs)
{
    float dx[TESTLINE_PACKET_MAX];
    float dy[TESTLINE_PACKET_MAX];
    float dz[TESTLINE_PACKET_MAX];
    int active[TESTLINE_PACKET_MAX];
    int packetresults[TESTLINE_PACKET_MAX];
    float packetfracs[TESTLINE_PACKET_MAX];
    int i;

    hlassume(count >= 0 && count <= TESTLINE_PACKET_MAX, assume_first);

    const int startcontents = TraceStartContents(start);
    if (startcontents == CONTENTS_SOLID || startcontents == CONTENTS_SKY)
    {
        // TestLine_r answers from the first leaf in both cases
        for (i = 0; i < count; i++)
        {
            results[i] = startcontents;
            fracs[i] = 0;
        }
        return;
    }

    for (i = 0; i < TESTLINE_PACKET_MAX; i++)
    {
        if (i < count)
        {
            dx[i] = stops[i][0] - start[0];
            dy[i] = stops[i][1] - start[1];
            dz[i] = stops[i][2] - start[2];
            active[i] = -1;
        }
        else
        {
            dx[i] = 0;
            dy[i] = 0;
            dz[i] = 0;
            active[i] = 0;
        }
        packetresults[i] = CONTENTS_SOLID;
        packetfracs[i] = 0;
    }

    TraceDispatch(start, startcontents, dx, dy, dz, active, packetresults, packetfracs);

    for (i = 0; i < count; i++)
    {
        results[i] = packetresults[i];
        fracs[i] = packetfracs[i];
    }
}

vec_t TraceSkyRange()
{
    return g_trace_skyrange;
}

void TraceCoherentOrder(const vec3_t* normals, const int count, int* order)
{
    int i;
    int k;

    // Sort along a Morton curve over the octahedral map of the sphere, so
    // that neighbours in the order point the same way and the rays of one
    // packet share most of their walk through the tree.
    std::vector<std::pair<unsigned int, int>> keyed((size_t)count);
    for (i = 0; i < count; i++)
    {
        const vec_t* n = normals[i];
        const float sum = (float)(fabs(n[0]) + fabs(n[1]) + fabs(n[2]));
        float u = sum > 0 ? n[0] / sum : 0;
        float v = sum > 0 ? n[1] / sum : 0;
        if (n[2] < 0)
        {
            const float fu = (float)((1 - fabs(v)) * (u < 0 ? -1 : 1));
            const float fv = (float)((1 - fabs(u)) * (v < 0 ? -1 : 1));
            u = fu;
            v = fv;
        }
        const unsigned int qu = (unsigned int)qmax(0, qmin(1023, (int)((u * 0.5f + 0.5f) * 1023)));
        const unsigned int qv = (unsigned int)qmax(0, qmin(1023, (int)((v * 0.5f + 0.5f) * 1023)));
        unsigned int key = 0;
        for (k = 0; k < 10; k++)
        {
            key |= ((qu >> k) & 1u) << (2 * k);
            key |= ((qv >> k) & 1u) << (2 * k + 1);
        }
        keyed[(size_t)i] = std::make_pair(key, i);
    }
    std::sort(keyed.begin(), keyed.end());
    for (i = 0; i < count; i++)
    {
        order[i] = keyed[(size_t)i].second;
    }
}

void BuildSkyTrace(const vec3_t* normals, const int count)
{
    int i;
    int k;

    // A sky ray only has to leave the world: every sky face lies inside the
    // world bounds, so a ray this long from any point inside them reaches the
    // sky if it is going to. Short rays keep float rounding small.
    {
        vec3_t size;
        VectorSubtract(g_dmodels[0].maxs, g_dmodels[0].mins, size);
        g_trace_skyrange = (vec_t)qmin(qmax(VectorLength(size) + 128, 1024), BOGUS_RANGE);
    }

    std::vector<int> order((size_t)qmax(count, 1));
    TraceCoherentOrder(normals, count, &order[0]);

    g_trace_skycount = count;
    g_trace_skynormals = normals;
    g_trace_skyslots = (count + TESTLINE_PACKET_MAX - 1) / TESTLINE_PACKET_MAX * TESTLINE_PACKET_MAX;
    free(g_trace_skyindex);
    g_trace_skyindex = (int*)malloc((size_t)qmax(g_trace_skyslots, 1) * sizeof(int));
    hlassume(g_trace_skyindex != NULL, assume_NoMemory);
    for (k = 0; k < 3; k++)
    {
        free(g_trace_skydir[k]);
        g_trace_skydir[k] = (float*)malloc((size_t)qmax(g_trace_skyslots, 1) * sizeof(float));
        hlassume(g_trace_skydir[k] != NULL, assume_NoMemory);
    }
    for (i = 0; i < g_trace_skyslots; i++)
    {
        const int index = i < count ? order[(size_t)i] : -1;
        g_trace_skyindex[i] = index;
        for (k = 0; k < 3; k++)
        {
            g_trace_skydir[k][i] = index >= 0 ? normals[index][k] : 0;
        }
    }
}

void TraceSkyVisibility(const vec3_t pos, const vec3_t normal, vec_t* fracs)
{
    int i;
    int k;

    for (i = 0; i < g_trace_skycount; i++)
    {
        fracs[i] = -1;
    }

    const int startcontents = TraceStartContents(pos);
    if (startcontents == CONTENTS_SOLID)
    {
        return;
    }

    for (i = 0; i < g_trace_skyslots; i += TESTLINE_PACKET_MAX)
    {
        float dx[TESTLINE_PACKET_MAX];
        float dy[TESTLINE_PACKET_MAX];
        float dz[TESTLINE_PACKET_MAX];
        int active[TESTLINE_PACKET_MAX];
        int results[TESTLINE_PACKET_MAX];
        float packetfracs[TESTLINE_PACKET_MAX];
        int anyactive = 0;

        for (k = 0; k < TESTLINE_PACKET_MAX; k++)
        {
            const float nx = g_trace_skydir[0][i + k];
            const float ny = g_trace_skydir[1][i + k];
            const float nz = g_trace_skydir[2][i + k];
            // the caller's own test: only directions the surface faces
            const float dot = -(normal[0] * nx + normal[1] * ny + normal[2] * nz);
            active[k] = (g_trace_skyindex[i + k] >= 0 && dot > NORMAL_EPSILON) ? -1 : 0;
            anyactive |= active[k];
            dx[k] = nx * -g_trace_skyrange;
            dy[k] = ny * -g_trace_skyrange;
            dz[k] = nz * -g_trace_skyrange;
            results[k] = CONTENTS_SOLID;
            packetfracs[k] = 0;
        }
        if (!anyactive)
        {
            continue;
        }
        if (startcontents != CONTENTS_SKY)
        {
            TraceDispatch(pos, startcontents, dx, dy, dz, active, results, packetfracs);
        }
        for (k = 0; k < TESTLINE_PACKET_MAX; k++)
        {
            if (!active[k])
            {
                continue;
            }
            if (startcontents == CONTENTS_SKY)
            {
                fracs[g_trace_skyindex[i + k]] = 0; // already in the sky, as TestLine_r answers
            }
            else if (results[k] == CONTENTS_SKY)
            {
                fracs[g_trace_skyindex[i + k]] = packetfracs[k];
            }
        }
    }
}

int TracePointContents(const vec3_t point)
{
    return TraceStartContents(point);
}

#ifdef HLRAD_OPAQUE_NODE

typedef struct
{
    Winding* winding;
    dplane_t plane;
    int numedges;
    dplane_t* edges;
#ifdef HLRAD_OPAQUE_ALPHATEST
    int texinfo;
    bool tex_alphatest;
    vec_t tex_vecs[2][4];
    int tex_width;
    int tex_height;
    const byte* tex_canvas;
#endif
} opaqueface_t;
opaqueface_t* opaquefaces;

typedef struct opaquenode_s
{
    planetypes type;
    vec3_t normal;
    vec_t dist;
    int children[2];
    int firstface;
    int numfaces;
} opaquenode_t;
opaquenode_t* opaquenodes;

#ifndef OPAQUE_NODE_INLINECALL
typedef struct
{
    vec3_t mins, maxs;
    int headnode;
} opaquemodel_t;
#endif
opaquemodel_t* opaquemodels;

bool TryMerge(opaqueface_t* f, const opaqueface_t* f2)
{
    if (!f->winding || !f2->winding)
    {
        return false;
    }
    if (fabs(f2->plane.dist - f->plane.dist) > ON_EPSILON ||
        fabs(f2->plane.normal[0] - f->plane.normal[0]) > NORMAL_EPSILON ||
        fabs(f2->plane.normal[1] - f->plane.normal[1]) > NORMAL_EPSILON ||
        fabs(f2->plane.normal[2] - f->plane.normal[2]) > NORMAL_EPSILON)
    {
        return false;
    }
#ifdef HLRAD_OPAQUE_ALPHATEST
    if ((f->tex_alphatest || f2->tex_alphatest) && f->texinfo != f2->texinfo)
    {
        return false;
    }
#endif

    Winding* w = f->winding;
    const Winding* w2 = f2->winding;
    const vec_t *pA, *pB, *pC, *pD, *p2A, *p2B, *p2C, *p2D;
    int i, i2;

    for (i = 0; i < w->m_NumPoints; i++)
    {
        for (i2 = 0; i2 < w2->m_NumPoints; i2++)
        {
            pA = w->m_Points[(i + w->m_NumPoints - 1) % w->m_NumPoints];
            pB = w->m_Points[i];
            pC = w->m_Points[(i + 1) % w->m_NumPoints];
            pD = w->m_Points[(i + 2) % w->m_NumPoints];
            p2A = w2->m_Points[(i2 + w2->m_NumPoints - 1) % w2->m_NumPoints];
            p2B = w2->m_Points[i2];
            p2C = w2->m_Points[(i2 + 1) % w2->m_NumPoints];
            p2D = w2->m_Points[(i2 + 2) % w2->m_NumPoints];
            if (!VectorCompare(pB, p2C) || !VectorCompare(pC, p2B))
            {
                continue;
            }
            break;
        }
        if (i2 == w2->m_NumPoints)
        {
            continue;
        }
        break;
    }
    if (i == w->m_NumPoints)
    {
        return false;
    }

    const vec_t* normal = f->plane.normal;
    vec3_t e1, e2;
    dplane_t pl1, pl2;
    int side1, side2;

    VectorSubtract(p2D, pA, e1);
    CrossProduct(normal, e1, pl1.normal); // pointing outward
    if (VectorNormalize(pl1.normal) == 0.0)
    {
        Developer(DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty edge.\n");
        return false;
    }
    pl1.dist = DotProduct(pA, pl1.normal);
    if (DotProduct(pB, pl1.normal) - pl1.dist < -ON_EPSILON)
    {
        return false;
    }
    side1 = (DotProduct(pB, pl1.normal) - pl1.dist > ON_EPSILON) ? 1 : 0;

    VectorSubtract(pD, p2A, e2);
    CrossProduct(normal, e2, pl2.normal); // pointing outward
    if (VectorNormalize(pl2.normal) == 0.0)
    {
        Developer(DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty edge.\n");
        return false;
    }
    pl2.dist = DotProduct(p2A, pl2.normal);
    if (DotProduct(p2B, pl2.normal) - pl2.dist < -ON_EPSILON)
    {
        return false;
    }
    side2 = (DotProduct(p2B, pl2.normal) - pl2.dist > ON_EPSILON) ? 1 : 0;

    Winding* neww = new Winding(w->m_NumPoints + w2->m_NumPoints - 4 + side1 + side2);
    int j, k;
    k = 0;
    for (j = (i + 2) % w->m_NumPoints; j != i; j = (j + 1) % w->m_NumPoints)
    {
        VectorCopy(w->m_Points[j], neww->m_Points[k]);
        k++;
    }
    if (side1)
    {
        VectorCopy(w->m_Points[j], neww->m_Points[k]);
        k++;
    }
    for (j = (i2 + 2) % w2->m_NumPoints; j != i2; j = (j + 1) % w2->m_NumPoints)
    {
        VectorCopy(w2->m_Points[j], neww->m_Points[k]);
        k++;
    }
    if (side2)
    {
        VectorCopy(w2->m_Points[j], neww->m_Points[k]);
        k++;
    }
    neww->RemoveColinearPoints();
    if (neww->m_NumPoints < 3)
    {
        Developer(DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty winding.\n");
        delete neww;
        neww = NULL;
    }
    delete f->winding;
    f->winding = neww;
    return true;
}

int MergeOpaqueFaces(int firstface, int numfaces)
{
    int i, j, newnum;
    opaqueface_t* faces = &opaquefaces[firstface];
    for (i = 0; i < numfaces; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (TryMerge(&faces[i], &faces[j]))
            {
                delete faces[j].winding;
                faces[j].winding = NULL;
                j = -1;
                continue;
            }
        }
    }
    for (i = 0, j = 0; i < numfaces; i++)
    {
        if (faces[i].winding)
        {
            faces[j] = faces[i];
            j++;
        }
    }
    newnum = j;
    for (; j < numfaces; j++)
    {
        memset(&faces[j], 0, sizeof(opaqueface_t));
    }
    return newnum;
}

void BuildFaceEdges(opaqueface_t* f)
{
    if (!f->winding)
        return;
    f->numedges = f->winding->m_NumPoints;
    f->edges = (dplane_t*)calloc(f->numedges, sizeof(dplane_t));
    hlassume(f->edges != NULL, assume_NoMemory);
    const vec_t *p1, *p2;
    const vec_t* n = f->plane.normal;
    vec3_t e;
    dplane_t* pl;
    int x;
    for (x = 0; x < f->winding->m_NumPoints; x++)
    {
        p1 = f->winding->m_Points[x];
        p2 = f->winding->m_Points[(x + 1) % f->winding->m_NumPoints];
        pl = &f->edges[x];
        VectorSubtract(p2, p1, e);
        CrossProduct(n, e, pl->normal);
        if (VectorNormalize(pl->normal) == 0.0)
        {
            Developer(DEVELOPER_LEVEL_WARNING, "Warning: BuildFaceEdges: Empty edge.\n");
            VectorClear(pl->normal);
            pl->dist = -1;
            continue;
        }
        pl->dist = DotProduct(pl->normal, p1);
    }
}

void CreateOpaqueNodes()
{
    int i, j;
    opaquemodels = (opaquemodel_t*)calloc(g_nummodels, sizeof(opaquemodel_t));
    opaquenodes = (opaquenode_t*)calloc(g_numnodes, sizeof(opaquenode_t));
    opaquefaces = (opaqueface_t*)calloc(g_numfaces, sizeof(opaqueface_t));
    hlassume(opaquemodels != NULL && opaquenodes != NULL && opaquefaces != NULL, assume_NoMemory);
    for (i = 0; i < g_numfaces; i++)
    {
        opaqueface_t* of = &opaquefaces[i];
        dface_t* df = &g_dfaces[i];
        of->winding = new Winding(*df);
        if (of->winding->m_NumPoints < 3)
        {
            delete of->winding;
            of->winding = NULL;
        }
        of->plane = g_dplanes[df->planenum];
        if (df->side)
        {
            VectorInverse(of->plane.normal);
            of->plane.dist = -of->plane.dist;
        }
#ifdef HLRAD_OPAQUE_ALPHATEST
        of->texinfo = df->texinfo;
        texinfo_t* info = &g_texinfo[of->texinfo];
        for (j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                of->tex_vecs[j][k] = info->vecs[j][k];
            }
        }
        radtexture_t* tex = &g_textures[info->miptex];
        of->tex_alphatest = tex->name[0] == '{';
        of->tex_width = tex->width;
        of->tex_height = tex->height;
        of->tex_canvas = tex->canvas;
#endif
    }
    for (i = 0; i < g_numnodes; i++)
    {
        opaquenode_t* on = &opaquenodes[i];
        dnode_t* dn = &g_dnodes[i];
        on->type = g_dplanes[dn->planenum].type;
        VectorCopy(g_dplanes[dn->planenum].normal, on->normal);
        on->dist = g_dplanes[dn->planenum].dist;
        on->children[0] = dn->children[0];
        on->children[1] = dn->children[1];
        on->firstface = dn->firstface;
        on->numfaces = dn->numfaces;
        on->numfaces = MergeOpaqueFaces(on->firstface, on->numfaces);
    }
    for (i = 0; i < g_numfaces; i++)
    {
        BuildFaceEdges(&opaquefaces[i]);
    }
    for (i = 0; i < g_nummodels; i++)
    {
        opaquemodel_t* om = &opaquemodels[i];
        dmodel_t* dm = &g_dmodels[i];
        om->headnode = dm->headnode[0];
        for (j = 0; j < 3; j++)
        {
            om->mins[j] = dm->mins[j] - 1;
            om->maxs[j] = dm->maxs[j] + 1;
        }
    }
}

void DeleteOpaqueNodes()
{
    int i;
    for (i = 0; i < g_numfaces; i++)
    {
        opaqueface_t* of = &opaquefaces[i];
        if (of->winding)
            delete of->winding;
        if (of->edges)
            free(of->edges);
    }
    free(opaquefaces);
    free(opaquenodes);
    free(opaquemodels);
}

int TestLineOpaque_face(int facenum, const vec3_t hit)
{
    opaqueface_t* thisface = &opaquefaces[facenum];
    int x;
    if (thisface->numedges == 0)
    {
        Developer(DEVELOPER_LEVEL_WARNING, "Warning: TestLineOpaque: Empty face.\n");
        return 0;
    }
    for (x = 0; x < thisface->numedges; x++)
    {
        if (DotProduct(hit, thisface->edges[x].normal) - thisface->edges[x].dist > ON_EPSILON)
        {
            return 0;
        }
    }
#ifdef HLRAD_OPAQUE_ALPHATEST
    if (thisface->tex_alphatest)
    {
        double x, y;
        x = DotProduct(hit, thisface->tex_vecs[0]) + thisface->tex_vecs[0][3];
        y = DotProduct(hit, thisface->tex_vecs[1]) + thisface->tex_vecs[1][3];
        x = floor(x - thisface->tex_width * floor(x / thisface->tex_width));
        y = floor(y - thisface->tex_height * floor(y / thisface->tex_height));
        x = x > thisface->tex_width - 1 ? thisface->tex_width - 1 : x < 0 ? 0 : x;
        y = y > thisface->tex_height - 1 ? thisface->tex_height - 1 : y < 0 ? 0 : y;
        if (thisface->tex_canvas[(int)y * thisface->tex_width + (int)x] == 0xFF)
        {
            return 0;
        }
    }
#endif
    return 1;
}

int TestLineOpaque_r(int nodenum, const vec3_t start, const vec3_t stop)
{
    opaquenode_t* thisnode;
    vec_t front, back;
    if (nodenum < 0)
    {
        return 0;
    }
    thisnode = &opaquenodes[nodenum];
    switch (thisnode->type)
    {
    case plane_x:
        front = start[0] - thisnode->dist;
        back = stop[0] - thisnode->dist;
        break;
    case plane_y:
        front = start[1] - thisnode->dist;
        back = stop[1] - thisnode->dist;
        break;
    case plane_z:
        front = start[2] - thisnode->dist;
        back = stop[2] - thisnode->dist;
        break;
    default:
        front = DotProduct(start, thisnode->normal) - thisnode->dist;
        back = DotProduct(stop, thisnode->normal) - thisnode->dist;
    }
    if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2)
    {
        return TestLineOpaque_r(thisnode->children[0], start, stop);
    }
    if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2)
    {
        return TestLineOpaque_r(thisnode->children[1], start, stop);
    }
    if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON)
    {
        return TestLineOpaque_r(thisnode->children[0], start, stop) ||
               TestLineOpaque_r(thisnode->children[1], start, stop);
    }
    {
        int side;
        vec_t frac;
        vec3_t mid;
        int facenum;
        side = (front - back) < 0;
        frac = front / (front - back);
        if (frac < 0)
            frac = 0;
        if (frac > 1)
            frac = 1;
        mid[0] = start[0] + (stop[0] - start[0]) * frac;
        mid[1] = start[1] + (stop[1] - start[1]) * frac;
        mid[2] = start[2] + (stop[2] - start[2]) * frac;
        for (facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
        {
            if (TestLineOpaque_face(facenum, mid))
            {
                return 1;
            }
        }
        return TestLineOpaque_r(thisnode->children[side], start, mid) ||
               TestLineOpaque_r(thisnode->children[!side], mid, stop);
    }
}

int TestLineOpaque(int modelnum, const vec3_t modelorigin, const vec3_t start, const vec3_t stop)
{
    opaquemodel_t* thismodel = &opaquemodels[modelnum];
    vec_t front, back, frac;
    vec3_t p1, p2;
    VectorSubtract(start, modelorigin, p1);
    VectorSubtract(stop, modelorigin, p2);
    int axial;
    for (axial = 0; axial < 3; axial++)
    {
        front = p1[axial] - thismodel->maxs[axial];
        back = p2[axial] - thismodel->maxs[axial];
        if (front >= -ON_EPSILON && back >= -ON_EPSILON)
        {
            return 0;
        }
        if (front > ON_EPSILON || back > ON_EPSILON)
        {
            frac = front / (front - back);
            if (front > back)
            {
                p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
            else
            {
                p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
        }
        front = thismodel->mins[axial] - p1[axial];
        back = thismodel->mins[axial] - p2[axial];
        if (front >= -ON_EPSILON && back >= -ON_EPSILON)
        {
            return 0;
        }
        if (front > ON_EPSILON || back > ON_EPSILON)
        {
            frac = front / (front - back);
            if (front > back)
            {
                p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
            else
            {
                p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
                p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
                p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
            }
        }
    }
    return TestLineOpaque_r(thismodel->headnode, p1, p2);
}

int CountOpaqueFaces_r(opaquenode_t* node)
{
    int count;
    count = node->numfaces;
    if (node->children[0] >= 0)
    {
        count += CountOpaqueFaces_r(&opaquenodes[node->children[0]]);
    }
    if (node->children[1] >= 0)
    {
        count += CountOpaqueFaces_r(&opaquenodes[node->children[1]]);
    }
    return count;
}

int CountOpaqueFaces(int modelnum)
{
    return CountOpaqueFaces_r(&opaquenodes[opaquemodels[modelnum].headnode]);
}

#ifdef HLRAD_OPAQUE_BLOCK
int TestPointOpaque_r(int nodenum, bool solid, const vec3_t point)
{
    opaquenode_t* thisnode;
    vec_t dist;
    while (1)
    {
        if (nodenum < 0)
        {
            if (solid && g_dleafs[-nodenum - 1].contents == CONTENTS_SOLID)
                return 1;
            else
                return 0;
        }
        thisnode = &opaquenodes[nodenum];
        switch (thisnode->type)
        {
        case plane_x:
            dist = point[0] - thisnode->dist;
            break;
        case plane_y:
            dist = point[1] - thisnode->dist;
            break;
        case plane_z:
            dist = point[2] - thisnode->dist;
            break;
        default:
            dist = DotProduct(point, thisnode->normal) - thisnode->dist;
        }
        if (dist > HUNT_WALL_EPSILON)
        {
            nodenum = thisnode->children[0];
        }
        else if (dist < -HUNT_WALL_EPSILON)
        {
            nodenum = thisnode->children[1];
        }
        else
        {
            break;
        }
    }
    {
        int facenum;
        for (facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
        {
            if (TestLineOpaque_face(facenum, point))
            {
                return 1;
            }
        }
    }
    return TestPointOpaque_r(thisnode->children[0], solid, point) ||
           TestPointOpaque_r(thisnode->children[1], solid, point);
}

#ifndef OPAQUE_NODE_INLINECALL
int TestPointOpaque(int modelnum, const vec3_t modelorigin, bool solid, const vec3_t point)
{
    opaquemodel_t* thismodel = &opaquemodels[modelnum];
    vec3_t newpoint;
    VectorSubtract(point, modelorigin, newpoint);
    int axial;
    for (axial = 0; axial < 3; axial++)
    {
        if (newpoint[axial] > thismodel->maxs[axial])
            return 0;
        if (newpoint[axial] < thismodel->mins[axial])
            return 0;
    }
    return TestPointOpaque_r(thismodel->headnode, solid, newpoint);
}
#endif
#endif

#endif

// =====================================================================================
// hlrad-ng: the scene of the GPU ray jobs
// =====================================================================================
// A device without ray queries gets the tnodes and walks them like the CPU
// does. A device with ray queries needs surfaces. A ray of TestLine_r stops at
// the first leaf whose contents differ from the contents at its start, so the
// surfaces are the parts of the node planes that separate two leaves of
// different contents: every node plane is cut down to the region of its node,
// then pushed through the front and the back subtree, which splits it into
// pieces that each lie between one front leaf and one back leaf. Pieces next
// to a sky leaf answer "sky", every other piece answers "solid"; the faces of
// the opaque entities are added as they are.
#if !defined(HLRAD_OPAQUE_NODE) || !defined(HLRAD_HULLU) || !defined(HLRAD_OPAQUE_STYLE)
#error "hlrad-ng GPU tracing needs the default hlrad feature set"
#endif

// Points of one piece of a node plane. Pieces of real maps stay far below this.
#define TRI_MAX_POINTS 32
// A point this close to a plane lies in it.
#define TRI_SPLIT_EPSILON 1e-5
// Every piece is grown a little along its plane, so that pieces overlap where
// they meet and no ray slips between two of them: by TRI_OVERLAP, plus
// TRI_OVERLAP_RELATIVE of its largest coordinate, which is a few times what a
// float rounds a coordinate of that size by. A ray that passes an edge closer
// than that is blocked; ON_EPSILON makes the BSP walk more tolerant still.
#define TRI_OVERLAP (1.0 / 512)
#define TRI_OVERLAP_RELATIVE 1e-6

typedef struct
{
    double p[TRI_MAX_POINTS][3];
    int n;
} tripoly_t;

typedef struct
{
    int node;          // where the piece is in the tree
    int frontcontents; // contents of its front leaf, 0 while it is still in the front subtree
    int backroot;      // where its walk of the back subtree starts
    tripoly_t poly;
} triwork_t;

static bool TriAddPoint(tripoly_t* poly, const double* point)
{
    if (poly->n == TRI_MAX_POINTS)
    {
        return false;
    }
    VectorCopy(point, poly->p[poly->n]);
    poly->n++;
    return true;
}

// Splits a convex polygon by a plane; a polygon in the plane goes to both
// sides. Either output may be NULL. Returns false when a piece would need
// more than TRI_MAX_POINTS points.
static bool TriSplit(const tripoly_t* in, const float* normal, const double dist, tripoly_t* front, tripoly_t* back)
{
    double dists[TRI_MAX_POINTS + 1];
    int sides[TRI_MAX_POINTS + 1]; // 0 front, 1 back, 2 on the plane
    int counts[3] = {0, 0, 0};
    int i;

    for (i = 0; i < in->n; i++)
    {
        dists[i] = in->p[i][0] * normal[0] + in->p[i][1] * normal[1] + in->p[i][2] * normal[2] - dist;
        sides[i] = dists[i] > TRI_SPLIT_EPSILON ? 0 : (dists[i] < -TRI_SPLIT_EPSILON ? 1 : 2);
        counts[sides[i]]++;
    }
    if (front)
    {
        front->n = 0;
    }
    if (back)
    {
        back->n = 0;
    }
    if (!counts[1])
    {
        if (front)
        {
            *front = *in;
        }
        if (back && !counts[0])
        {
            *back = *in;
        }
        return true;
    }
    if (!counts[0])
    {
        if (back)
        {
            *back = *in;
        }
        return true;
    }

    dists[in->n] = dists[0];
    sides[in->n] = sides[0];
    for (i = 0; i < in->n; i++)
    {
        const double* p1 = in->p[i];
        if (sides[i] != 1 && front && !TriAddPoint(front, p1))
        {
            return false;
        }
        if (sides[i] != 0 && back && !TriAddPoint(back, p1))
        {
            return false;
        }
        if (sides[i] == 2 || sides[i + 1] == 2 || sides[i + 1] == sides[i])
        {
            continue;
        }
        // the edge crosses the plane
        const double* p2 = in->p[(i + 1) % in->n];
        const double t = dists[i] / (dists[i] - dists[i + 1]);
        const double mid[3] = {p1[0] + (p2[0] - p1[0]) * t, p1[1] + (p2[1] - p1[1]) * t, p1[2] + (p2[2] - p1[2]) * t};
        if ((front && !TriAddPoint(front, mid)) || (back && !TriAddPoint(back, mid)))
        {
            return false;
        }
    }
    return true;
}

// A square on the plane of a node, large enough to hold everything a ray can reach.
static void TriBasePolygon(tripoly_t* poly, const tnode_t* tnode, const double* center, const double extent)
{
    const double normal[3] = {tnode->normal[0], tnode->normal[1], tnode->normal[2]};
    double up[3] = {0, 0, 0};
    double right[3];
    double org[3];
    int k;

    up[(fabs(normal[2]) > fabs(normal[0]) && fabs(normal[2]) > fabs(normal[1])) ? 0 : 2] = 1;
    const double along = DotProduct(up, normal);
    VectorMA(up, -along, normal, up);
    const double length = sqrt(DotProduct(up, up));
    VectorScale(up, 1 / length, up);
    CrossProduct(up, normal, right);

    const double offset = DotProduct(center, normal) - (double)tnode->dist;
    VectorMA(center, -offset, normal, org);
    for (k = 0; k < 3; k++)
    {
        poly->p[0][k] = org[k] - right[k] * extent + up[k] * extent;
        poly->p[1][k] = org[k] + right[k] * extent + up[k] * extent;
        poly->p[2][k] = org[k] + right[k] * extent - up[k] * extent;
        poly->p[3][k] = org[k] - right[k] * extent - up[k] * extent;
    }
    poly->n = 4;
}

// Adds a piece to the triangle list, grown along its plane (TRI_OVERLAP).
static void TriEmit(const tripoly_t* poly, const float* planenormal, std::vector<float>& triangles)
{
    const double normal[3] = {planenormal[0], planenormal[1], planenormal[2]};
    double points[TRI_MAX_POINTS][3];
    double outward[TRI_MAX_POINTS][3];
    double moved[TRI_MAX_POINTS][3];
    double center[3] = {0, 0, 0};
    double largest = 0;
    int n = 0;
    int i;
    int k;

    // drop repeated points
    for (i = 0; i < poly->n; i++)
    {
        if (n > 0)
        {
            double step[3];
            VectorSubtract(poly->p[i], points[n - 1], step);
            if (DotProduct(step, step) <= 1e-12)
            {
                continue;
            }
        }
        VectorCopy(poly->p[i], points[n]);
        n++;
    }
    if (n > 1)
    {
        double step[3];
        VectorSubtract(points[0], points[n - 1], step);
        if (DotProduct(step, step) <= 1e-12)
        {
            n--;
        }
    }
    if (n < 3)
    {
        return;
    }

    for (i = 0; i < n; i++)
    {
        VectorAdd(center, points[i], center);
        for (k = 0; k < 3; k++)
        {
            largest = qmax(largest, fabs(points[i][k]));
        }
    }
    const double overlap = TRI_OVERLAP + TRI_OVERLAP_RELATIVE * largest;
    VectorScale(center, 1.0 / n, center);

    // the direction that leaves the piece across each edge
    for (i = 0; i < n; i++)
    {
        double edge[3];
        double tocenter[3];
        VectorSubtract(points[(i + 1) % n], points[i], edge);
        const double length = sqrt(DotProduct(edge, edge));
        VectorScale(edge, 1 / length, edge);
        CrossProduct(edge, normal, outward[i]);
        VectorSubtract(points[i], center, tocenter);
        if (DotProduct(outward[i], tocenter) < 0)
        {
            VectorSubtract(vec3_origin, outward[i], outward[i]);
        }
    }
    // every corner moves out across both of its edges; a sharp corner would
    // move very far, so its reach is limited
    for (i = 0; i < n; i++)
    {
        const double* a = outward[(i + n - 1) % n];
        const double* b = outward[i];
        const double scale = overlap / qmax(1 + DotProduct(a, b), 0.25);
        for (k = 0; k < 3; k++)
        {
            moved[i][k] = points[i][k] + (a[k] + b[k]) * scale;
        }
    }

    for (i = 1; i + 1 < n; i++)
    {
        const double* corners[3] = {moved[0], moved[i], moved[i + 1]};
        for (k = 0; k < 3; k++)
        {
            triangles.push_back((float)corners[k][0]);
            triangles.push_back((float)corners[k][1]);
            triangles.push_back((float)corners[k][2]);
        }
    }
}

// Whether the GPU scene can stand in for TestSegmentAgainstOpaqueList: every
// opaque entity has to block light completely, with no light style of its own,
// no tint and no see-through texture.
static bool TraceOpaqueIsPlain()
{
    for (unsigned x = 0; x < g_opaque_face_count; x++)
    {
        const opaqueList_t* opaque = &g_opaque_face_list[x];
        const dmodel_t* model = &g_dmodels[opaque->modelnum];
        if (opaque->transparency || opaque->style != -1)
        {
            return false;
        }
        for (int facenum = model->firstface; facenum < model->firstface + model->numfaces; facenum++)
        {
            if (g_textures[g_texinfo[g_dfaces[facenum].texinfo].miptex].name[0] == '{')
            {
                return false;
            }
        }
    }
    return true;
}

// The world and the opaque entities as triangles: the ones that answer
// "solid" first, the ones that answer "sky" from *firstsky on.
static bool BuildSceneTriangles(std::vector<float>& triangles, unsigned* firstsky)
{
    const int numtnodes = (int)(tnode_p - tnodes);
    std::vector<int> parent((size_t)numtnodes, -1);
    std::vector<char> parentside((size_t)numtnodes, 0);
    std::vector<float> sky;
    std::vector<triwork_t> stack;
    double center[3];
    int node;
    int k;

    for (node = 0; node < numtnodes; node++)
    {
        for (k = 0; k < 2; k++)
        {
            const int child = tnodes[node].children[k];
            if (child >= 0)
            {
                parent[(size_t)child] = node;
                parentside[(size_t)child] = (char)k;
            }
        }
    }
    for (k = 0; k < 3; k++)
    {
        center[k] = ((double)g_dmodels[0].mins[k] + (double)g_dmodels[0].maxs[k]) / 2;
    }
    // no ray is longer than the sky range, and every ray starts inside the world
    const double extent = 2.0 * (double)g_trace_skyrange + 1024;

    for (node = 0; node < numtnodes; node++)
    {
        triwork_t work;
        TriBasePolygon(&work.poly, &tnodes[node], center, extent);

        // cut the plane down to the region of its node
        for (int child = node, up = parent[(size_t)node]; up >= 0 && work.poly.n >= 3;
             child = up, up = parent[(size_t)up])
        {
            tripoly_t piece;
            const bool infront = parentside[(size_t)child] == 0;
            if (!TriSplit(&work.poly, tnodes[up].normal, tnodes[up].dist, infront ? &piece : NULL,
                          infront ? NULL : &piece))
            {
                return false;
            }
            work.poly = piece;
        }
        if (work.poly.n < 3)
        {
            continue;
        }

        work.node = tnodes[node].children[0];
        work.frontcontents = 0;
        work.backroot = tnodes[node].children[1];
        stack.push_back(work);
        while (!stack.empty())
        {
            work = stack.back();
            stack.pop_back();
            if (work.node < 0)
            {
                if (work.frontcontents == 0)
                {
                    // a front leaf: find what lies behind this piece
                    work.frontcontents = work.node;
                    work.node = work.backroot;
                    stack.push_back(work);
                    continue;
                }
                const int back = work.node;
                if (back == work.frontcontents)
                {
                    continue; // no change of contents: rays pass
                }
                const bool tosky = back == CONTENTS_SKY || work.frontcontents == CONTENTS_SKY;
                if (tosky && (back == CONTENTS_SOLID || work.frontcontents == CONTENTS_SOLID))
                {
                    continue; // no ray gets between solid and sky
                }
                TriEmit(&work.poly, tnodes[node].normal, tosky ? sky : triangles);
                continue;
            }

            const tnode_t* split = &tnodes[work.node];
            triwork_t front = work;
            triwork_t back = work;
            if (!TriSplit(&work.poly, split->normal, split->dist, &front.poly, &back.poly))
            {
                return false;
            }
            if (front.poly.n >= 3)
            {
                front.node = split->children[0];
                stack.push_back(front);
            }
            if (back.poly.n >= 3)
            {
                back.node = split->children[1];
                stack.push_back(back);
            }
        }
    }

    // opaque entities block with their faces, seen from either side
    for (unsigned x = 0; x < g_opaque_face_count; x++)
    {
        const opaqueList_t* opaque = &g_opaque_face_list[x];
        const dmodel_t* model = &g_dmodels[opaque->modelnum];
        for (int facenum = model->firstface; facenum < model->firstface + model->numfaces; facenum++)
        {
            const Winding winding(g_dfaces[facenum]);
            for (int i = 1; i + 1 < (int)winding.m_NumPoints; i++)
            {
                const vec_t* corners[3] = {winding.m_Points[0], winding.m_Points[i], winding.m_Points[i + 1]};
                for (k = 0; k < 3; k++)
                {
                    triangles.push_back(corners[k][0] + opaque->origin[0]);
                    triangles.push_back(corners[k][1] + opaque->origin[1]);
                    triangles.push_back(corners[k][2] + opaque->origin[2]);
                }
            }
        }
    }

    *firstsky = (unsigned)(triangles.size() / 9);
    triangles.insert(triangles.end(), sky.begin(), sky.end());
    return !triangles.empty();
}

// Depth the stack of the BSP walk in gputrace.glsl allows (TRACE_STACK_DEPTH there).
#define TRACE_GPU_STACK_DEPTH 64

bool TraceGpuScene(unsigned* firstsky)
{
    static bool tried = false;
    static bool ready = false;
    static unsigned first = 0;
    const int numtnodes = (int)(tnode_p - tnodes);

    if (!tried)
    {
        tried = true;
        // HLRAD_NG_GPU=0 keeps every ray on the CPU and HLRAD_NG_GPU=1 uses
        // whatever device there is. Left alone, the GPU is used when it is a
        // graphics card or traces rays in hardware; an older integrated one
        // is slower than the CPU tracer.
        const char* mode = getenv("HLRAD_NG_GPU");
        const bool forced = mode && mode[0] == '1';
        if (!(mode && mode[0] == '0') && numtnodes > 0 && TraceOpaqueIsPlain() && GpuInit() &&
            (forced || GpuIsDiscrete() || GpuHasRayQuery()))
        {
            if (GpuHasRayQuery())
            {
                std::vector<float> triangles;
                ready = BuildSceneTriangles(triangles, &first) &&
                        GpuSetSceneTriangles(&triangles[0], (int)(triangles.size() / 9));
            }
            else if (g_trace_depth < TRACE_GPU_STACK_DEPTH && g_opaque_face_count == 0)
            {
                // the BSP walk knows nothing of the opaque entities
                std::vector<gpunode_t> nodes((size_t)numtnodes);
                for (int i = 0; i < numtnodes; i++)
                {
                    gpunode_t* node = &nodes[(size_t)i];
                    VectorCopy(tnodes[i].normal, node->plane);
                    node->plane[3] = tnodes[i].dist;
                    node->children[0] = tnodes[i].children[0];
                    node->children[1] = tnodes[i].children[1];
                    node->pad[0] = 0;
                    node->pad[1] = 0;
                }
                ready = GpuSetSceneNodes(&nodes[0], numtnodes);
            }
        }
    }
    *firstsky = first;
    return ready;
}
