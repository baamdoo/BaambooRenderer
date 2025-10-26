#ifndef _HLSL_MATH_HEADER
#define _HLSL_MATH_HEADER

struct Plane
{
    float3 normal;
    float  d;
};
#define PLANE_INTERSECTION_POSITIVE_HALFSPACE 0
#define PLANE_INTERSECTION_NEGATIVE_HALFSPACE 1
#define PLANE_INTERSECTION_INTERSECTING		  2

#define FRUSTUM_CONTAINMENT_DISJOINT          0
#define FRUSTUM_CONTAINMENT_INTERSECTS        1
#define FRUSTUM_CONTAINMENT_CONTAINS          2

#define FRUSTUM_PLANE_LEFT	 0
#define FRUSTUM_PLANE_RIGHT	 1
#define FRUSTUM_PLANE_BOTTOM 2
#define FRUSTUM_PLANE_TOP	 3
#define FRUSTUM_PLANE_NEAR	 4
#define FRUSTUM_PLANE_FAR	 5
struct Frustum
{
    Plane Left;   // -x
    Plane Right;  // +x
    Plane Bottom; // -y
    Plane Top;    // +y
    Plane Near;   // -z
    Plane Far;    // +z
};

struct BoundingSphere
{
    float3 center;
    float  radius;
    
    void Transform(float4x4 mTransform)
    {
        center = mul(float4(center, 1.0), mTransform).xyz;
    
        float3 scale;
        scale.x = length(float3(mTransform[0][0], mTransform[0][1], mTransform[0][2]));
        scale.y = length(float3(mTransform[1][0], mTransform[1][1], mTransform[1][2]));
        scale.z = length(float3(mTransform[2][0], mTransform[2][1], mTransform[2][2]));
        float maxScale = max(max(scale.x, scale.y), scale.z);
    
        radius = radius * maxScale;
    }
    
    bool Intersects(BoundingSphere other)
    {
        float3 d     = center - other.center;
        float  dist2 = dot(d, d);
        
        float radiusSum = radius + other.radius;
        float r2        = radiusSum * radiusSum;
        
        return dist2 <= r2;
    }
    
    int IntersectsToPlane(Plane p)
    {
        float sd = dot(center, p.normal) - p.d;
        if (sd > radius)
        {
            return PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        }
        if (sd < -radius)
        {
            return PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        }
        
        return PLANE_INTERSECTION_INTERSECTING;
    }
    
    int FrustumCheck(Frustum f)
    {
        int p0 = IntersectsToPlane(f.Left);
        int p1 = IntersectsToPlane(f.Right);
        int p2 = IntersectsToPlane(f.Bottom);
        int p3 = IntersectsToPlane(f.Top);
        int p4 = IntersectsToPlane(f.Near);
        int p5 = IntersectsToPlane(f.Far);
        
        bool bAnyOutside = p0 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside     |= p1 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside     |= p2 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside     |= p3 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside     |= p4 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside     |= p5 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        
        bool bAllInside = p0 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside     &= p1 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside     &= p2 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside     &= p3 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside     &= p4 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside     &= p5 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;

        if (bAnyOutside)
        {
            return FRUSTUM_CONTAINMENT_DISJOINT;
        }

        if (bAllInside)
        {
            return FRUSTUM_CONTAINMENT_CONTAINS;
        }
        
        return FRUSTUM_CONTAINMENT_INTERSECTS;
    }
};

struct BoundingBox
{
    float3 center;
    float  padding0;
    float3 extents;
    float  padding1;
    
    void Transform(float4x4 mTransform)
    {
        float3 corners[8] =
        {
            center + float3(-extents.x, -extents.y, -extents.z),
            center + float3( extents.x, -extents.y, -extents.z),
            center + float3(-extents.x,  extents.y, -extents.z),
            center + float3( extents.x,  extents.y, -extents.z),
            center + float3(-extents.x, -extents.y,  extents.z),
            center + float3( extents.x, -extents.y,  extents.z),
            center + float3(-extents.x,  extents.y,  extents.z),
            center + float3( extents.x,  extents.y,  extents.z)
        };
    
        float3 minPoint = float3(1e10, 1e10, 1e10);
        float3 maxPoint = float3(-1e10, -1e10, -1e10);
    
        [unroll]
        for (int i = 0; i < 8; ++i)
        {
            float3 transformedCorner = mul(float4(corners[i], 1.0), mTransform).xyz;
            minPoint = min(minPoint, transformedCorner);
            maxPoint = max(maxPoint, transformedCorner);
        }
    
        center  = (minPoint + maxPoint) * 0.5;
        extents = (maxPoint - minPoint) * 0.5;
    }
    
    bool Intersects(BoundingBox other)
    {
        float3 minA = center - extents;
        float3 maxA = center + extents;

        float3 minB = other.center - other.extents;
        float3 maxB = other.center + other.extents;

        return maxA.x >= minB.x && minA.x <= maxB.x &&
			   maxA.y >= minB.y && minA.y <= maxB.y &&
			   maxA.z >= minB.z && minA.z <= maxB.z;
    }
    
    int IntersectsToPlane(Plane p)
    {
        float sd = dot(center, p.normal) - p.d;
        float r  = dot(extents, abs(p.d));
        if (sd > r)
        {
            return PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        }
        if (sd < -r)
        {
            return PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        }
        
        return PLANE_INTERSECTION_INTERSECTING;
    }
    
    int FrustumCheck(Frustum f)
    {
        int p0 = IntersectsToPlane(f.Left);
        int p1 = IntersectsToPlane(f.Right);
        int p2 = IntersectsToPlane(f.Bottom);
        int p3 = IntersectsToPlane(f.Top);
        int p4 = IntersectsToPlane(f.Near);
        int p5 = IntersectsToPlane(f.Far);
        
        bool bAnyOutside = p0 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside |= p1 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside |= p2 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside |= p3 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside |= p4 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        bAnyOutside |= p5 == PLANE_INTERSECTION_NEGATIVE_HALFSPACE;
        
        bool bAllInside = p0 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside &= p1 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside &= p2 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside &= p3 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside &= p4 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;
        bAllInside &= p5 == PLANE_INTERSECTION_POSITIVE_HALFSPACE;

        if (bAnyOutside)
        {
            return FRUSTUM_CONTAINMENT_DISJOINT;
        }

        if (bAllInside)
        {
            return FRUSTUM_CONTAINMENT_CONTAINS;
        }
        
        return FRUSTUM_CONTAINMENT_INTERSECTS;
    }
};

#endif //_HLSL_MATH_HEADER