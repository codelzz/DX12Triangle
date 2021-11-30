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

/*
// #DXR Extra: Per-Instance Data
cbuffer Colors : register(b0)
{
    float3 A[3];
    float3 B[3];
    float3 C[3];
}
*/

// c++Դ���У����Ƕ�ÿ��ʵ��ʹ�ö����ĳ�������ò��ִ��뽫������
// #DXR Extra: Per-Instance Data
struct StructColor
{
    float4 a;
    float4 b;
    float4 c;
};
cbuffer Colors : register(b0)
{
    StructColor Tint[3];
}

/*
// #DXR Extra: Per-Instance Data
cbuffer Colors : register(b0)
{
    float3 A;
    float3 B;
    float3 C;
}*/

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
    // ����ʹ�����õ� PrimitiveIndex() ��ȡ���ǻ��е�����������
    float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    uint vertId = 3 * PrimitiveIndex();
    // #DXR Extra: Per-Instance Data
    // ���� DXR �ṩ������ InstanceID() �������ڷ��������� CreateTopLevelAS 
    // �� AddInstance �������������

    // #DXR Extra: Per-Instance Data
    float3 hitColor = float3(0.6, 0.7, 0.6);
    // Shade only the first 3 instances (triangles)
    int instanceID = InstanceID();
    // #DXR Extra: Per-Instance Data
    // float3 hitColor = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;
    
    if (instanceID < 3)
    {
        // #DXR Extra: Per-Instance Data
        hitColor = Tint[instanceID].a * barycentrics.x + Tint[instanceID].b * barycentrics.y + Tint[instanceID].c * barycentrics.z;
    }
    /*
    if (InstanceID() < 3)
    {
        // #DXR Extra: Per-Instance Data
        // hitColor = BTriVertex[vertId + 0].color * barycentrics.x + BTriVertex[vertId + 1].color * barycentrics.y + BTriVertex[vertId + 2].color * barycentrics.z;
        hitColor = A[InstanceID()] * barycentrics.x + B[InstanceID()] * barycentrics.y + C[InstanceID()] * barycentrics.z;
    }*/
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}
