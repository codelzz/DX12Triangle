//----------------------------------------------------------
// 
// RayGen.hlsl contains the ray generation program RayGen(), 
// flagged by its semantic [shader("raygeneration")] . It 
// also declares its access to the raytracing output buffer 
// gOutput bound as a unordered access view (UAV), and the 
// raytracing acceleration structure SceneBVH, bound as a 
// shader resource view (SRV). For now this shader program 
// simply writes a constant color in the raytracing output 
// buffer.
//
//-----------------------------------------------------------

#include "Common.hlsl"

// Raytracing output texture, accessed as a UAV
RWTexture2D< float4 > gOutput : register(u0);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t0);

// #DXR Extra: Perspective Camera
cbuffer CameraParams : register(b0)
{
    float4x4 view;          
    float4x4 projection;    
    float4x4 viewI;         /* View Invertion*/
    float4x4 projectionI;   /* Projection Invertion*/
}

[shader("raygeneration")] 
void RayGen() {
     
    HitInfo payload;
    payload.colorAndDistance = float4(0, 0, 0, 0);      // 初始化光线 payload

    // 获取被调度的 2D 网格工作项的位置(一般映射为像素，可表示像素坐标).
    uint2 launchIndex = DispatchRaysIndex();                        // 获取当前像素 2D 坐标
    float2 dims = float2(DispatchRaysDimensions().xy);              // 获取需要渲染的图片尺寸
    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);   // 至此，我们可以推断出归一化后浮点像素坐标 d
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // 已知默认相机位置为 (0,0,1) 并朝向 (0,0,-1), 我们可以设置一个 ray discriptor
    // RayDesc 代表通过将光线原点 x,y 坐标偏移归一化的浮点像素坐标来表示直接穿过
    // 每个像素的光线。(这里的基础光线代表相机朝向(0,0,1)->(0,0,-1))
    // [Question] 为什么像素平面位置等于相机位置？
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // 定义一束光线，包含原点，方向，和最大-最小距离值
    // # DXR Extra: Perspective Camera 
    float aspectRatio = dims.x / dims.y;
    RayDesc ray;
    ray.Origin = mul(viewI, float4(0, 0, 0, 1));
    float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
    ray.Direction = mul(viewI, float4(target.xyz, 0));
    ray.TMin = 0;
    ray.TMax = 100000;
    
    // Trace the ray
    TraceRay( 
        // Parameter name: AccelerationStructure 
        // Acceleration structure 
        SceneBVH, 
    
        // Parameter name: RayFlags 
        // 用于指定光线击中表面的行为 
        RAY_FLAG_NONE, 
    
        // Parameter name: InstanceInclusionMask 
        // Instance inclusion mask，可通过与 geometry mask 与运算遮盖 geometry，0xFF 表示不遮盖
        0xFF, 
        
        // Parameter name: RayContributionToHitGroupIndex 
        // Depending on the type of ray, a given object can have several hit groups attached 
        // (ie. what to do when hitting to compute regular shading, and what to do when hitting 
        // to compute shadows). Those hit groups are specified sequentially in the SBT, so the value 
        // below indicates which offset (on 4 bits) to apply to the hit groups for this ray. In this 
        // sample we only have one hit group per object, hence an offset of 0. 
        0, 
    
        // Parameter name: MultiplierForGeometryContributionToHitGroupIndex 
        // The offsets in the SBT can be computed from the object ID, its instance ID, but also simply 
        // by the order the objects have been pushed in the acceleration structure. This allows the 
        // application to group shaders in the SBT in the same order as they are added in the AS, in 
        // which case the value below represents the stride (4 bits representing the number of hit 
        // groups) between two consecutive objects. 
        0,
    
        // Parameter name: MissShaderIndex 
        // Index of the miss shader to use in case several consecutive miss shaders are present in the 
        // SBT. This allows to change the behavior of the program when no geometry have been hit, for
        // example one to return a sky color for regular rendering, and another returning a full
        // visibility value for shadow rays. This sample has only one miss shader, hence an index 0 
        0,
        
        // Parameter name: Ray 
        // Ray information to trace 
        ray, 
        
        // Parameter name: Payload 
        // Payload associated to the ray, which will be used to communicate between the hit/miss 
        // shaders and the raygen 
        payload);
    gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.f);
}
