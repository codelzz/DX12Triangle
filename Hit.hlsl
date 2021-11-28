//----------------------------------------------------------
//
// Hit.hlsl contains a very simple closest hit shader 
// ClosestHit(), with its semantic [shader("closesthit")]. 
// It will be executed upon hitting the geometry 
// (our triangle). As the miss shader, it takes the ray 
// payload payload as a inout parameter. It also has a 
// second parameter defining the intersection attributes as 
// provided by the intersection shader, ie. the barycentric 
// coordinates. This shader simply writes a constant color 
// to the payload, as well as the distance from the origin,
// provided by the built-in RayCurrentT() function.
//
//----------------------------------------------------------

#include "Common.hlsl"

struct STriVertex
{
    float3 vertex;
    float4 color;
};
StructuredBuffer<STriVertex> BTriVertex : register(t0);

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    // 我们使用内置的 PrimitiveIndex() 获取我们击中的三角形索引
    float3 barycentrics =
      float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    uint vertId = 3 * PrimitiveIndex();
    float3 hitColor = BTriVertex[vertId + 0].color * barycentrics.x +
                      BTriVertex[vertId + 1].color * barycentrics.y +
                      BTriVertex[vertId + 2].color * barycentrics.z;
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
    // payload.colorAndDistance = float4(1, 0, 0, RayTCurrent());
}
