//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloTriangle.h"
#include <stdexcept>

// # DXR - Raytracing
#include "DXRHelper.h"
// # DXR - Shader Binding Table
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"
// # DXR - Raytracing pipeline
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
// # DXR Extras - Perspective camera
#include "glm/gtc/type_ptr.hpp" 
#include "manipulator.h" 

#include "Windowsx.h"
//------------------------

D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_rtvDescriptorSize(0)
{
}

void D3D12HelloTriangle::OnInit()
{
	// # DXR Extras - Perspective Camera
	nv_helpers_dx12::CameraManip.setWindowSize(GetWidth(), GetHeight()); 
	nv_helpers_dx12::CameraManip.setLookat(glm::vec3(1.5f, 1.5f, 1.5f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

	LoadPipeline();
	LoadAssets();

	//--- ray tracing ---
	// 检查设备是否支持 raytracing 
	CheckRaytracingSupport(); 

	// # DXR - Acceleration Structure
	// 给 raytracing 配置 acceleration structures (AS). When setting up 
	// geometry, each bottom-level AS has its own transform matrix. 
	CreateAccelerationStructures(); 

	// Command lists are created in the recording state, but there is 
	// nothing to record yet. The main loop expects it to be closed, so 
	// close it now. 
	ThrowIfFailed(m_commandList->Close());
	
	// # DXR - Raytracing Pipeline 
	// 
	// Create the raytracing pipeline, associating the shader code to symbol names
	// and to their root signatures, and defining the amount of memory carried by
	// rays (ray payload)
	CreateRaytracingPipeline();

	// #DXR Extra: Per-Instance Data
	// 创建实例常量缓冲
	CreatePerInstanceConstantBuffers();

	// #DXR Extra: Per-Instance Data
	// 创建全局常量缓冲，其包含每个三角形实例中每个顶点颜色
	CreateGlobalConstantBuffer();

	// ## Raytracing Resource
	// 创建存储光追输出缓冲，其维度与目标image一致
	CreateRaytracingOutputBuffer();

	// # DXR Extra - Perspective Camera
	// 创建用于存储 modelview 和 perspective camera matrices 的缓冲
	CreateCameraBuffer();

	// Create the buffer containing the raytracing result (always output in a
	// UAV), and create the heap referencing the resources used by the raytracing,
	// such as the acceleration structure
	CreateShaderResourceHeap();

	// ## Shader Bingding Table
	// Create the shader binding table and indicating which shaders
    // are invoked for each instance in the AS
	CreateShaderBindingTable();
}

// Load the rendering pipeline dependencies.
void D3D12HelloTriangle::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_12_1,
			IID_PPV_ARGS(&m_device)
			));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
			));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),	// Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
		));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}

	ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

	// #DXR Extra: Depth Buffering
	// The original sample does not support depth buffering, so we need to allocate a depth buffer,
	// and later bind it before rasterization
	CreateDepthBuffer();
}

// Load the sample assets.
void D3D12HelloTriangle::LoadAssets()
{
	// Create an empty root signature.
	{
		// #DXR Extra: Perspective Camera
		// root signature 描述那些数据可被 shader 访问。camera matrices 被保持在 constant buffer，其本身引用堆。
		// 为此，我们引用了堆的一个范围，并将这个范围作为 shader 的唯一参数。camera buffer 与索引0关联，并使其在
		// b0 register 中的 shader 可访问。
		CD3DX12_ROOT_PARAMETER constantParameter;
		CD3DX12_DESCRIPTOR_RANGE range;
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		constantParameter.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(1, &constantParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		// #DXR Extra: Depth Buffering
		// Add support for depth testing, using a 32-bit floating-point depth buffer
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
	}

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

	// 创建顶点缓冲
	{
		// CreateTriangleVB();	// 创建三角形顶点缓冲
		//---DXR Extra: Indexed Geometry
		CreateTetrahedronVB();
		// #DXR - Per Instance
		CreatePlaneVB();    // 创建地平面顶点缓冲, 与上述三角形缓冲类似
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForPreviousFrame();
	}
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
	// #DXR Extra: Perspective Camera 
	// 在每一帧后我们需要对应更新 camera matrix
	UpdateCameraBuffer();
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(1, 0));

	WaitForPreviousFrame();
}

void D3D12HelloTriangle::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForPreviousFrame();

	CloseHandle(m_fenceEvent);
}

void D3D12HelloTriangle::PopulateCommandList() {
	// Command list allocators 只能在其关联的 command lists 完成 GPU 上的执行后重置；
	// 程序应使用 fence 来确定 GPU 执行进度。
	ThrowIfFailed(m_commandAllocator->Reset());

	// 然而，ExecuteCommandList() 在特定的 command list 被调用时，command list 可以在
	// re-recording 之前的任意时候重置
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	// 配置必要状态.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// 指示 backbuffer 将会作为渲染目标被使用
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	// #DXR Extra: Depth Buffering
	// Bind the depth buffer as a render target
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	//m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);


	// 记录命令.
	if (m_raster)
	{
		// #DXR Extra: Depth Buffering 
		m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		// #DXR Extra: Perspective Camera 
		std::vector<ID3D12DescriptorHeap*> heaps = { m_constHeap.Get() }; 
		m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data()); 
		// set the root descriptor table 0 to the constant buffer descriptor heap 
		m_commandList->SetGraphicsRootDescriptorTable( 0, m_constHeap->GetGPUDescriptorHandleForHeapStart());

		// 仅在光栅化下执行
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		// m_commandList->DrawInstanced(3, 1, 0, 0);

		// #DXR Extra: Indexed Geometry
		// 绘制四面体
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		m_commandList->IASetIndexBuffer(&m_indexBufferView);
		m_commandList->DrawIndexedInstanced(12, 1, 0, 0, 0);

		// #DXR Extra: Per-Instance Data
		// 与光追相似，可视化地平面
		m_commandList->IASetVertexBuffers(0, 1, &m_planeBufferView);
		m_commandList->DrawInstanced(6, 1, 0, 0);
	}
	else 
	{
		// 在光追中, 我们用不一样的颜色清理 buffer
		const float clearColor[] = { 0.6f,0.8f,0.4f,1.0f };
		m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		// 绑定 descriptor heap 以访问 TLAS 和 光追输出
		std::vector<ID3D12DescriptorHeap*> heaps = { m_srvUavHeap.Get() };
		m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
		// 在最后一帧，光追输出将会作为复制源使用，来复制其内容到渲染目标。现在我们需要将其转化为
		// UAV以便 shader 可以写入
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		m_commandList->ResourceBarrier(1, &transition);

		// 配置光追任务
		D3D12_DISPATCH_RAYS_DESC desc = {};

		// SBT 结构如下：ray generation, miss shaders, hit groups.
		// The layout of the SBT is as follows: ray generation shader, miss
		// shaders, hit groups. 所有 SBT 项都具有相同的大小以允许固定 stride.
		
		// Ray generation shader 总是处于SBT的开头
		uint32_t rayGenerationSectionSizeInBytes = m_sbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

		// Miss shaders 在 SBT的第二部分，紧跟踪 generation shader。我们有一个 miss shader 用于
		// camera rays 即另一个用于 shadow rays. 因此，这个部分需要 2*m_sbtEntrySize 大小。同时
		// 我们指明了两个 miss shaders 间的 stride，其等于一个 SBT entry。
		uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
		desc.MissShaderTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
		desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
		desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

		// Hit groups 部分在 miss shaders 后开始. 在这个例子中我们有一个 hit group 用于 triangle
		uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
		desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes + missSectionSizeInBytes;
		desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
		desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();

		// 渲染图片的维度, 等同于内核启动维度
		desc.Width = GetWidth();
		desc.Height = GetHeight();
		desc.Depth = 1;

		// 绑定光追管线
		m_commandList->SetPipelineState1(m_rtStateObject.Get());
		// 启动光追并输出结果
		m_commandList->DispatchRays(&desc);

		// 光追输出需要被复制到实际用于显示的渲染目标。为此，我们需要将光追输出从一个 UAV 转化
		// 到一个复制源，从渲染目标缓冲到复制终点。我们剋在转化渲染目标缓冲到渲染目标前执行复制。
		transition = CD3DX12_RESOURCE_BARRIER::Transition(m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_commandList->ResourceBarrier(1, &transition);
		transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
		m_commandList->ResourceBarrier(1, &transition);
		m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_outputResource.Get());
		transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &transition);
	}

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTriangle::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

//----- Raytracing -------
// ## commom
void D3D12HelloTriangle::CheckRaytracingSupport()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));
	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) throw std::runtime_error("Raytracing not supported on device");
}

void D3D12HelloTriangle::OnKeyUp(UINT8 key)
{
	// Alternate between rasterization and raytracing using the space bar
	if (key == VK_SPACE)
	{
		m_raster = !m_raster;
	}
}

// ## Acceleration Structure
//-----------------------------------------------------------------------------
// 
// Create a bottom-level acceleration structure based on a list of vertex
// buffers in GPU memory along with their vertex count. The build is then done
// in 3 steps: 
// 1. gathering the geometry
// 2. computing the sizes of the required buffers
// 3. building the actual AS
// 
// To create the bottom level acceleration structure (BLAS), we are calling CreateBottomLevelAS 
// and passing an array of two elements (pair):
// * the pointer to the resource holding the vertices of the geometry
// * the number of vertices 
// 
// Note that we are assuming that the resource contains Vertex structures. For the sake of 
// simplicity, we do not use indexing: triangles are described by 'Vertex' triplets. 
// 
// The function CreateBottomLevelAS is divided into 3 main steps. 
// 1. it combines all the vertex buffers into the BLAS builder helper class. The BLAS generation 
//    is performed on GPU, 
// 2. it computes the storage requirements to hold the final BLAS as well as some temporary space 
//    by calling ComputeASBufferSizes. This maps to the actual DXR API, which requires the application 
//    to allocate the space for the BLAS as well as the temporary (scratch) space. This scratch space 
//    can be freed as soon as the build is complete, ie. after the execution of the command list containing
//    the build request is completed. Internally, the ComputeASBufferSizes method calls 
//    ID3D12Device5::GetRaytracingAccelerationStructurePrebuildInfo which will give a conservative estimate 
//    of the memory requirements. The buffers can then be allocated directly in GPU memory on the default heap.
// 3. the BLAS can be generated by calling the Generate method. It will create a descriptor of the acceleration 
//    structure building work with a D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL type, and call 
//    ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure() with that descriptor. The resulting
//    BLAS contains the full definition of the geometry, organized in a way suitable for efficiently finding
//    ray intersections with that geometry.
//
// D3D12HelloTriangle::AccelerationStructureBuffers D3D12HelloTriangle::CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers)
// {
// # DXR Extra: Indexed Geometry
// 改写该方法使其支持索引缓冲
// #DXR Extra: Indexed Geometry
D3D12HelloTriangle::AccelerationStructureBuffers D3D12HelloTriangle::CreateBottomLevelAS(
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers, 
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers) {

	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS; 
	// Adding all vertex buffers and not transforming their position. 
	for (size_t i = 0; i < vVertexBuffers.size(); i++)
	{ // 
		for (const auto& buffer : vVertexBuffers) {
			if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
				bottomLevelAS.AddVertexBuffer(
					vVertexBuffers[i].first.Get(),
					0,
					vVertexBuffers[i].second,
					sizeof(Vertex),
					vIndexBuffers[i].first.Get(),
					0,
					vIndexBuffers[i].second,
					nullptr, 0, true);
			else
				bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0, vVertexBuffers[i].second, sizeof(Vertex), 0, 0);
		}
	}
	/*
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;
	// Adding all vertex buffers and not transforming their position.
	for (const auto& buffer : vVertexBuffers) { 
		bottomLevelAS.AddVertexBuffer(buffer.first.Get(), 0, buffer.second, sizeof(Vertex), 0, 0); 
	}*/
	// The AS build requires some scratch space to store temporary information.
	// The amount of scratch memory is dependent on the scene complexity.
	UINT64 scratchSizeInBytes = 0;
	// The final AS also needs to be stored in addition to the existing vertex buffers.
	// It size is also dependent on the scene complexity.
	UINT64 resultSizeInBytes = 0;
	bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes);
	// Once the sizes are obtained, the application is responsible for allocating 
	// the necessary buffers. Since the entire generation will be done on the GPU, 
	// we can directly allocate those on the default heap
	AccelerationStructureBuffers buffers;
	buffers.pScratch = nv_helpers_dx12::CreateBuffer(m_device.Get(), scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, nv_helpers_dx12::kDefaultHeapProps); 
	buffers.pResult = nv_helpers_dx12::CreateBuffer(m_device.Get(), resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps); 
	// Build the acceleration structure. Note that this call integrates a barrier 
	// on the generated AS, so that it can be used to compute a top-level AS right 
	// after this method. 
	bottomLevelAS.Generate(m_commandList.Get(), buffers.pScratch.Get(), buffers.pResult.Get(), false, nullptr);
	return buffers;
}

//---CreateTopLevelAS-----------------------------------------------------------
// 
// 创建一个主要加速结构来持有场景中所有的实例。与 BLAS 的生成类似，其通过三个步骤完成：
// 1. 获取实例
// 2. 计算加速结构所需要的内存
// 3. 构造加速结构
// 
// TLAS 可以被看成在加速结构上的加速结构，其致力于优化光线与任意底层 BLAS 交点的查询。一个
// TLAS 可以反复实例化同一个 BLAS 多次，并利用 per-instance matrices 在不同的世界空间位置
// 来渲染他们。
// 
// 在本例中，我们调用 CreateTopLevelAS 来传递一个两个元素的数组：
// * 一个指向 BLAS 的资源指针
// * object 的 position matrix
// 
// 然而，TLAS 需要额外的缓冲来持有每个实例的 description。ComputeASBufferSizes 方法通过调
// 用 ID3D12Device5::GetRaytracingAccelerationStructurePrebuildInfo 提供了临时和结果缓
// 冲的大小，并从 instance descriptor D3D12_RAYTRACING_INSTANCE_DESC 中计算出 instance
// buffer 大小和 instance 数量。
// 
// 与 BLAS 相似，临时和结果缓冲都被直接分配到 GPU 内存中，在默认堆里。Instance descriptors buffer
// 需要在 helper 中完成映射，并需要分配与 upload 堆。一旦完成缓冲分配，生成call 使用 
// D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL 类型填入 instance descriptions buffer 
// 和所需要完成构建的工作的 descriptor。该 descriptor 将被传递到 
// ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure 来构建持有所有实例的
// 加速结构。
//
void D3D12HelloTriangle::CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances) { 
	// 1. 获取所有实例到 m_topLevelASGenerator 中
	// # DXR - per isntance data
	// 现在实例都是相互独立，我们需要将实例与其自己在SBT中的 hit 相关联。
	for (size_t i = 0; i < instances.size(); i++) 
	{ 
		m_topLevelASGenerator.AddInstance(
			instances[i].first.Get(), 
			instances[i].second, 
			static_cast<UINT>(i)   /* 实例ID，可通过DXR内置方法InstanceID()在hlsl中获取该值 */,
			static_cast<UINT>(i)); /* 这里我们需要将实例与其自己在SBT中的Hit group关联，
								      这样 i-th 三角形将会调用第一个在SBT中定义的hitgroup，
									  其自生引用 m_perInstanceConstantBuffers[i] */
	} 

	// 与 BLAS 类似，构造加速结构需要一些临时空间(scratch space)来存储临时数据以创建实际的加速结构。
	// 在 TLAS 情景下，instance descriptor 同样需要被存储到 GPU 内存中。该方法输出了每个 (scratch
	// ,result, instance descriptor) 的内存需求，这样程序可以分配相应的内存。
	UINT64 scratchSize, resultSize, instanceDescsSize; 
	m_topLevelASGenerator.ComputeASBufferSizes(
		m_device.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);
	// 创建临时和结果缓冲。由于构造在GPU中已完成，这些可以直接分配在默认堆中。
	m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), 
		scratchSize, 
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
		nv_helpers_dx12::kDefaultHeapProps);
	m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer( 
		m_device.Get(), 
		resultSize, 
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
		nv_helpers_dx12::kDefaultHeapProps); 
	// The buffer describing the instances: ID, shader binding information, 
	// matrices ... Those will be copied into the buffer by the helper through 
	// mapping, so the buffer has to be allocated on the upload heap.
	m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer( m_device.Get(), 
		instanceDescsSize, 
		D3D12_RESOURCE_FLAG_NONE, 
		D3D12_RESOURCE_STATE_GENERIC_READ, 
		nv_helpers_dx12::kUploadHeapProps); 
	// After all the buffers are allocated, or if only an update is required, we 
	// can build the acceleration structure. Note that in the case of the update 
	// we also pass the existing AS as the 'previous' AS, so that it can be 
	// refitted in place. 
	m_topLevelASGenerator.Generate(
		m_commandList.Get(), 
		m_topLevelASBuffers.pScratch.Get(), 
		m_topLevelASBuffers.pResult.Get(), 
		m_topLevelASBuffers.pInstanceDesc.Get());
}

//---CreateAccelerationStructures----------------------------------------------------------
// 
// 结合 Bottom Level Acceleration Structure (BLAS) 和 Top-Level Acceleration Structure (TLAS)
// 来构建光追场景所需要的完整加速结构 (AS)
// 
// 该方法调用 AS builds 来构造 BLAS 和 TLAS 并且保存所生成的结构。虽然我们仅保留三角形的 BLAS 结构
// 并舍弃暂存空间(scratch space) ，我们保存了所有 TLAS 的缓冲到 m_topLevelASBuffers 预期处理动态
// 场景，其中暂存空间将被反复使用。
// 
// 该方法首先用构建 BLAS 的命令填充 command list。对于每个 BLAS，helper 引入了一个资源屏障
// D3D12_RESOURCE_BARRIER_TYPE_UAV 来确保 BLAS 可以在同个 command list 被请求。正如 TLAS 同样在
// 该 command list 中，这是必须的。F
// 
// 在请求 AS build 后，我们通过 ExecuteCommandLists 立即执行了 command list，并使用了一个 fence
// 在开始渲染前刷新 command list。
void D3D12HelloTriangle::CreateAccelerationStructures() { 
	// 从 triangle vertex buffer 构造 BLAS 
	//	AccelerationStructureBuffers bottomLevelBuffers = CreateBottomLevelAS({{m_vertexBuffer.Get(), 3}}); 
	// 构造四面体BLAS
	AccelerationStructureBuffers bottomLevelBuffers = CreateBottomLevelAS({{m_vertexBuffer.Get(), 4} }, { {m_indexBuffer.Get(), 12} });

	// 从 plan vertex buffer 构造 BLAS
	AccelerationStructureBuffers planeBottomLevelBuffers = CreateBottomLevelAS({ {m_planeBuffer.Get(), 6} });

	m_instances = { 
		// # DXR Extra：三个三角形实例
		{bottomLevelBuffers.pResult, XMMatrixIdentity()}, 
		// DXR Extra: Index Geometry
		// {bottomLevelBuffers.pResult, XMMatrixTranslation(.6f, 0, 0)}, 
		// {bottomLevelBuffers.pResult, XMMatrixTranslation(-.6f, 0, 0)}, 
		// # DXR Extra：一个平面实例 
		{planeBottomLevelBuffers.pResult, XMMatrixTranslation(0, 0, 0)}
	};
	CreateTopLevelAS(m_instances); 

	// 刷新 command list 并等待完成 
	m_commandList->Close();
	ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
	m_fenceValue++;
	m_commandQueue->Signal(m_fence.Get(), m_fenceValue);

	m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
	WaitForSingleObject(m_fenceEvent, INFINITE);

	// 当 command list 完成执行，将其重置用于渲染
	ThrowIfFailed( m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get())); 
	// 保存 BLAS 缓冲。剩余的缓冲将会在退出方程后释放
	m_bottomLevelAS = bottomLevelBuffers.pResult;
}

// ## Raytracing Pipeline

//---CreateRayGenSignature----------------------------------------------------
// 
// ray generation shader 需要访问两种资源: 
//	* the raytracing output
//	* the top-level acceleration structure (TLAS)
// 
// RayGen progame 中的 root signature 表明程序需要访问的 image out 和包含了 
// Top-Level AS 的 Buffer，简单起见，这里所引入的 root signature 使用 
// RootSignatureGenerator helper。 Add* 方法本质上给每个 entry 创建了 
// D3D12_ROOT_PARAMETER descriptors，同时 Generate 的调用将 descriptors 结合到 
// D3D12_ROOT_SIGNATURE_DESC, 其本身用于调用 D3D12SerializeRootSignature
// 和 ID3D12Device::CreateRootSignature
//
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateRayGenSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddHeapRangesParameter(
		{{0 /*u0*/, 1 /*1 descriptor */, 0 /*use the implicit register space 0*/, D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/, 0 /*heap slot where the UAV is defined*/}, 
		 {0 /*t0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV /* TLAS */, 1}, 
		 {0 /*b0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV /*Camera parameters*/, 2} 
		});
	return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// hit shader 仅通过 ray payload 通讯，不需要任何资源。当 ray 与 三角形相交时, 我们
// 简单地在 payload 中返回颜色
//
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateHitSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	// 这里创建顺序不对会导致代码异常

	// 为了访问 vertex buffer 我们需要告诉 Hit Root Signature 我们将会使用 shader resource view。
	// 默认情况下，它会与shader中的 register(t0) 绑定
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0 /*t0*/);
	// DXR Extra: Indexed Geometry
	// vertices and colors 
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1 /*t1*/); // indices

	// #DXR Extra: Per-Instance Data
	// 我们这里修改 hit shader 的 root signature 来将常量缓冲作为 root parameter 传递
	// 与在堆中传递的缓冲相反，root parameter 可以对每个实例传递。由于这是我们第一个在
	// root signature 声明的常量缓冲，我们将其与 register 0 绑定，它将会在 HLSL 代码中
	// 作为 register(0) 访问。
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0 /*b0*/);
	return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// Miss shader 仅通过 ray payload 通讯，不需要任何资源。Miss shader 的 root signature
// 为空因为其只通过 payload 通信。
//
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateMissSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc; 
	return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// Raytracing pipeline 用一个 structure 绑定了 shader code，root signatures 和 
// pipeline characteristic。该 structure 被用于在光追计算中调用 shaders 和管理临时
// 内存
// 
void D3D12HelloTriangle::CreateRaytracingPipeline() {
	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());

	// pipeline 包含了所有可能在 raytracing 过程中 shader 的 DXIL 代码。这里代码将 HLSL
	// 代码编译成 DXIL 库集。为了更加清晰，我们选择将代码通过语义 (ray generation，hit，
	// miss) 分离成多干个库
	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Miss.hlsl");
	m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Hit.hlsl");

	// 与 DLL 类似，每个库若干个 exported symbols 关联。这需要显式地完成。值得注意的时一个
	// 库可以包含任意数量的 symbols，其 semantic 在 HLSL 中使用 [shader("xxx")] 给出
	// [Question] 为什么在 HLSL 中symbols为小写且不匹配 C++ symbols
	pipeline.AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_missLibrary.Get(), { L"Miss" });
	// pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit" });
	// #DXR Extra: Per-Instance Data
	pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit", L"PlaneClosestHit" });

	// 要能够使用这些 shader，每个 DX12 shader 需要一个 root signature 定义所需要访问的
	// parameters 和 buffers
	m_rayGenSignature = CreateRayGenSignature();
	m_missSignature = CreateMissSignature();
	m_hitSignature = CreateHitSignature();

	// 三种不同的 shader 可以被调用来获得交点
	// 1. interscation shader 在 non-triangular geometry 的 bounding box 被击中时被调用
	// 2. any-hit shader 在存在潜在交点时被调用。该shader可用于alpha-testing 来丢弃某些交点
	// 3. closest-hit shader 在出现距离 ray origin 最近交点时被调用
	// 以上三种 shader 都被绑定到同一个 hit group中。

	// 对于 triangular geometry 的 intersection shader 是内置的。 一个空 any-hit 
	// 同样是默认定义的。因此这里 hit group 只包含了 closest hit shader。由于上述定义的 
	// exported symbols 可以通过名字引用。
	
	// 对于 triangles 的 Hit group，shader 只需要插入 vertex colors 
	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
	// #DXR Extra: Per-Instance Data
	pipeline.AddHitGroup(L"PlaneHitGroup", L"PlaneClosestHit");

	// 这里我们将 root signature 与每个 shader 关联。我们可以 explicity 展示一些 shader 共享一些 root signature
	// (如. Miss and ShadowMiss)。Hit shaders 仅指代 Hit group, 这意味着 底层交点，any-hit 和 closest-hit shaders
	// 共享同一个 root signature
	pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(), { L"Miss" });
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), { L"HitGroup" });
	// #DXR Extra: Per-Instance Data
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), { L"HitGroup", L"PlaneHitGroup" });

	// payload size 定义了 rays 所能携带的最大数据量，即，shaders间交换的数据，如 HLSL 代码中 HitInfo
	// structure。将该值保持在最低尤为关键，否者会产生过高的内存消耗和缓存垃圾。
	pipeline.SetMaxPayloadSize(4 * sizeof(float));	// RGB + distance 

	// ray 击中表面时，DXR 可以给 hit 提供一些属性。在该例子中，我们只使用了 triangle 最后两个顶点权重 (u,v) 所定义的
	// barycentric coordinates, 实际的 barycentric coordinates 可以通过 
	// float3 barycentrics = float3(1.f-u-v, u, v); 获得
	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates 

	// raytracing 过程可以在已存在的 hit points 投射 rays 形成嵌套调用。这里我们只跟踪 primary rays, 其要求深度为 1。
	// 递归深度需要保持在最低值来得到最好新能。Path tracing algorithms 可以轻易地在 ray generation 扁平化为一个简单循环。
	pipeline.SetMaxRecursionDepth(1);
	
	// 编译 pipeline 用于在GPU执行
	m_rtStateObject = pipeline.Generate(); 

	// 将 state object 转化为 properties object，允许稍后按名称访问 shader pointers
	ThrowIfFailed(m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps)));
}

//-----------------------------------------------------------------------------
// 分配光追输出 buffer，大小与输出图像一致 
//
void D3D12HelloTriangle::CreateRaytracingOutputBuffer() {
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	// backbuffer 本质是 DXGI_FORMAT_R8G8B8A8_UNORM_SRGB 但 sRGB 格式无法用于 UAVs
	// 为了准确度，我们需要在 shader 中自己转化为 sRGB
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = GetWidth();
	resDesc.Height = GetHeight();
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	ThrowIfFailed(m_device->CreateCommittedResource(
		&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
		IID_PPV_ARGS(&m_outputResource)));
}

//-----------------------------------------------------------------------------
// 创建一个主 heap 给 shaders 使用。该 heap 会提供给 raytracing 和 TLAS 访问
// 
// 所有 shader 可访问的数据通常在渲染前引用在 heap bound。这个 heap 包含预定义的 
// slots (插槽)，每个 slots 包含一个在 GPU 内存中 object 的 view。实际情况下，这个
// heap 是一个包含了 views on common resources 的内存区。该 view 会被
//  ID3D12Device::Create*View 直接写入 heap memory。这里 heap 只包含两项
// 
// 1. 作为 UAV 访问的 raytracing buffer
// 2. 作为 带有特定维度标志 (D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE) 
//    的 shader resource (SRV) 的 TLAS
//
void D3D12HelloTriangle::CreateShaderResourceHeap() {
	// 创建一个 SRV/UAV/CBV descriptor heap. 我们需要三项
	// 1. UAV for the raytracing output 
	// 2. SRV for the TLAS
	// 3. CBV for the camera matrices (#DXR Extra: Perspective Camera)
	m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(m_device.Get(), 3, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	// 在 CPU 端换取 heap memory 的 handle，用于直接写 descriptors
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

	// 创建 UAV。基于我们所创建的 root signature，这是第一项。 Create*View  方法将 view 信息直接写入 srvHandle
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc,srvHandle);

	// 紧接着加入 TLAS SRV
	srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
	// 将 AS View 写入 Heap
	m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

	// #DXR Extra: Perspective Camera
	// 在 TLAS 后给加入 camera constant buffer
	srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// 为相机描述并创建 constant buffer view 
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_cameraBufferSize;
	m_device->CreateConstantBufferView(&cbvDesc, srvHandle);
}

//---CreateShaderBindingTable---------------------------------------------------
//
// 着色器绑定表（shader binding table, SBT）是光追设置的基石：它将几何实例绑定到他们对
// 应的 hit groups，并根据 root signatures 将资源绑定到光追着色器程序。这里，我们有一个
// 包含单一三角形的场景(DXR - Part 2)。SBT 有三个entries:
//	1. for ray generation program
//  2. for miss program
//  3. for hit group 
// 
// ray generation 需要访问两个外部资源：
//  1. 光追输出缓冲 (raytracing output buffer)
//  2. 顶层加速结构 (TLAS)
// 
// ray generation shader 的 root signature 需要这两个资源在当前 bound heap 中可用。
// 所以，着色器需要有一个指针指向 heap 的开头。Hit group 和 miss program 不需要任何
// 的外部数据，因此他们的 root signature 为空。SBT 现在的布局如下
//			----------------
//			| Raygen |*
//			| Indentifier |O
//			----------------
//			| Heap Start  |O
//			| Pointer |*
//			----------------
//			| Miss |*
//			| Indentifier |O
//			----------------
//			| HitGroup |O
//			| Identifier |O
//			----------------
// 当光追开始时，ray generation program 的 identifer 将会被用于给每个像素执行他的
// entry point (对应HLSL中的方法 [shader("raygeneration")] void RayGen())。指向
// heap 的 pointer 将会使得 shader 找到需要的资源。
// 
// 当 ray generation 程序发出一束光线，heap pointer 会被用于找出 GPU 内存中 TLAS
// 的位置并触发追踪。
// 
// 光线也许会错过所有的集合体，这种情况下 SBT 将会被用于找出 miss shader identifier 
// 并执行对应代码。
// 
// 如果光线击中几何体，hit group identifier 将会被用于找到 shader 对应的 hit group:
// * Intersection
// * Any hit
// * Closes hit.
// 按照顺序，这些 shader 将会被执行并将结果发送的 ray generation shader。ray 
// generation shader 可以从 heap 中访问光追输出缓冲并写入结果。如果场景中有多个物体
// 及不同的 hit groups，SBT 会包括所有的 hit groups 及他们的资源。
//
// 例如，我们有三个物体，每一个在 main heap 中访问相机数据。物体 0,1 可以由他们各自的
// 的纹理，而物体 2 不含纹理。SBT 将会有如下结构:
//			------------------
//			| Raygen |*       
//			| Identifier |O   
//			------------------
//			| Heap Start |O   
//			| Pointer |*      
//			------------------
//			| HitGroup0 |O    
//			| Identifier|O    
//			------------------
//			| Heap Start |O	 
//			| Pointer |*	  
//			------------------
//			| Texture0 |O
//			| Pointer |O
//			-----------------
//			| HitGroup1 |O
//			| Identifier |O
//			------------------
//			| Heap Start |O   
//			| Pointer |*      
//			------------------
//			| Texture1 |O
//			| Pointer  |O
//			------------------
//			| HitGroup2 |O
//			| Identifer |O
//			------------------
//			| Heap Start |O
//			| Pointer  |*
//			-----------------
//			|//|O
//			||O
//			-----------------
//
// 需要注意 HitGroup2 不访问任何纹理。然而，SBT 对齐需求强制每种程序类型(ray generations, miss, hitgroup)
// 有固定的入口大小。给定程序类型的入口大小是由该类型中最大的 root signature 大小决定：
// * ray generation 为 1
// * miss 为 0
// * hit group 为 2
// 因此 SBT 入口被padding来遵循对其准则。
// 
// 许多实际情况下，raytracing 过程使用不同的光线类型。例如来区分常规射线和阴影射线。在这情况下，
// 对于每种物体类型，SBT将给每种光线类型包含一个hit group。回到单一对象的例子中，加入第二种
// 光线类型只需要将对应的 hit group 加入到 SBT 中
// 
//			----------------
//			| Raygen |*
//			| Indentifier |O
//			----------------
//			| Heap Start  |O
//			| Pointer |*
//			----------------
//			| Miss |*
//			| Indentifier |O
//			----------------
//			| HitGroup |O
//			| Identifier |O
//			----------------
//			| ShadowGroup |O
//			| Identifier |O
//			----------------
//
void D3D12HelloTriangle::CreateShaderBindingTable() {
	// SBT 帮助类包含对 Add* 程序的调用。如果被反复调用，帮助类必须得在重新加入着色器前清空。
	m_sbtHelper.Reset();

	// 指向堆开头的指针是没有根参数的着色器唯一需要的参数
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();

	// 帮助类将 root parameter pointers 和 heap pointers 都视为 void*，且 DX12 使用
	// D3D12_GPU_DESCRIPTOR_HANDLE 来定义 heap pointers. 在这个结构体的指针是 UINT64*
	// 我们需要将其 reinterpreted
	auto heapPointer = reinterpret_cast<UINT64*>(srvUavHeapHandle.ptr);

	// ray generation shader 只需要堆数据
	m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });

	// miss 和 hit shader 并不需要访问外部数据，他们是通过 ray payload 来通信
	m_sbtHelper.AddMissProgram(L"Miss", {});

	// 添加三角形碰撞 shader 
	// 这里需要将GPU内存中的这个缓冲地址传递给 Hit shader
	// 
	// #DXR Extra: Per-Instance Data
	// 我们有三个三角形，他们中的每个都需要访问其自己的常量缓冲作为root parameter在其primary hit shader。
	// shadow hit 只需要在payload中设置是否可见，因此不需要外部数据。
	// 需要注意的是，这里每个instance都绑定其对应的 Hit Group，因此我们需要在每个hitgroup中都持有 m_vertexBuffer

	m_sbtHelper.AddHitGroup(L"HitGroup", 
		{(void*)(m_vertexBuffer->GetGPUVirtualAddress()), 
		 (void*)(m_indexBuffer->GetGPUVirtualAddress()),
		 (void*)(m_perInstanceConstantBuffers[0]->GetGPUVirtualAddress())
		});

	/*
	for (int i = 0; i < 3; ++i) 
	{
		m_sbtHelper.AddHitGroup(
			L"HitGroup",
			{(void*)(m_vertexBuffer->GetGPUVirtualAddress()),
			 (void*)(m_globalConstantBuffer->GetGPUVirtualAddress()),
			 (void*)(m_perInstanceConstantBuffers[i]->GetGPUVirtualAddress())
			});
	}*/

	// #DXR Extra: Per-Instance Data
	// 添加地平面
	m_sbtHelper.AddHitGroup(L"PlaneHitGroup", {});

	// 给定shader和他们的参数，计算SBT的大小
	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

	// 在 upload heap 中创建 SBT. 由于帮助类需要使用映射来编写 SBT 内容，这是必须的。
	// 在 SBT 编译后，它可以被复制到 default heap 来提升性能
	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), 
		sbtSize, 
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nv_helpers_dx12::kUploadHeapProps);
	if (!m_sbtStorage) {
		throw std::logic_error("Could not allocate the shader binding table");
	}
	// 编译 shader
	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
}

// DXR Extra: Perspective Camera
//----------------------------------------------------------------------------------
//
// Camera Buffer 是一个常量缓冲，其存储相机变换矩阵用于光栅和光追。该方法分配了矩阵复制所需
// 要的缓冲。为了代码清晰，他同时创建了仅包含该缓冲的堆用于 rasterization。
// 
void D3D12HelloTriangle::CreateCameraBuffer() {
	// view, perspective, viewInv, perspectiveInv 
	uint32_t nbMatrix = 4;
	// 给所有变换矩阵分配常量缓冲
	m_cameraBufferSize = nbMatrix * sizeof(XMMATRIX);
	m_cameraBuffer = nv_helpers_dx12::CreateBuffer(m_device.Get(), m_cameraBufferSize,D3D12_RESOURCE_FLAG_NONE, 
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps); 
	// 创建用于光栅的 descriptor heap
	m_constHeap = nv_helpers_dx12::CreateDescriptorHeap( m_device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true); 
	// 描述和创建 constant buffer view 
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {}; 
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_cameraBufferSize; 
	// 获取一个在 CPU 侧的堆内存句柄，用于直接改写 descriptor
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_constHeap->GetCPUDescriptorHandleForHeapStart(); 
	m_device->CreateConstantBufferView(&cbvDesc, srvHandle);
}

// # DXR Extra - Perspective Camera
//--------------------------------------------------------------------------------
// 
// 创建和复制camera viewmodel 及 perspective 矩阵
//
void D3D12HelloTriangle::UpdateCameraBuffer() {
	std::vector<XMMATRIX> matrices(4); 
	// 初始化 view matrix，理想情况下该矩阵基于用户的互动。用于光栅化的 lookat 和 
	// perspective matrices 被定义来将世界向量变换到 [0,1]x[0,1]x[0,1] 相机空间
	const glm::mat4& mat = nv_helpers_dx12::CameraManip.getMatrix(); 
	memcpy(&matrices[0].r->m128_f32[0], glm::value_ptr(mat), 16 * sizeof(float));

	float fovAngleY = 45.0f * XM_PI / 180.0f; 
	matrices[1] = XMMatrixPerspectiveFovRH(fovAngleY, m_aspectRatio, 0.1f, 1000.0f); 
	// 光追和光栅化相反，光线在相机空间被定义，然后被转换到世界空间中。为此，我们需要保存
	// 逆矩阵 
	XMVECTOR det; 
	matrices[2] = XMMatrixInverse(&det, matrices[0]); 
	matrices[3] = XMMatrixInverse(&det, matrices[1]); 
	// 复制矩阵内容 
	uint8_t *pData; 
	ThrowIfFailed(m_cameraBuffer->Map(0, nullptr, (void **)&pData)); 
	memcpy(pData, matrices.data(), m_cameraBufferSize); m_cameraBuffer->Unmap(0, nullptr);
}

// # DXR Extra - Perspective Camera
//-------------------------------------------------------------------------------- 
//  
void D3D12HelloTriangle::OnButtonDown(UINT32 lParam) {
	nv_helpers_dx12::CameraManip.setMousePosition(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam)); 
} 

// # DXR Extra - Persepective Camera
//--------------------------------------------------------------------------------
// 
void D3D12HelloTriangle::OnMouseMove(UINT8 wParam, UINT32 lParam) { 
	using nv_helpers_dx12::Manipulator; Manipulator::Inputs inputs;
	inputs.lmb = wParam & MK_LBUTTON; inputs.mmb = wParam & MK_MBUTTON;
	inputs.rmb = wParam & MK_RBUTTON;
	if (!inputs.lmb && !inputs.rmb && !inputs.mmb) 
		return; 
	// no mouse button pressed 
	inputs.ctrl = GetAsyncKeyState(VK_CONTROL); 
	inputs.shift = GetAsyncKeyState(VK_SHIFT);
	inputs.alt = GetAsyncKeyState(VK_MENU); 
	nv_helpers_dx12::CameraManip.mouseMove(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam), inputs);
}

// #DXR Extra: Per-Instance Data
//---CreatePlaneVB-------------------------------------------------------------
//
// 给平面创建顶点缓冲（vertex buffer,VB）
//
void D3D12HelloTriangle::CreatePlaneVB() {
	// 定义平面几何结构
	Vertex planeVertices[] = { 
		{{-1.5f, -.8f, 01.5f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // 0 
		{{-1.5f, -.8f, -1.5f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // 1 
		{{01.5f, -.8f, 01.5f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // 2 
		{{01.5f, -.8f, 01.5f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // 2 
		{{-1.5f, -.8f, -1.5f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // 1 
		{{01.5f, -.8f, -1.5f}, {1.0f, 1.0f, 1.0f, 1.0f}}  // 4 
	}; 
	const UINT planeBufferSize = sizeof(planeVertices); 
	// 【注意】 使用 upload heaps 来传输 static data 就像 vert buffer 一样不被推荐。
	// 每次 GPU 需要他时，upload heap 都会被编组（marshalled）。在这里的 upload head
	// 适用于简化代码，并且只有很少的 verts 被实际转移。
	CD3DX12_HEAP_PROPERTIES heapProperty =
		CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferResource =
		CD3DX12_RESOURCE_DESC::Buffer(planeBufferSize);
	ThrowIfFailed(m_device->CreateCommittedResource(
		&heapProperty, D3D12_HEAP_FLAG_NONE, &bufferResource,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&m_planeBuffer)));

	// 将三角形数据复制到顶点缓冲（vertex buffer）
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);	// 我们并不打算从CPU的这个资源读取数据。
	ThrowIfFailed(m_planeBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, planeVertices, sizeof(planeVertices));
	m_planeBuffer->Unmap(0, nullptr);

	// 初始化顶点缓存视图（vertex buffer view）.
	m_planeBufferView.BufferLocation = m_planeBuffer->GetGPUVirtualAddress();
	m_planeBufferView.StrideInBytes = sizeof(Vertex);
	m_planeBufferView.SizeInBytes = planeBufferSize;
}

// #DXR Extra: Per-Instance Data
//---CreatePlaneVB-------------------------------------------------------------
//
// 给三角形创建顶点缓冲（vertex buffer,VB）
//
void D3D12HelloTriangle::CreateTriangleVB() {
	// 定义三角形几何结构
	Vertex triangleVertices[] = {
		{{0.0f, 0.25f * m_aspectRatio, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
		{{0.25f, -0.25f * m_aspectRatio, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
		{{-0.25f, -0.25f * m_aspectRatio, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}
	};
	const UINT vertexBufferSize = sizeof(triangleVertices);
	// 【注意】 使用 upload heaps 来传输 static data 就像 vert buffer 一样不被推荐。
	// 每次 GPU 需要他时，upload heap 都会被编组（marshalled）。在这里的 upload head
	// 适用于简化代码，并且只有很少的 verts 被实际转移。
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_vertexBuffer)));

	// 将三角形数据复制到顶点缓冲
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
	m_vertexBuffer->Unmap(0, nullptr);

	// 初始化顶点缓冲视图
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.StrideInBytes = sizeof(Vertex);
	m_vertexBufferView.SizeInBytes = vertexBufferSize;
}

// #DXR Extra: Indexed Geometry
//---CreatePlaneVB-------------------------------------------------------------
//
// 给四面体创建顶点缓冲（vertex buffer,VB）
//
void D3D12HelloTriangle::CreateTetrahedronVB() {
	Vertex tetrahedronVertices[] = {
		{{std::sqrtf(8.f / 9.f), 0.f, -1.f / 3.f}, {1.f, 0.f, 0.f, 1.f}}, 
		{{-std::sqrtf(2.f / 9.f), std::sqrtf(2.f / 3.f), -1.f / 3.f}, {0.f, 1.f, 0.f, 1.f}}, 
		{{-std::sqrtf(2.f / 9.f), -std::sqrtf(2.f / 3.f), -1.f / 3.f}, {0.f, 0.f, 1.f, 1.f}}, 
		{{0.f, 0.f, 1.f}, {1, 0, 1, 1}} 
	};

	const UINT vertexBufferSize = sizeof(tetrahedronVertices);
	// 【注意】 使用 upload heaps 来传输 static data 就像 vert buffer 一样不被推荐。
	// 每次 GPU 需要他时，upload heap 都会被编组（marshalled）。在这里的 upload head
	// 适用于简化代码，并且只有很少的 verts 被实际转移。
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_vertexBuffer)));

	// 数据复制到顶点缓冲
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, tetrahedronVertices, sizeof(tetrahedronVertices));
	m_vertexBuffer->Unmap(0, nullptr);

	// 初始化顶点缓冲视图
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.StrideInBytes = sizeof(Vertex);
	m_vertexBufferView.SizeInBytes = vertexBufferSize;

	//---DXR Extra: Indexed Geometry------------------------------------------------------------------------
	// Indices
	std::vector<UINT> indices = { 0, 1, 2, 0, 3, 1, 0, 2, 3, 1, 3, 2 };
	const UINT indexBufferSize = static_cast<UINT>(indices.size()) * sizeof(UINT);
	CD3DX12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferResource = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
	ThrowIfFailed(m_device->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE, &bufferResource, // 
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)));

	// Copy the triangle data to the index buffer.
	UINT8 * pIndexDataBegin;
	ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, indices.data(), indexBufferSize);
	m_indexBuffer->Unmap(0, nullptr);

	// Initialize the index buffer view.
	m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
	m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	m_indexBufferView.SizeInBytes = indexBufferSize;
}

// #DXR Extra: Per-Instance Data
//-----------------------------------------------------------------------------
// 创建全局常量缓冲 
//
void D3D12HelloTriangle::CreateGlobalConstantBuffer()
{ // 由于HLSL包裹规则，我们创建了 9个float4 的 CB (each needs to start on a 16-byte boundary)
	XMVECTOR bufferData[] = { 
		// A 
		XMVECTOR{1.0f, 0.0f, 0.0f, 1.0f}, 
		XMVECTOR{0.7f, 0.4f, 0.0f, 1.0f}, 
		XMVECTOR{0.4f, 0.7f, 0.0f, 1.0f}, 
		// B 
		XMVECTOR{0.0f, 1.0f, 0.0f, 1.0f}, 
		XMVECTOR{0.0f, 0.7f, 0.4f, 1.0f}, 
		XMVECTOR{0.0f, 0.4f, 0.7f, 1.0f}, 
		// C 
		XMVECTOR{0.0f, 0.0f, 1.0f, 1.0f}, 
		XMVECTOR{0.4f, 0.0f, 0.7f, 1.0f}, 
		XMVECTOR{0.7f, 0.0f, 0.4f, 1.0f}, 
	}; 
	// 创建全局常量缓冲 
	m_globalConstantBuffer = nv_helpers_dx12::CreateBuffer( 
		m_device.Get(),
		sizeof(bufferData),
		D3D12_RESOURCE_FLAG_NONE, 
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nv_helpers_dx12::kUploadHeapProps); 
	// 将CPU内存复制到GPU 
	uint8_t* pData; 
	ThrowIfFailed(m_globalConstantBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, bufferData, sizeof(bufferData)); 
	m_globalConstantBuffer->Unmap(0, nullptr);
}

// #DXR Extra: Per-Instance Data
//---CreatePerInstanceConstantBuffers------------------------------------------
// 创建实例常量缓冲 
//
void D3D12HelloTriangle::CreatePerInstanceConstantBuffers() {
	// Due to HLSL packing rules, we create the CB with 9 float4 (each needs to
	// start on a 16-byte boundary)
	XMVECTOR bufferData[] = {
		// A
		XMVECTOR{1.0f, 0.0f, 0.0f, 1.0f},
		XMVECTOR{1.0f, 0.4f, 0.0f, 1.0f},
		XMVECTOR{1.0f, 0.7f, 0.0f, 1.0f},

		// B
		XMVECTOR{0.0f, 1.0f, 0.0f, 1.0f},
		XMVECTOR{0.0f, 1.0f, 0.4f, 1.0f},
		XMVECTOR{0.0f, 1.0f, 0.7f, 1.0f},

		// C
		XMVECTOR{0.0f, 0.0f, 1.0f, 1.0f},
		XMVECTOR{0.4f, 0.0f, 1.0f, 1.0f},
		XMVECTOR{0.7f, 0.0f, 1.0f, 1.0f},
	};
	m_perInstanceConstantBuffers.resize(3);
	int i(0);
	for (auto& cb : m_perInstanceConstantBuffers) {
		const uint32_t bufferSize = sizeof(XMVECTOR) * 3;
		cb = nv_helpers_dx12::CreateBuffer(
			m_device.Get(), 
			bufferSize, 
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ, 
			nv_helpers_dx12::kUploadHeapProps);
		uint8_t* pData;
		ThrowIfFailed(cb->Map(0, nullptr, (void**)&pData));
		memcpy(pData, &bufferData[i * 3], bufferSize);
		cb->Unmap(0, nullptr);
		++i;
	}
}

//  #DXR Extra: Depth Buffer
//---CreateDepthBuffer-----------------------------------------------------
// 给光栅化创建深度缓冲，该缓冲需要存储到另外的heap中。
void D3D12HelloTriangle::CreateDepthBuffer() {
	// depth buffer heap type 是特例化的，heap 内容对 shader 不可见。
	m_dsvHeap = nv_helpers_dx12::CreateDescriptorHeap(m_device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);
	// The depth and stencil can be packed into a single 32-bit texture buffer. Since we do not need 
	// stencil, we use the 32 bits to store depth information (DXGI_FORMAT_D32_FLOAT). 
	D3D12_HEAP_PROPERTIES depthHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT); 
	D3D12_RESOURCE_DESC depthResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 1);
	depthResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	// The depth values will be initialized to 1 
	CD3DX12_CLEAR_VALUE depthOptimizedClearValue(DXGI_FORMAT_D32_FLOAT, 1.0f, 0); 
	// Allocate the buffer itself, with a state allowing depth writes 
	ThrowIfFailed(m_device->CreateCommittedResource( 
		&depthHeapProperties, 
		D3D12_HEAP_FLAG_NONE, 
		&depthResourceDesc, 
		D3D12_RESOURCE_STATE_DEPTH_WRITE, 
		&depthOptimizedClearValue, IID_PPV_ARGS(&m_depthStencil))); 
	// Write the depth buffer view into the depth buffer heap 
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {}; 
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; 
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; 
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE; 
	m_device->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}
