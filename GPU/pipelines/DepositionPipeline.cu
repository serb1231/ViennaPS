#include <optix_device.h>

#include <curtBoundary.hpp>
#include <curtLaunchParams.hpp>
#include <curtPerRayData.hpp>
#include <curtRNG.hpp>
#include <curtReflection.hpp>
#include <curtSBTRecords.hpp>
#include <curtSource.hpp>

#include "context.hpp"

/*  launch parameters in constant memory, filled in by optix upon
    optixLaunch (this gets filled in from the buffer we pass to
    optixLaunch) */
extern "C" __constant__ curtLaunchParams<NumericType> params;

// for this simple example, we have a single ray type
enum
{
    SURFACE_RAY_TYPE = 0,
    RAY_TYPE_COUNT
};

extern "C" __global__ void __closesthit__depoParticle()
{
    const HitSBTData *sbtData = (const HitSBTData *)optixGetSbtDataPointer();
    PerRayData<NumericType> *prd =
        (PerRayData<NumericType> *)getPRD<PerRayData<NumericType>>();

    if (sbtData->isBoundary)
    {
        if (params.periodicBoundary)
        {
            applyPeriodicBoundary(prd, sbtData);
        }
        else
        {
            reflectFromBoundary(prd);
        }
    }
    else
    {
        const unsigned int primID = optixGetPrimitiveIndex();
        const float sticking = params.sticking;
        atomicAdd(&params.resultBuffer[primID], prd->rayWeight * sticking);
        prd->rayWeight -= prd->rayWeight * sticking;
        diffuseReflection(prd);
    }
}

//------------------------------------------------------------------------------
// miss program that gets called for any ray that did not have a
// valid intersection
// ------------------------------------------------------------------------------
extern "C" __global__ void __miss__depoParticle()
{
    getPRD<PerRayData<NumericType>>()->rayWeight = 0.f;
}

//------------------------------------------------------------------------------
// ray gen program - entry point
//------------------------------------------------------------------------------
extern "C" __global__ void __raygen__depoParticle()
{
    const uint3 idx = optixGetLaunchIndex();
    const uint3 dims = optixGetLaunchDimensions();
    const int linearLaunchIndex =
        idx.x + idx.y * dims.x + idx.z * dims.x * dims.y;

    // per-ray data
    PerRayData<NumericType> prd;
    prd.rayWeight = 1.f;
    // each ray has its own RNG state
    initializeRNGState(&prd, linearLaunchIndex, params.seed);

    // generate ray direction
    const NumericType sourcePower = params.cosineExponent;
    initializeRayRandom(&prd, &params, sourcePower, idx);

    // the values we store the PRD pointer in:
    uint32_t u0, u1;
    packPointer((void *)&prd, u0, u1);

    while (prd.rayWeight > params.rayWeightThreshold)
    {
        optixTrace(params.traversable, // traversable GAS
                   prd.pos,            // origin
                   prd.dir,            // direction
                   1e-4f,              // tmin
                   1e20f,              // tmax
                   0.0f,               // rayTime
                   OptixVisibilityMask(255),
                   OPTIX_RAY_FLAG_DISABLE_ANYHIT, // OPTIX_RAY_FLAG_NONE,
                   SURFACE_RAY_TYPE,              // SBT offset
                   RAY_TYPE_COUNT,                // SBT stride
                   SURFACE_RAY_TYPE,              // missSBTIndex
                   u0, u1);
    }
}
