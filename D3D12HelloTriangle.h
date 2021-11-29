//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// DX12 Raytracing Basic - Part 1 
//	https://developer.nvidia.com/rtx/raytracing/dxr/dx12-raytracing-tutorial-part-1
// DX12 Raytracing Basic - Part 2
//  https://developer.nvidia.com/rtx/raytracing/dxr/dx12-raytracing-tutorial-part-2
// DX12 Raytracing Extras - Perspective Camera 
//	https://developer.nvidia.com/rtx/raytracing/dxr/dx12-raytracing-tutorial/extra/dxr_tutorial_extra_perspective
//*********************************************************

#pragma once

#include "DXSample.h"

//---raytracing header---
#include <dxcapi.h>
#include <vector>

// ## Acceleration Struceture
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"

// ## Shader Binding Table
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

// ## Raytracing pipeline
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
//-----------------------

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloTriangle : public DXSample
{
public:
	D3D12HelloTriangle(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

private:
	static const UINT FrameCount = 2;

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
	};

	// Pipeline objects.
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device5> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
	UINT m_rtvDescriptorSize;

	// App resources.
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();

	// ----------------------------------------------------------------------------------
	// # DXR
	void CheckRaytracingSupport();

	virtual void OnKeyUp(UINT8 key);
	bool m_raster = true;

	// ----------------------------------------------------------------------------------
	// # DXR Acceleration Structure
	struct AccelerationStructureBuffers {
		ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
		ComPtr<ID3D12Resource> pResult;       // Where the AS is
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};

	// example only use a single bottom-level AS, for which we only store the pResult buffer
	ComPtr<ID3D12Resource> m_bottomLevelAS; // Storage for the bottom Level AS

	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
	AccelerationStructureBuffers m_topLevelASBuffers;
	std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

	/// Create the acceleration structure of an instance
	/// \param     vVertexBuffers : pair of buffer and vertex count
	/// \return    AccelerationStructureBuffers for TLAS
	AccelerationStructureBuffers CreateBottomLevelAS(
		std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers);

	/// Create the main acceleration structure that holds all instances of the scene
	/// \param     instances : pair of BLAS and transform
	void CreateTopLevelAS(
		const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>
		& instances);

	/// Create all acceleration structures, bottom and top
	void CreateAccelerationStructures();

	// ----------------------------------------------------------------------------------
	// # DXR - Raytracing Pipeline
	ComPtr<ID3D12RootSignature> CreateRayGenSignature();
	ComPtr<ID3D12RootSignature> CreateMissSignature();
	ComPtr<ID3D12RootSignature> CreateHitSignature();

	void CreateRaytracingPipeline();

	ComPtr<IDxcBlob> m_rayGenLibrary;
	ComPtr<IDxcBlob> m_hitLibrary;
	ComPtr<IDxcBlob> m_missLibrary;

	ComPtr<ID3D12RootSignature> m_rayGenSignature;
	ComPtr<ID3D12RootSignature> m_hitSignature;
	ComPtr<ID3D12RootSignature> m_missSignature;

	// Ray tracing pipeline state
	ComPtr<ID3D12StateObject> m_rtStateObject;
	// Ray tracing pipeline state properties, retaining the shader identifiers
	// to use in the Shader Binding Table
	ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

	// ----------------------------------------------------------------------------------
	// # DXR - Raytracing Resources
	void CreateRaytracingOutputBuffer();
	void CreateShaderResourceHeap();
	ComPtr<ID3D12Resource> m_outputResource;
	ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
	/*Unorderd access view (UAV)*/

	// ----------------------------------------------------------------------------------
	// # DXR - Shader binding table (SBT)
	void CreateShaderBindingTable();
	nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
	ComPtr<ID3D12Resource> m_sbtStorage;

	// ----------------------------------------------------------------------------------
	// # DXR Extra: Perspective Camera 
	// 
	// 要引入 Perspective Camera, camera matrices 需要通过常量缓冲 m_cameraBuffer 传递到 
	// shader 中。为了在光栅管线中使用，我们给被引用的 camera 创建堆 m_constHeap 
	void CreateCameraBuffer();
	void UpdateCameraBuffer();
	ComPtr<ID3D12DescriptorHeap > m_constHeap;	// rasterization
	ComPtr<ID3D12Resource > m_cameraBuffer;		// raytracing
	uint32_t m_cameraBufferSize = 0;
	// #DXR Extra: Perspective Camera ++ 
	void OnButtonDown(UINT32 lParam); 
	void OnMouseMove(UINT8 wParam, UINT32 lParam);
};
