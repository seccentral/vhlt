// hlrad-ng GPU ray kernel, included by the compute shaders (gpudirect.comp, gpuvis.comp).
//
// TraceLine answers what TestLine_r answers on the CPU: what a line segment
// meets first, seen from the contents of the leaf it starts in. There are two
// builds of every shader:
//
//   GPU_RAYQUERY defined   hardware ray tracing (VK_KHR_ray_query). The scene
//                          is the set of surfaces between leaves of different
//                          contents (trace.cpp builds them from the BSP tree);
//                          the triangles next to a sky leaf come last, from
//                          primitive TRACE_FIRST_SKY on.
//   otherwise              a walk of the world BSP with the plane rules of the
//                          CPU packet kernel (tracepacket.h): a ray keeps a
//                          parameter interval [t0, t1] on its segment, a
//                          segment within ON_EPSILON of a plane follows both
//                          children, and solid wins a tie against sky.
//
// The including shader defines TRACE_FIRST_SKY and TRACE_ON_EPSILON before the
// include. Binding 0 of set 0 belongs to this file.

const int CONTENTS_EMPTY = -1;
const int CONTENTS_SOLID = -2;
const int CONTENTS_SKY = -6;

#ifdef GPU_RAYQUERY

#extension GL_EXT_ray_query : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;

// What the segment origin -> origin + delta meets first: CONTENTS_EMPTY for
// nothing, CONTENTS_SKY for the sky, CONTENTS_SOLID for anything else. With
// needsky false the caller only wants to know whether the segment is clear,
// and a blocked segment may report CONTENTS_SOLID for the sky as well.
int TraceLine(vec3 origin, vec3 delta, int startcontents, bool needsky)
{
    if (startcontents == CONTENTS_SOLID || startcontents == CONTENTS_SKY)
    {
        return startcontents;
    }
    rayQueryEXT query;
    uint flags = gl_RayFlagsOpaqueEXT;
    if (!needsky)
    {
        flags |= gl_RayFlagsTerminateOnFirstHitEXT;
    }
    rayQueryInitializeEXT(query, scene, flags, 0xFFu, origin, 0.0, delta, 1.0);
    while (rayQueryProceedEXT(query))
    {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT)
    {
        return CONTENTS_EMPTY;
    }
    if (!needsky)
    {
        return CONTENTS_SOLID;
    }
    return uint(rayQueryGetIntersectionPrimitiveIndexEXT(query, true)) >= TRACE_FIRST_SKY ? CONTENTS_SKY
                                                                                           : CONTENTS_SOLID;
}

#else

struct Node
{
    vec4 plane;     // normal xyz, dist
    ivec2 children; // tnode index, or negative leaf contents
    ivec2 pad;
};

layout(std430, set = 0, binding = 0) readonly buffer Nodes
{
    Node nodes[];
};

// trace.cpp only uses this build when the tree is shallower than the stack
const int TRACE_STACK_DEPTH = 64;

int TraceLine(vec3 origin, vec3 delta, int startcontents, bool needsky)
{
    if (startcontents == CONTENTS_SOLID || startcontents == CONTENTS_SKY)
    {
        return startcontents;
    }

    int stackNode[TRACE_STACK_DEPTH];
    float stackT0[TRACE_STACK_DEPTH];
    float stackT1[TRACE_STACK_DEPTH];
    int sp = 0;

    int node = 0;
    float t0 = 0.0;
    float t1 = 1.0;
    float best = 2.0; // parameter of the closest contents change so far
    int result = CONTENTS_EMPTY;
    float halfeps = TRACE_ON_EPSILON * 0.5;

    for (;;)
    {
        if (t0 <= best)
        {
            if (node >= 0)
            {
                Node n = nodes[node];
                float front = dot(origin, n.plane.xyz) - n.plane.w;
                float nd = dot(delta, n.plane.xyz);
                float d0 = front + t0 * nd;
                float d1 = front + t1 * nd;

                if (d0 > halfeps && d1 > halfeps)
                {
                    node = n.children.x;
                    continue;
                }
                if (d0 < -halfeps && d1 < -halfeps)
                {
                    node = n.children.y;
                    continue;
                }

                float f0 = t0;
                float f1 = t1;
                float b0 = t0;
                float b1 = t1;
                if (!(abs(d0) <= TRACE_ON_EPSILON && abs(d1) <= TRACE_ON_EPSILON))
                {
                    // split where the ray crosses the plane
                    float tm = clamp(-front / nd, t0, t1);
                    if (d0 < d1)
                    {
                        b1 = tm;
                        f0 = tm;
                    }
                    else
                    {
                        f1 = tm;
                        b0 = tm;
                    }
                }

                // the side holding the start point first; the other waits
                if (front >= 0.0)
                {
                    stackNode[sp] = n.children.y;
                    stackT0[sp] = b0;
                    stackT1[sp] = b1;
                    sp++;
                    node = n.children.x;
                    t0 = f0;
                    t1 = f1;
                }
                else
                {
                    stackNode[sp] = n.children.x;
                    stackT0[sp] = f0;
                    stackT1[sp] = f1;
                    sp++;
                    node = n.children.y;
                    t0 = b0;
                    t1 = b1;
                }
                continue;
            }
            if (node != startcontents)
            {
                if (!needsky)
                {
                    return CONTENTS_SOLID; // any change of contents blocks the segment
                }
                if (node == CONTENTS_SKY)
                {
                    if (t0 < best)
                    {
                        best = t0;
                        result = CONTENTS_SKY;
                    }
                }
                else
                {
                    best = t0;
                    result = CONTENTS_SOLID;
                }
            }
        }
        if (sp == 0)
        {
            break;
        }
        sp--;
        node = stackNode[sp];
        t0 = stackT0[sp];
        t1 = stackT1[sp];
    }
    return result;
}

#endif
