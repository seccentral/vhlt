// =====================================================================================
// hlrad-ng packet tracer kernel
// =====================================================================================
// Included by trace.cpp once per vector width, with these macros set:
//   TP_W        number of rays per packet (4 or 8)
//   TP_NAME(x)  name mangling for this width
//   TP_ATTR     function attributes (the 8-wide build needs target("avx2"))
//
// A packet is TP_W line segments that share one start point. The kernel walks
// the world BSP (tnodes) once for the whole packet and applies, per ray, the
// same plane rules as TestLine_r: a segment clearly in front of or behind a
// plane follows that child, a segment lying in the plane (within ON_EPSILON)
// follows both, anything else is split at the plane. Instead of moving the
// end points at every split, each ray keeps a parameter interval [t0, t1] on
// its original segment, so rounding does not build up along the walk.
//
// The result of a ray is the first leaf whose contents differ from the
// contents at the start point: sky gives CONTENTS_SKY, anything else blocks
// (CONTENTS_SOLID, which also covers water surfaces, as in TestLine_r). When
// two changes tie, solid wins.
//
// The walk is iterative with an explicit stack of TRACE_STACK_DEPTH entries;
// trace.cpp only uses packets when the tree is shallower than that. Axial
// planes must carry exact unit normals (trace.cpp normalizes them), so one
// branch-free plane evaluation serves every plane type.

typedef float TP_NAME(vf) __attribute__((vector_size(TP_W * 4)));
typedef int TP_NAME(vi) __attribute__((vector_size(TP_W * 4)));

typedef struct
{
    TP_NAME(vf) t0, t1;
    TP_NAME(vi) active;
    int node;
} TP_NAME(stackentry_t);

TP_ATTR static inline __attribute__((always_inline)) void TP_NAME(Splat)(TP_NAME(vf) & out, const float x)
{
    for (int i = 0; i < TP_W; i++)
    {
        out[i] = x;
    }
}

TP_ATTR static inline __attribute__((always_inline)) void TP_NAME(SplatI)(TP_NAME(vi) & out, const int x)
{
    for (int i = 0; i < TP_W; i++)
    {
        out[i] = x;
    }
}

// out = mask ? a : b, per lane
TP_ATTR static inline __attribute__((always_inline)) void TP_NAME(Select)(TP_NAME(vf) & out, const TP_NAME(vi) & mask,
                                                                          const TP_NAME(vf) & a, const TP_NAME(vf) & b)
{
    out = (TP_NAME(vf))(((TP_NAME(vi))a & mask) | ((TP_NAME(vi))b & ~mask));
}

// Traces TP_W segments from origin; dx, dy and dz hold TP_W deltas each
// (stop - start), activelanes holds TP_W lane flags (0 or -1). results and
// fracs receive TP_W entries; inactive lanes get CONTENTS_SOLID.
TP_ATTR static void TP_NAME(TracePacket)(const float* origin, const int startcontents, const float* dx, const float* dy,
                                         const float* dz, const int* activelanes, int* results, float* fracs)
{
    TP_NAME(stackentry_t) stack[TRACE_STACK_DEPTH];
    int sp = 0;

    TP_NAME(vf) rdx;
    TP_NAME(vf) rdy;
    TP_NAME(vf) rdz;
    TP_NAME(vf) best;   // parameter of the closest contents change so far
    TP_NAME(vi) result; // contents found there, CONTENTS_EMPTY while none
    TP_NAME(vf) t0;
    TP_NAME(vf) t1;
    TP_NAME(vi) active;
    TP_NAME(vf) eps;
    TP_NAME(vf) halfeps;
    TP_NAME(vi) sky;
    TP_NAME(vi) solid;
    int node = 0;

    memcpy(&rdx, dx, sizeof(rdx));
    memcpy(&rdy, dy, sizeof(rdy));
    memcpy(&rdz, dz, sizeof(rdz));
    memcpy(&active, activelanes, sizeof(active));
    TP_NAME(Splat)(best, 2.0f); // beyond the end of every segment
    TP_NAME(SplatI)(result, CONTENTS_EMPTY);
    TP_NAME(Splat)(t0, 0.0f);
    TP_NAME(Splat)(t1, 1.0f);
    TP_NAME(Splat)(eps, (float)ON_EPSILON);
    TP_NAME(Splat)(halfeps, (float)(ON_EPSILON / 2));
    TP_NAME(SplatI)(sky, CONTENTS_SKY);
    TP_NAME(SplatI)(solid, CONTENTS_SOLID);

    for (;;)
    {
        // a ray is done with everything behind its closest contents change
        active &= (t0 <= best);
        if (__builtin_reduce_or(active))
        {
#ifdef HLRAD_NG_PROFILE
            g_profile_tracenodes++;
#endif
            if (node >= 0)
            {
                const tnode_t* tnode = &tnodes[node];
                TP_NAME(vf) nx;
                TP_NAME(vf) ny;
                TP_NAME(vf) nz;
                TP_NAME(vf) vfront;
                TP_NAME(Splat)(nx, tnode->normal[0]);
                TP_NAME(Splat)(ny, tnode->normal[1]);
                TP_NAME(Splat)(nz, tnode->normal[2]);
                const float front =
                    (origin[0] * tnode->normal[0] + origin[1] * tnode->normal[1] + origin[2] * tnode->normal[2]) -
                    tnode->dist;
                TP_NAME(Splat)(vfront, front);
                const TP_NAME(vf) nd = rdx * nx + rdy * ny + rdz * nz; // plane normal . ray delta

                // signed distances of both interval ends to the plane
                const TP_NAME(vf) d0 = vfront + t0 * nd;
                const TP_NAME(vf) d1 = vfront + t1 * nd;

                const TP_NAME(vi) onlyfront = (d0 > halfeps) & (d1 > halfeps);
                const TP_NAME(vi) onlyback = (d0 < -halfeps) & (d1 < -halfeps);
                const TP_NAME(vi) onplane =
                    ~(onlyfront | onlyback) & (d0 <= eps) & (d0 >= -eps) & (d1 <= eps) & (d1 >= -eps);
                const TP_NAME(vi) split = ~(onlyfront | onlyback | onplane);

                // Parameter where the ray crosses the plane, clamped to the
                // interval. It comes from the plane and the ray alone, so the
                // division stays off the path that carries t0 and t1 from
                // node to node. Lanes that do not split may hold inf or nan
                // here and are never selected below.
                TP_NAME(vf) tm = -vfront / nd;
                TP_NAME(Select)(tm, tm < t0, t0, tm);
                TP_NAME(Select)(tm, tm > t1, t1, tm);

                // a splitting ray that heads toward the front side starts behind the plane
                const TP_NAME(vi) startsback = split & (d0 < d1);
                const TP_NAME(vi) startsfront = split & ~(d0 < d1);

                TP_NAME(vf) f0;
                TP_NAME(vf) f1;
                TP_NAME(vf) b0;
                TP_NAME(vf) b1;
                TP_NAME(Select)(f0, startsback, tm, t0);
                TP_NAME(Select)(f1, startsfront, tm, t1);
                TP_NAME(Select)(b0, startsfront, tm, t0);
                TP_NAME(Select)(b1, startsback, tm, t1);
                const TP_NAME(vi) frontactive = active & ~onlyback;
                const TP_NAME(vi) backactive = active & ~onlyfront;

                // the side holding the start point first, so close changes
                // prune the rest; the other side waits on the stack
                TP_NAME(vi) startinfront;
                TP_NAME(SplatI)(startinfront, front >= 0 ? -1 : 0);
                TP_NAME(stackentry_t)* far = &stack[sp++];
                TP_NAME(Select)(far->t0, startinfront, b0, f0);
                TP_NAME(Select)(far->t1, startinfront, b1, f1);
                far->active = (backactive & startinfront) | (frontactive & ~startinfront);
                far->node = tnode->children[front >= 0 ? 1 : 0];
                TP_NAME(Select)(t0, startinfront, f0, b0);
                TP_NAME(Select)(t1, startinfront, f1, b1);
                active = (frontactive & startinfront) | (backactive & ~startinfront);
                node = tnode->children[front >= 0 ? 0 : 1];
                continue;
            }
            if (node != startcontents)
            {
                // a leaf of other contents ends the rays that reach it;
                // solid wins a tie against sky
                const TP_NAME(vi) issky = node == CONTENTS_SKY ? ~(sky ^ sky) : (sky ^ sky);
                const TP_NAME(vi) hit = active & ((t0 < best) | ~issky);
                TP_NAME(Select)(best, hit, t0, best);
                result = (((sky & issky) | (solid & ~issky)) & hit) | (result & ~hit);
            }
        }
        if (sp == 0)
        {
            break;
        }
        sp--;
        t0 = stack[sp].t0;
        t1 = stack[sp].t1;
        active = stack[sp].active;
        node = stack[sp].node;
    }

    for (int i = 0; i < TP_W; i++)
    {
        results[i] = activelanes[i] ? result[i] : (int)CONTENTS_SOLID;
        fracs[i] = best[i];
    }
}
