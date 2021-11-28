//----------------------------------------------------------
// 
// Common.hlsl is included by the other shaders, and defines 
// the ray payload HitInfo which will be used to communicate 
// information between shaders. It only contains a float4 
// vector representing the color at the hit point and the 
// distance from the ray origin to that hit point. This file 
// also declares the structure Attributes which will be used 
// to store the float2 barycentric coordinates returned by 
// the intersection shader.
//
//-----------------------------------------------------------

// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
  float4 colorAndDistance;
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
  float2 bary;
};
