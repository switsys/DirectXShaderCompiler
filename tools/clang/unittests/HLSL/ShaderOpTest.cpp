///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// ShaderOpTest.cpp                                                          //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// Licensed under the MIT license. See COPYRIGHT in the project root for     //
// full license information.                                                 //
//                                                                           //
// Provides the implementation to run tests based on descriptions.           //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3dx12.h>
#include <d3dcompiler.h>
#include <atlbase.h>

#include "ShaderOpTest.h"

#include "dxc/dxcapi.h"             // IDxcCompiler
#include "dxc/Support/Global.h"     // OutputDebugBytes
#include "dxc/Support/Unicode.h"    // IsStarMatchUTF16
#include "dxc/Support/dxcapi.use.h" // DxcDllSupport
#include "WexTestClass.h"           // TAEF
#include "HLSLTestUtils.h"          // LogCommentFmt

#include <stdlib.h>
#include <DirectXMath.h>
#include <intsafe.h>
#include <strsafe.h>
#include <xmllite.h>
#pragma comment(lib, "xmllite.lib")

///////////////////////////////////////////////////////////////////////////////
// Useful helper functions.

static st::OutputStringFn g_OutputStrFn;
static void * g_OutputStrFnCtx;

void st::SetOutputFn(void *pCtx, OutputStringFn F) {
  g_OutputStrFnCtx = pCtx;
  g_OutputStrFn = F;
}

static void ShaderOpLogFmt(_In_z_ _Printf_format_string_ const wchar_t *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::wstring buf(hlsl_test::vFormatToWString(fmt, args));
  va_end(args);
  if (g_OutputStrFn == nullptr)
    WEX::Logging::Log::Comment(buf.data());
  else
    g_OutputStrFn(g_OutputStrFnCtx, buf.data());
}

// Rely on TAEF Verifier helpers.
#define CHECK_HR(x) { \
  if (!g_OutputStrFn) VERIFY_SUCCEEDED(x); else { \
  HRESULT _check_hr = (x); \
  if (FAILED(_check_hr)) AtlThrow(x); } \
}

// Check the specified HRESULT and return the success value.
static HRESULT CHECK_HR_RET(HRESULT hr) {
  CHECK_HR(hr);
  return hr;
}

HRESULT LogIfLost(HRESULT hr, ID3D12Device *pDevice) {
  if (hr == DXGI_ERROR_DEVICE_REMOVED) {
    HRESULT reason = pDevice->GetDeviceRemovedReason();
    LPCWSTR reasonText = L"?";
    if (reason == DXGI_ERROR_DEVICE_HUNG) reasonText = L"DXGI_ERROR_DEVICE_HUNG";
    if (reason == DXGI_ERROR_DEVICE_REMOVED) reasonText = L"DXGI_ERROR_DEVICE_REMOVED";
    if (reason == DXGI_ERROR_DEVICE_RESET) reasonText = L"DXGI_ERROR_DEVICE_RESET";
    if (reason == DXGI_ERROR_DRIVER_INTERNAL_ERROR) reasonText = L"DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    if (reason == DXGI_ERROR_INVALID_CALL) reasonText = L"DXGI_ERROR_INVALID_CALL";
    ShaderOpLogFmt(L"Device lost: 0x%08x (%s)", reason, reasonText);
  }
  return hr;
}

HRESULT LogIfLost(HRESULT hr, ID3D12Resource *pResource) {
  if (hr == DXGI_ERROR_DEVICE_REMOVED) {
    CComPtr<ID3D12Device> pDevice;
    pResource->GetDevice(__uuidof(ID3D12Device), (void**)&pDevice);
    LogIfLost(hr, pDevice);
  }
  return hr;
}


bool UseHardwareDevice(const DXGI_ADAPTER_DESC1 &desc, LPCWSTR AdapterName) {
  if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
    // Don't select the Basic Render Driver adapter.
    return false;
  }

  if (!AdapterName)
    return true;
  return Unicode::IsStarMatchUTF16(AdapterName, wcslen(AdapterName),
                                   desc.Description, wcslen(desc.Description));
}

UINT GetByteSizeForFormat(DXGI_FORMAT value) {
  switch (value) {
  case DXGI_FORMAT_R32G32B32A32_TYPELESS: return 16;
  case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
  case DXGI_FORMAT_R32G32B32A32_UINT: return 16;
  case DXGI_FORMAT_R32G32B32A32_SINT: return 16;
  case DXGI_FORMAT_R32G32B32_TYPELESS: return 12;
  case DXGI_FORMAT_R32G32B32_FLOAT: return 12;
  case DXGI_FORMAT_R32G32B32_UINT: return 12;
  case DXGI_FORMAT_R32G32B32_SINT: return 12;
  case DXGI_FORMAT_R16G16B16A16_TYPELESS: return 8;
  case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
  case DXGI_FORMAT_R16G16B16A16_UNORM: return 8;
  case DXGI_FORMAT_R16G16B16A16_UINT: return 8;
  case DXGI_FORMAT_R16G16B16A16_SNORM: return 8;
  case DXGI_FORMAT_R16G16B16A16_SINT: return 8;
  case DXGI_FORMAT_R32G32_TYPELESS: return 8;
  case DXGI_FORMAT_R32G32_FLOAT: return 8;
  case DXGI_FORMAT_R32G32_UINT: return 8;
  case DXGI_FORMAT_R32G32_SINT: return 8;
  case DXGI_FORMAT_R32G8X24_TYPELESS: return 8;
  case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return 4;
  case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return 4;
  case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return 4;
  case DXGI_FORMAT_R10G10B10A2_TYPELESS: return 4;
  case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
  case DXGI_FORMAT_R10G10B10A2_UINT: return 4;
  case DXGI_FORMAT_R11G11B10_FLOAT: return 4;
  case DXGI_FORMAT_R8G8B8A8_TYPELESS: return 4;
  case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 4;
  case DXGI_FORMAT_R8G8B8A8_UINT: return 4;
  case DXGI_FORMAT_R8G8B8A8_SNORM: return 4;
  case DXGI_FORMAT_R8G8B8A8_SINT: return 4;
  case DXGI_FORMAT_R16G16_TYPELESS: return 4;
  case DXGI_FORMAT_R16G16_FLOAT: return 4;
  case DXGI_FORMAT_R16G16_UNORM: return 4;
  case DXGI_FORMAT_R16G16_UINT: return 4;
  case DXGI_FORMAT_R16G16_SNORM: return 4;
  case DXGI_FORMAT_R16G16_SINT: return 4;
  case DXGI_FORMAT_R32_TYPELESS: return 4;
  case DXGI_FORMAT_D32_FLOAT: return 4;
  case DXGI_FORMAT_R32_FLOAT: return 4;
  case DXGI_FORMAT_R32_UINT: return 4;
  case DXGI_FORMAT_R32_SINT: return 4;
  case DXGI_FORMAT_R24G8_TYPELESS: return 4;
  case DXGI_FORMAT_D24_UNORM_S8_UINT: return 4;
  case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return 4;
  case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return 4;
  case DXGI_FORMAT_R8G8_TYPELESS: return 2;
  case DXGI_FORMAT_R8G8_UNORM: return 2;
  case DXGI_FORMAT_R8G8_UINT: return 2;
  case DXGI_FORMAT_R8G8_SNORM: return 2;
  case DXGI_FORMAT_R8G8_SINT: return 2;
  case DXGI_FORMAT_R16_TYPELESS: return 2;
  case DXGI_FORMAT_R16_FLOAT: return 2;
  case DXGI_FORMAT_D16_UNORM: return 2;
  case DXGI_FORMAT_R16_UNORM: return 2;
  case DXGI_FORMAT_R16_UINT: return 2;
  case DXGI_FORMAT_R16_SNORM: return 2;
  case DXGI_FORMAT_R16_SINT: return 2;
  case DXGI_FORMAT_R8_TYPELESS: return 1;
  case DXGI_FORMAT_R8_UNORM: return 1;
  case DXGI_FORMAT_R8_UINT: return 1;
  case DXGI_FORMAT_R8_SNORM: return 1;
  case DXGI_FORMAT_R8_SINT: return 1;
  case DXGI_FORMAT_A8_UNORM: return 1;
  case DXGI_FORMAT_R1_UNORM: return 1;
  default:
    CHECK_HR(E_INVALIDARG);
    return 0;
  }
}

void GetHardwareAdapter(IDXGIFactory2 *pFactory, LPCWSTR AdapterName,
                               IDXGIAdapter1 **ppAdapter) {
  CComPtr<IDXGIAdapter1> adapter;
  *ppAdapter = nullptr;

  for (UINT adapterIndex = 0;
       DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter);
       ++adapterIndex) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    if (!UseHardwareDevice(desc, AdapterName)) {
      adapter.Release();
      continue;
    }

    // Check to see if the adapter supports Direct3D 12, but don't create the
    // actual device yet.
    if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                    _uuidof(ID3D12Device), nullptr))) {
      break;
    }
    adapter.Release();
  }

  *ppAdapter = adapter.Detach();
}

void RecordTransitionBarrier(ID3D12GraphicsCommandList *pCommandList,
                                    ID3D12Resource *pResource,
                                    D3D12_RESOURCE_STATES before,
                                    D3D12_RESOURCE_STATES after) {
  CD3DX12_RESOURCE_BARRIER barrier(
      CD3DX12_RESOURCE_BARRIER::Transition(pResource, before, after));
  pCommandList->ResourceBarrier(1, &barrier);
}

void ExecuteCommandList(ID3D12CommandQueue *pQueue, ID3D12CommandList *pList) {
  ID3D12CommandList *ppCommandLists[] = { pList };
  pQueue->ExecuteCommandLists(1, ppCommandLists);
}

HRESULT SetObjectName(ID3D12Object *pObject, LPCSTR pName) {
  if (pObject && pName) {
    CA2W WideName(pName);
    return pObject->SetName(WideName);
  }
  return S_FALSE;
}

void WaitForSignal(ID3D12CommandQueue *pCQ, ID3D12Fence *pFence,
                          HANDLE hFence, UINT64 fenceValue) {
  // Signal and increment the fence value.
  const UINT64 fence = fenceValue;
  CHECK_HR(pCQ->Signal(pFence, fence));

  if (pFence->GetCompletedValue() < fenceValue) {
    CHECK_HR(pFence->SetEventOnCompletion(fenceValue, hFence));
    WaitForSingleObject(hFence, INFINITE);
    //CHECK_HR(pCQ->Wait(pFence, fenceValue));
  }
}

static void SetupComputeValuePattern(std::vector<uint32_t> &values, size_t count) {
  values.resize(count); // one element per dispatch group, in bytes
  for (size_t i = 0; i < count; ++i) {
    values[i] = (uint32_t)i;
  }
}

void MappedData::dump() const {
  OutputDebugBytes(m_pData, m_size);
}
void MappedData::reset() {
  if (m_pResource != nullptr) {
    m_pResource->Unmap(0, nullptr);
    m_pResource.Release();
  }
  m_pData = nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// Helper class for mapped data.

void MappedData::reset(ID3D12Resource *pResource, UINT32 sizeInBytes) {
  reset();
  D3D12_RANGE r;
  r.Begin = 0;
  r.End = sizeInBytes;
  CHECK_HR(LogIfLost(pResource->Map(0, &r, &m_pData), pResource));
  m_pResource = pResource;
  m_size = sizeInBytes;
}

///////////////////////////////////////////////////////////////////////////////
// ShaderOpTest library implementation.

namespace st {

LPCSTR string_table::insert(LPCSTR pValue) {
  std::unordered_set<LPCSTR, HashStr, PredStr>::iterator i = m_values.find(pValue);
  if (i == m_values.end()) {
    size_t bufSize = strlen(pValue) + 1;
    std::vector<char> s;
    s.resize(bufSize);
    strcpy_s(s.data(), bufSize, pValue);
    LPCSTR result = s.data();
    m_values.insert(result);
    m_strings.push_back(std::move(s));
    return result;
  }
  else {
    return *i;
  }
}

LPCSTR string_table::insert(LPCWSTR pValue) {
  CW2A pValueAnsi(pValue);
  return insert(pValueAnsi.m_psz);
}

void CommandListRefs::CreateForDevice(ID3D12Device *pDevice, bool compute) {
  D3D12_COMMAND_LIST_TYPE T = compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE
                                      : D3D12_COMMAND_LIST_TYPE_DIRECT;
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = T;

  if (Queue == nullptr) {
    CHECK_HR(pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&Queue)));
  }
  CHECK_HR(pDevice->CreateCommandAllocator(T, IID_PPV_ARGS(&Allocator)));
  CHECK_HR(pDevice->CreateCommandList(0, T, Allocator, nullptr,
                                      IID_PPV_ARGS(&List)));
}

void ShaderOpTest::CopyBackResources() {
  CommandListRefs ResCommandList;
  ResCommandList.CreateForDevice(m_pDevice, m_pShaderOp->IsCompute());
  ID3D12GraphicsCommandList *pList = ResCommandList.List;

  pList->SetName(L"ShaderOpTest Resource ReadBack CommandList");
  for (ShaderOpResource &R : m_pShaderOp->Resources) {
    if (!R.ReadBack)
      continue;
    ShaderOpResourceData &D = m_ResourceData[R.Name];
    RecordTransitionBarrier(pList, D.Resource, D.ResourceState,
                            D3D12_RESOURCE_STATE_COPY_SOURCE);
    D.ResourceState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    D3D12_RESOURCE_DESC &Desc = D.ShaderOpRes->Desc;
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      pList->CopyResource(D.ReadBack, D.Resource);
    }
    else {
      UINT rowPitch = Desc.Width * 4;
      if (rowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)
        rowPitch += D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - (rowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
      Footprint.Offset = 0;
      Footprint.Footprint = CD3DX12_SUBRESOURCE_FOOTPRINT(DXGI_FORMAT_R8G8B8A8_UNORM, Desc.Width, Desc.Height, 1, rowPitch);
      CD3DX12_TEXTURE_COPY_LOCATION DstLoc(D.ReadBack, Footprint);
      CD3DX12_TEXTURE_COPY_LOCATION SrcLoc(D.Resource, 0);
      pList->CopyTextureRegion(&DstLoc, 0, 0, 0, &SrcLoc, nullptr);
    }
  }
  pList->Close();
  ExecuteCommandList(ResCommandList.Queue, pList);
  WaitForSignal(ResCommandList.Queue, m_pFence, m_hFence, m_FenceValue++);
}

void ShaderOpTest::CreateCommandList() {
  bool priorQueue = m_CommandList.Queue != nullptr;
  m_CommandList.CreateForDevice(m_pDevice, m_pShaderOp->IsCompute());
  m_CommandList.Allocator->SetName(L"ShaderOpTest Allocator");
  m_CommandList.List->SetName(L"ShaderOpTest CommandList");
  if (!priorQueue)
    m_CommandList.Queue->SetName(L"ShaderOpTest CommandList");
}

void ShaderOpTest::CreateDescriptorHeaps() {
  for (ShaderOpDescriptorHeap &H : m_pShaderOp->DescriptorHeaps) {
    CComPtr<ID3D12DescriptorHeap> pHeap;
    if (H.Desc.NumDescriptors == 0) {
      H.Desc.NumDescriptors = (UINT)H.Descriptors.size();
    }
    CHECK_HR(m_pDevice->CreateDescriptorHeap(&H.Desc, IID_PPV_ARGS(&pHeap)));
    m_DescriptorHeaps.push_back(pHeap);
    m_DescriptorHeapsByName[H.Name] = pHeap;
    SetObjectName(pHeap, H.Name);

    const UINT descriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(H.Desc.Type);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(pHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(pHeap->GetGPUDescriptorHandleForHeapStart());
    for (ShaderOpDescriptor &D : H.Descriptors) {
      ShaderOpResource *R = m_pShaderOp->GetResourceByName(D.ResName);
      if (R == nullptr) {
        LPCSTR DescName = D.Name ? D.Name : "[unnamed descriptor]";
        ShaderOpLogFmt(L"Descriptor '%S' references missing resource '%S'", DescName, D.ResName);
        CHECK_HR(E_INVALIDARG);
      }

      ShaderOpResourceData &Data = m_ResourceData[D.ResName];
      ShaderOpDescriptorData DData;
      DData.Descriptor = &D;
      DData.ResData = &Data;
      if (0 == _stricmp(D.Kind, "UAV")) {
        ID3D12Resource *pCounterResource = nullptr;
        if (D.CounterName && *D.CounterName) {
          ShaderOpResourceData &CounterData = m_ResourceData[D.CounterName];
          pCounterResource = CounterData.Resource;
        }
        m_pDevice->CreateUnorderedAccessView(Data.Resource, pCounterResource,
                                             &D.UavDesc, cpuHandle);
      }
      else if (0 == _stricmp(D.Kind, "SRV")) {
        D3D12_SHADER_RESOURCE_VIEW_DESC *pSrvDesc = nullptr;
        m_pDevice->CreateShaderResourceView(Data.Resource, pSrvDesc, cpuHandle);
      }
      else if (0 == _stricmp(D.Kind, "RTV")) {
        m_pDevice->CreateRenderTargetView(Data.Resource, nullptr, cpuHandle);
      }
      else if (0 == _stricmp(D.Kind, "CBV")) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = Data.Resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = Data.Resource->GetDesc().Width;
        m_pDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
      }

      DData.GPUHandle = gpuHandle;
      DData.CPUHandle = cpuHandle;
      m_DescriptorData[R->Name] = DData;
      cpuHandle = cpuHandle.Offset(descriptorSize);
      gpuHandle = gpuHandle.Offset(descriptorSize);
    }
  }

  // Create query descriptor heap.
  D3D12_QUERY_HEAP_DESC queryHeapDesc;
  ZeroMemory(&queryHeapDesc, sizeof(queryHeapDesc));
  queryHeapDesc.Count = 1;
  queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
  CHECK_HR(m_pDevice->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_pQueryHeap)));
}

void ShaderOpTest::CreateDevice() {
  if (m_pDevice == nullptr) {
    const D3D_FEATURE_LEVEL FeatureLevelRequired = D3D_FEATURE_LEVEL_11_0;
    CComPtr<IDXGIFactory4> factory;
    CComPtr<ID3D12Device> pDevice;

    CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    if (m_pShaderOp->UseWarpDevice) {
      CComPtr<IDXGIAdapter> warpAdapter;
      CHECK_HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
      CHECK_HR(D3D12CreateDevice(warpAdapter, FeatureLevelRequired,
                                 IID_PPV_ARGS(&pDevice)));
    } else {
      CComPtr<IDXGIAdapter1> hardwareAdapter;
      GetHardwareAdapter(factory, m_pShaderOp->AdapterName, &hardwareAdapter);
      if (hardwareAdapter == nullptr) {
        CHECK_HR(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
      }
      CHECK_HR(D3D12CreateDevice(hardwareAdapter, FeatureLevelRequired,
                                 IID_PPV_ARGS(&pDevice)));
    }

    m_pDevice.Attach(pDevice.Detach());
    m_pDevice->SetName(L"ShaderOpTest Device");
  }

  m_FenceValue = 1;
  CHECK_HR(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                  __uuidof(ID3D12Fence), (void **)&m_pFence));
  m_pFence->SetName(L"ShaderOpTest Fence");
  m_hFence = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (m_hFence == nullptr) {
    AtlThrow(HRESULT_FROM_WIN32(GetLastError()));
  }
}

static void InitByteCode(D3D12_SHADER_BYTECODE *pBytecode, ID3D10Blob *pBlob) {
  if (pBlob == nullptr) {
    pBytecode->BytecodeLength = 0;
    pBytecode->pShaderBytecode = nullptr;
  }
  else {
    pBytecode->BytecodeLength = pBlob->GetBufferSize();
    pBytecode->pShaderBytecode = pBlob->GetBufferPointer();
  }
}

void ShaderOpTest::CreatePipelineState() {
  CreateRootSignature();
  CreateShaders();
  if (m_pShaderOp->IsCompute()) {
    CComPtr<ID3D10Blob> pCS;
    pCS = m_Shaders[m_pShaderOp->CS];
    D3D12_COMPUTE_PIPELINE_STATE_DESC CDesc;
    ZeroMemory(&CDesc, sizeof(CDesc));
    CDesc.pRootSignature = m_pRootSignature.p;
    InitByteCode(&CDesc.CS, pCS);
    CHECK_HR(m_pDevice->CreateComputePipelineState(&CDesc, IID_PPV_ARGS(&m_pPSO)));
  }
  else {
    CComPtr<ID3D10Blob> pPS;
    CComPtr<ID3D10Blob> pVS;
    pPS = m_Shaders[m_pShaderOp->PS];
    pVS = m_Shaders[m_pShaderOp->VS];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC GDesc;
    ZeroMemory(&GDesc, sizeof(GDesc));
    InitByteCode(&GDesc.VS, pVS);
    InitByteCode(&GDesc.PS, pPS);
    GDesc.InputLayout.NumElements = m_pShaderOp->InputElements.size();
    GDesc.InputLayout.pInputElementDescs = m_pShaderOp->InputElements.data();
    GDesc.PrimitiveTopologyType = m_pShaderOp->PrimitiveTopology;
    GDesc.NumRenderTargets = m_pShaderOp->RenderTargets.size();
    GDesc.SampleMask = m_pShaderOp->SampleMask;
    for (size_t i = 0; i < m_pShaderOp->RenderTargets.size(); ++i) {
      ShaderOpResource *R = m_pShaderOp->GetResourceByName(m_pShaderOp->RenderTargets[i]);
      GDesc.RTVFormats[i] = R->Desc.Format;
    }
    GDesc.SampleDesc.Count = 1; // TODO: read from file, set form shader operation; also apply to count
    GDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // TODO: read from file, set from op
    GDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // TODO: read frm file, set from op

    // TODO: pending values to set
#if 0
    D3D12_STREAM_OUTPUT_DESC           StreamOutput;
    D3D12_DEPTH_STENCIL_DESC           DepthStencilState;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue;
    DXGI_FORMAT                        DSVFormat;
    UINT                               NodeMask;
    D3D12_PIPELINE_STATE_FLAGS         Flags;
#endif
    GDesc.pRootSignature = m_pRootSignature.p;
    CHECK_HR(m_pDevice->CreateGraphicsPipelineState(&GDesc, IID_PPV_ARGS(&m_pPSO)));
  }
}

void ShaderOpTest::CreateResources() {
  CommandListRefs ResCommandList;
  ResCommandList.CreateForDevice(m_pDevice, true);
  ResCommandList.Allocator->SetName(L"ShaderOpTest Resource Creation Allocation");
  ResCommandList.Queue->SetName(L"ShaderOpTest Resource Creation Queue");
  ResCommandList.List->SetName(L"ShaderOpTest Resource Creation CommandList");
  
  ID3D12GraphicsCommandList *pList = ResCommandList.List.p;
  std::vector<CComPtr<ID3D12Resource> > intermediates;

  for (ShaderOpResource &R : m_pShaderOp->Resources) {
    if (m_ResourceData.count(R.Name) > 0) continue;
    // Initialize the upload resource early, to allow a by-name initializer
    // to set the desired width.
    bool initByName = R.Init && 0 == _stricmp("byname", R.Init);
    bool initZero = R.Init && 0 == _stricmp("zero", R.Init);
    bool initFromBytes = R.Init && 0 == _stricmp("frombytes", R.Init);
    bool hasInit = initByName || initZero || initFromBytes;
    bool isBuffer = R.Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    std::vector<BYTE> values;
    if (hasInit) {
      if (isBuffer) {
        values.resize((size_t)R.Desc.Width);
      }
      else {
        // Probably needs more information.
        values.resize((size_t)(R.Desc.Width * R.Desc.Height *
          GetByteSizeForFormat(R.Desc.Format)));
      }
      if (initZero) {
        memset(values.data(), 0, values.size());
      }
      else if (initByName) {
        m_InitCallbackFn(R.Name, values);
        if (isBuffer) {
          R.Desc.Width = values.size();
        }
      }
      else if (initFromBytes) {
        values = R.InitBytes;
        if (R.Desc.Width == 0) {
          if (isBuffer) {
            R.Desc.Width = values.size();
          }
          else if (R.Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
            R.Desc.Width = values.size() / GetByteSizeForFormat(R.Desc.Format);
          }
        }
      }
    }

    CComPtr<ID3D12Resource> pResource;
    CHECK_HR(m_pDevice->CreateCommittedResource(
        &R.HeapProperties, R.HeapFlags, &R.Desc, R.InitialResourceState,
        nullptr, IID_PPV_ARGS(&pResource)));
    ShaderOpResourceData &D = m_ResourceData[R.Name];
    D.ShaderOpRes = &R;
    D.Resource = pResource;
    D.ResourceState = R.InitialResourceState;
    SetObjectName(pResource, R.Name);

    if (hasInit) {
      CComPtr<ID3D12Resource> pIntermediate;
      CD3DX12_HEAP_PROPERTIES upload(D3D12_HEAP_TYPE_UPLOAD);
      D3D12_RESOURCE_DESC uploadDesc = R.Desc;
      if (!isBuffer) {
        // Assuming a simple linear layout here.
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width *= uploadDesc.Height;
        uploadDesc.Width *= GetByteSizeForFormat(uploadDesc.Format);
        uploadDesc.Height = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      }
      uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
      CHECK_HR(m_pDevice->CreateCommittedResource(
          &upload, D3D12_HEAP_FLAG_NONE, &uploadDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&pIntermediate)));
      intermediates.push_back(pIntermediate);

      char uploadObjectName[128];
      if (R.Name && SUCCEEDED(StringCchPrintfA(
                        uploadObjectName, _countof(uploadObjectName),
                        "Upload resource for %s", R.Name))) {
        SetObjectName(pIntermediate, uploadObjectName);
      }

      D3D12_SUBRESOURCE_DATA transferData;
      transferData.pData = values.data();
      transferData.RowPitch = values.size();
      transferData.SlicePitch = transferData.RowPitch;
      UpdateSubresources<1>(pList, pResource.p, pIntermediate.p, 0, 0, 1,
                            &transferData);
    }

    if (R.ReadBack) {
      CComPtr<ID3D12Resource> pReadbackResource;
      CD3DX12_HEAP_PROPERTIES readback(D3D12_HEAP_TYPE_READBACK);
      UINT64 width = R.Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
                         ? R.Desc.Width
                         : (R.Desc.Height * R.Desc.Width *
                            GetByteSizeForFormat(R.Desc.Format));
      CD3DX12_RESOURCE_DESC readbackDesc(CD3DX12_RESOURCE_DESC::Buffer(width));
      readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
      CHECK_HR(m_pDevice->CreateCommittedResource(
          &readback, D3D12_HEAP_FLAG_NONE, &readbackDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&pReadbackResource)));
      D.ReadBack = pReadbackResource;

      char readbackObjectName[128];
      if (R.Name && SUCCEEDED(StringCchPrintfA(
                        readbackObjectName, _countof(readbackObjectName),
                        "Readback resource for %s", R.Name))) {
        SetObjectName(pReadbackResource, readbackObjectName);
      }
    }

    if (R.TransitionTo != D.ResourceState) {
      RecordTransitionBarrier(pList, D.Resource, D.ResourceState,
                              R.TransitionTo);
      D.ResourceState = R.TransitionTo;
    }
  }

  // Create a buffer to receive query results.
  {
    CComPtr<ID3D12Resource> pReadbackResource;
    CD3DX12_HEAP_PROPERTIES readback(D3D12_HEAP_TYPE_READBACK);
    CD3DX12_RESOURCE_DESC readbackDesc(CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)));
    CHECK_HR(m_pDevice->CreateCommittedResource(
      &readback, D3D12_HEAP_FLAG_NONE, &readbackDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(&m_pQueryBuffer)));
    SetObjectName(m_pQueryBuffer, "Query Pipeline Readback Buffer");
  }

  CHECK_HR(pList->Close());
  ExecuteCommandList(ResCommandList.Queue, pList);
  WaitForSignal(ResCommandList.Queue, m_pFence, m_hFence, m_FenceValue++);
}

void ShaderOpTest::CreateRootSignature() {
  if (m_pShaderOp->RootSignature == nullptr) {
    AtlThrow(E_INVALIDARG);
  }
  CComPtr<ID3DBlob> pCode;
  CComPtr<ID3DBlob> pRootSignatureBlob;
  CComPtr<ID3DBlob> pError;
  std::string sQuoted;
  sQuoted.reserve(2 + strlen(m_pShaderOp->RootSignature) + 1);
  sQuoted.append("\"");
  sQuoted.append(m_pShaderOp->RootSignature);
  sQuoted.append("\"");
  char *ch = (char *)sQuoted.data();
  while (*ch) {
    if (*ch == '\r' || *ch == '\n') *ch = ' ';
    ++ch;
  }

  D3D_SHADER_MACRO M[2] = {
    { "RootSigVal", sQuoted.c_str() },
    { nullptr, nullptr }
  };
  HRESULT hr = D3DCompile(nullptr, 0, "RootSigShader", M, nullptr, sQuoted.c_str(),
                          "rootsig_1_0", 0, 0, &pCode, &pError);
  if (FAILED(hr) && pError != nullptr) {
    ShaderOpLogFmt(L"Failed to compile root signature:\r\n%*S",
      (int)pError->GetBufferSize(),
      (LPCSTR)pError->GetBufferPointer());
  }
  CHECK_HR(hr);
  CHECK_HR(D3DGetBlobPart(pCode->GetBufferPointer(), pCode->GetBufferSize(),
                          D3D_BLOB_ROOT_SIGNATURE, 0, &pRootSignatureBlob));
  CHECK_HR(m_pDevice->CreateRootSignature(
      0, pRootSignatureBlob->GetBufferPointer(),
      pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature)));
}

static bool TargetUsesDxil(LPCSTR pText) {
  return (strlen(pText) > 3) && pText[3] >= '6'; // xx_6xx
}

void ShaderOpTest::CreateShaders() {
  for (ShaderOpShader &S : m_pShaderOp->Shaders) {
    CComPtr<ID3DBlob> pCode;
    HRESULT hr = S_OK;
    LPCSTR pText = m_pShaderOp->GetShaderText(&S);
    if (TargetUsesDxil(S.Target)) {
      CComPtr<IDxcCompiler> pCompiler;
      CComPtr<IDxcLibrary> pLibrary;
      CComPtr<IDxcBlobEncoding> pTextBlob;
      CComPtr<IDxcOperationResult> pResult;
      CA2W nameW(S.Name, CP_UTF8);
      CA2W entryPointW(S.EntryPoint, CP_UTF8);
      CA2W targetW(S.Target, CP_UTF8);
      HRESULT resultCode;
      CHECK_HR(m_pDxcSupport->CreateInstance(CLSID_DxcLibrary, &pLibrary));
      CHECK_HR(pLibrary->CreateBlobWithEncodingFromPinned(
          (LPBYTE)pText, (UINT32)strlen(pText), CP_UTF8, &pTextBlob));
      CHECK_HR(m_pDxcSupport->CreateInstance(CLSID_DxcCompiler, &pCompiler));
      CHECK_HR(pCompiler->Compile(pTextBlob, nameW, entryPointW, targetW,
                                  nullptr, 0, nullptr, 0, nullptr, &pResult));
      CHECK_HR(pResult->GetStatus(&resultCode));
      if (FAILED(resultCode)) {
        CComPtr<IDxcBlobEncoding> errors;
        CHECK_HR(pResult->GetErrorBuffer(&errors));
        ShaderOpLogFmt(L"Failed to compile shader: %*S\r\n",
                       (int)errors->GetBufferSize(),
                       errors->GetBufferPointer());
      }
      CHECK_HR(resultCode);
      CHECK_HR(pResult->GetResult((IDxcBlob **)&pCode));
    } else {
      CComPtr<ID3DBlob> pError;
      hr = D3DCompile(pText, strlen(pText), S.Name, nullptr, nullptr,
                      S.EntryPoint, S.Target, 0, 0, &pCode, &pError);
      if (FAILED(hr) && pError != nullptr) {
        ShaderOpLogFmt(L"%*S\r\n", (int)pError->GetBufferSize(),
                       ((LPCSTR)pError->GetBufferPointer()));
      }
    }
    CHECK_HR(hr);
    m_Shaders[S.Name] = pCode;
  }
}

void ShaderOpTest::GetPipelineStats(D3D12_QUERY_DATA_PIPELINE_STATISTICS *pStats) {
  MappedData M;
  M.reset(m_pQueryBuffer, sizeof(*pStats));
  memcpy(pStats, M.data(), sizeof(*pStats));
}

void ShaderOpTest::GetReadBackData(LPCSTR pResourceName, MappedData *pData) {
  pResourceName = m_pShaderOp->Strings.insert(pResourceName); // Unique
  ShaderOpResourceData &D = m_ResourceData.at(pResourceName);
  D3D12_RESOURCE_DESC Desc = D.ReadBack->GetDesc();
  UINT32 sizeInBytes = (UINT32)Desc.Width;
  pData->reset(D.ReadBack, sizeInBytes);
}

static void SetDescriptorHeaps(ID3D12GraphicsCommandList *pList,
                               std::vector<ID3D12DescriptorHeap *> &heaps) {
  if (heaps.empty())
    return;
  std::vector<ID3D12DescriptorHeap *> localHeaps;
  for (auto &H : heaps) {
    if (H->GetDesc().Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
      localHeaps.push_back(H);
    }
  }
  if (!localHeaps.empty())
    pList->SetDescriptorHeaps((UINT)localHeaps.size(), localHeaps.data());
}

void ShaderOpTest::RunCommandList() {
  ID3D12GraphicsCommandList *pList = m_CommandList.List.p;
  if (m_pShaderOp->IsCompute()) {
    pList->SetPipelineState(m_pPSO);
    pList->SetComputeRootSignature(m_pRootSignature);
    SetDescriptorHeaps(pList, m_DescriptorHeaps);
    SetRootValues(pList, m_pShaderOp->IsCompute());
    pList->Dispatch(m_pShaderOp->DispatchX, m_pShaderOp->DispatchY,
                    m_pShaderOp->DispatchZ);
  } else {
    pList->SetPipelineState(m_pPSO);
    pList->SetGraphicsRootSignature(m_pRootSignature);
    SetDescriptorHeaps(pList, m_DescriptorHeaps);
    SetRootValues(pList, m_pShaderOp->IsCompute());

    if (!m_pShaderOp->RenderTargets.empty()) {
      // Use the first render target to set up the viewport and scissors.
      ShaderOpResource *R = m_pShaderOp->GetResourceByName(m_pShaderOp->RenderTargets[0]);
      D3D12_VIEWPORT viewport;
      D3D12_RECT scissorRect;

      memset(&viewport, 0, sizeof(viewport));
      viewport.Height = R->Desc.Height;
      viewport.Width = R->Desc.Width;
      viewport.MaxDepth = 1.0f;
      memset(&scissorRect, 0, sizeof(scissorRect));
      scissorRect.right = viewport.Width;
      scissorRect.bottom = viewport.Height;
      pList->RSSetViewports(1, &viewport);
      pList->RSSetScissorRects(1, &scissorRect);
    }

    // Indicate that the buffers will be used as render targets.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8];
    UINT rtvHandleCount = m_pShaderOp->RenderTargets.size();
    for (size_t i = 0; i < rtvHandleCount; ++i) {
      auto &rt = m_pShaderOp->RenderTargets[i];
      ShaderOpDescriptorData &DData = m_DescriptorData[rt];
      rtvHandles[i] = DData.CPUHandle;
      RecordTransitionBarrier(pList, DData.ResData->Resource,
                              DData.ResData->ResourceState,
                              D3D12_RESOURCE_STATE_RENDER_TARGET);
      DData.ResData->ResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    pList->OMSetRenderTargets(rtvHandleCount, rtvHandles, FALSE, nullptr);

    const float ClearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    pList->ClearRenderTargetView(rtvHandles[0], ClearColor, 0, nullptr);
    pList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);// TODO: set from m_pShaderOp

    // TODO: set all of this from m_pShaderOp.
    ShaderOpResourceData &VBufferData = this->m_ResourceData[m_pShaderOp->Strings.insert("VBuffer")];

    // Calculate the stride in bytes from the inputs, assuming linear & contiguous.
    UINT strideInBytes = 0;
    for (auto && IE : m_pShaderOp->InputElements) {
      strideInBytes += GetByteSizeForFormat(IE.Format);
    }

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    vertexBufferView.BufferLocation = VBufferData.Resource->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = strideInBytes;
    vertexBufferView.SizeInBytes = VBufferData.ShaderOpRes->Desc.Width;
    pList->IASetVertexBuffers(0, 1, &vertexBufferView);
    UINT vertexCount = vertexBufferView.SizeInBytes / vertexBufferView.StrideInBytes;
    UINT instanceCount = 1;
    UINT vertexCountPerInstance = vertexCount / instanceCount;

    pList->BeginQuery(m_pQueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    pList->DrawInstanced(vertexCountPerInstance, instanceCount, 0, 0);
    pList->EndQuery(m_pQueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    pList->ResolveQueryData(m_pQueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
                            0, 1, m_pQueryBuffer, 0);
  }
  CHECK_HR(pList->Close());
  ExecuteCommandList(m_CommandList.Queue, pList);
  WaitForSignal(m_CommandList.Queue, m_pFence, m_hFence, m_FenceValue++);
}

void ShaderOpTest::RunShaderOp(ShaderOp *pShaderOp) {
  m_pShaderOp = pShaderOp;

  CreateDevice();
  CreateResources();
  CreateDescriptorHeaps();
  CreatePipelineState();
  CreateCommandList();
  RunCommandList();
  CopyBackResources();
}

void ShaderOpTest::RunShaderOp(std::shared_ptr<ShaderOp> ShaderOp) {
  m_OrigShaderOp = ShaderOp;
  RunShaderOp(m_OrigShaderOp.get());
}

void ShaderOpTest::SetRootValues(ID3D12GraphicsCommandList *pList,
  bool isCompute) {
  for (size_t i = 0; i < m_pShaderOp->RootValues.size(); ++i) {
    ShaderOpRootValue &V = m_pShaderOp->RootValues[i];
    UINT idx = V.Index == 0 ? (UINT)i : V.Index;
    if (V.ResName) {
      auto r_it = m_ResourceData.find(V.ResName);
      if (r_it == m_ResourceData.end()) {
        ShaderOpLogFmt(L"Root value #%u refers to missing resource %S", (unsigned)i, V.ResName);
        CHECK_HR(E_INVALIDARG);
      }
      // Issue a warning for trying to bind textures (GPU address will return null)
      ShaderOpResourceData &D = r_it->second;
      ID3D12Resource *pRes = D.Resource;
      if (isCompute) {
        switch (D.ShaderOpRes->TransitionTo) {
        case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:
          pList->SetComputeRootConstantBufferView(idx,
            pRes->GetGPUVirtualAddress());
          break;
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
          pList->SetComputeRootUnorderedAccessView(idx,
            pRes->GetGPUVirtualAddress());
          break;
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
        default:
          pList->SetComputeRootShaderResourceView(idx,
            pRes->GetGPUVirtualAddress());
          break;
        }
      }
      else {
        switch (D.ShaderOpRes->TransitionTo) {
        case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:
          pList->SetGraphicsRootConstantBufferView(idx,
            pRes->GetGPUVirtualAddress());
          break;
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
          pList->SetGraphicsRootUnorderedAccessView(idx,
            pRes->GetGPUVirtualAddress());
          break;
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
        default:
          pList->SetGraphicsRootShaderResourceView(idx,
            pRes->GetGPUVirtualAddress());
          break;
        }
      }
    }
    else if (V.HeapName) {
      D3D12_GPU_DESCRIPTOR_HANDLE heapBase(m_DescriptorHeapsByName[V.HeapName]->GetGPUDescriptorHandleForHeapStart());
      if (isCompute) {
        pList->SetComputeRootDescriptorTable(idx, heapBase);
      }
      else {
        pList->SetGraphicsRootDescriptorTable(idx, heapBase);
      }
    }
  }
}

void ShaderOpTest::SetDxcSupport(dxc::DxcDllSupport *pDxcSupport) {
  m_pDxcSupport = pDxcSupport;
}

void ShaderOpTest::SetInitCallback(TInitCallbackFn InitCallbackFn) {
  m_InitCallbackFn = InitCallbackFn;
}

void ShaderOpTest::SetupRenderTarget(ShaderOp *pShaderOp, ID3D12Device *pDevice,
                                     ID3D12CommandQueue *pCommandQueue,
                                     ID3D12Resource *pRenderTarget) {
  m_pDevice = pDevice;
  m_CommandList.Queue = pCommandQueue;
  // Simplification - add the render target name if missing, set it up 'by hand' if not.
  if (pShaderOp->RenderTargets.empty()) {
    pShaderOp->RenderTargets.push_back(pShaderOp->Strings.insert("RTarget"));
    ShaderOpResource R;
    ZeroMemory(&R, sizeof(R));
    R.Desc = pRenderTarget->GetDesc();
    R.Name = pShaderOp->Strings.insert("RTarget");
    R.HeapFlags = D3D12_HEAP_FLAG_NONE;
    R.Init = nullptr;
    R.InitialResourceState = D3D12_RESOURCE_STATE_PRESENT;
    R.ReadBack = FALSE;
    pShaderOp->Resources.push_back(R);

    ShaderOpResourceData &D = m_ResourceData[R.Name];
    D.ShaderOpRes = &pShaderOp->Resources.back();
    D.Resource = pRenderTarget;
    D.ResourceState = R.InitialResourceState;
  }
  // Create a render target heap to put this in.
  ShaderOpDescriptorHeap *pRtvHeap = pShaderOp->GetDescriptorHeapByName("RtvHeap");
  if (pRtvHeap == nullptr) {
    ShaderOpDescriptorHeap H;
    ZeroMemory(&H, sizeof(H));
    H.Name = pShaderOp->Strings.insert("RtvHeap");
    H.Desc.NumDescriptors = 1;
    H.Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    pShaderOp->DescriptorHeaps.push_back(H);
    pRtvHeap = &pShaderOp->DescriptorHeaps.back();
  }
  if (pRtvHeap->Descriptors.empty()) {
    ShaderOpDescriptor D;
    ZeroMemory(&D, sizeof(D));
    D.Name = pShaderOp->Strings.insert("RTarget");
    D.ResName = D.Name;
    D.Kind = pShaderOp->Strings.insert("RTV");
    pRtvHeap->Descriptors.push_back(D);
  }
}

void ShaderOpTest::PresentRenderTarget(ShaderOp *pShaderOp,
                                       ID3D12CommandQueue *pCommandQueue,
                                       ID3D12Resource *pRenderTarget) {
  CommandListRefs ResCommandList;
  ResCommandList.Queue = pCommandQueue;
  ResCommandList.CreateForDevice(m_pDevice, m_pShaderOp->IsCompute());
  ID3D12GraphicsCommandList *pList = ResCommandList.List;

  pList->SetName(L"ShaderOpTest Resource Present CommandList");
  RecordTransitionBarrier(pList, pRenderTarget,
                          D3D12_RESOURCE_STATE_RENDER_TARGET,
                          D3D12_RESOURCE_STATE_PRESENT);
  pList->Close();
  ExecuteCommandList(ResCommandList.Queue, pList);
  WaitForSignal(ResCommandList.Queue, m_pFence, m_hFence, m_FenceValue++);
}

ShaderOp *ShaderOpSet::GetShaderOp(LPCSTR pName) {
  for (ShaderOp &S : ShaderOps) {
    if (S.Name && 0 == _stricmp(pName, S.Name)) {
      return &S;
    }
  }
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// ShaderOpTest library implementation for deserialization.

#pragma region Parsing support

// Use this class to initialize a ShaderOp object from an XML document.
class ShaderOpParser {
private:
  string_table *m_pStrings;
  bool ReadAtElementName(IXmlReader *pReader, LPCWSTR pName);
  HRESULT ReadAttrStr(IXmlReader *pReader, LPCWSTR pAttrName, LPCSTR *ppValue);
  HRESULT ReadAttrBOOL(IXmlReader *pReader, LPCWSTR pAttrName, BOOL *pValue, BOOL defaultValue = FALSE);
  HRESULT ReadAttrUINT64(IXmlReader *pReader, LPCWSTR pAttrName, UINT64 *pValue, UINT64 defaultValue = 0);
  HRESULT ReadAttrUINT16(IXmlReader *pReader, LPCWSTR pAttrName, UINT16 *pValue, UINT16 defaultValue = 0);
  HRESULT ReadAttrUINT(IXmlReader *pReader, LPCWSTR pAttrName, UINT *pValue, UINT defaultValue = 0);
  void ReadElementContentStr(IXmlReader *pReader, LPCSTR *ppValue);
  void ParseDescriptor(IXmlReader *pReader, ShaderOpDescriptor *pDesc);
  void ParseDescriptorHeap(IXmlReader *pReader, ShaderOpDescriptorHeap *pHeap);
  void ParseInputElement(IXmlReader *pReader, D3D12_INPUT_ELEMENT_DESC *pInputElement);
  void ParseInputElements(IXmlReader *pReader, std::vector<D3D12_INPUT_ELEMENT_DESC> *pInputElements);
  void ParseRenderTargets(IXmlReader *pReader, std::vector<LPCSTR> *pRenderTargets);
  void ParseRootValue(IXmlReader *pReader, ShaderOpRootValue *pRootValue);
  void ParseRootValues(IXmlReader *pReader, std::vector<ShaderOpRootValue> *pRootValues);
  void ParseResource(IXmlReader *pReader, ShaderOpResource *pResource);
  void ParseShader(IXmlReader *pReader, ShaderOpShader *pShader);

public:
  void ParseShaderOpSet(IStream *pStream, ShaderOpSet *pShaderOpSet);
  void ParseShaderOpSet(IXmlReader *pReader, ShaderOpSet *pShaderOpSet);
  void ParseShaderOp(IXmlReader *pReader, ShaderOp *pShaderOp);
};

void ParseShaderOpSetFromStream(IStream *pStream, st::ShaderOpSet *pShaderOpSet) {
  ShaderOpParser parser;
  parser.ParseShaderOpSet(pStream, pShaderOpSet);
}

void ParseShaderOpSetFromXml(IXmlReader *pReader, st::ShaderOpSet *pShaderOpSet) {
  ShaderOpParser parser;
  parser.ParseShaderOpSet(pReader, pShaderOpSet);
}

enum class ParserEnumKind {
  INPUT_CLASSIFICATION,
  DXGI_FORMAT,
  HEAP_TYPE,
  CPU_PAGE_PROPERTY,
  MEMORY_POOL,
  RESOURCE_DIMENSION,
  TEXTURE_LAYOUT,
  RESOURCE_FLAG,
  HEAP_FLAG,
  RESOURCE_STATE,
  DESCRIPTOR_HEAP_TYPE,
  DESCRIPTOR_HEAP_FLAG,
  UAV_DIMENSION
};

struct ParserEnumValue {
  LPCWSTR Name;
  UINT    Value;
};

struct ParserEnumTable {
  size_t          ValueCount;
  const ParserEnumValue *Values;
  ParserEnumKind  Kind;
};

static const ParserEnumValue INPUT_CLASSIFICATION_TABLE[] = {
  { L"INSTANCE", D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA },
  { L"VERTEX", D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA }
};

static const ParserEnumValue DXGI_FORMAT_TABLE[] = {
  { L"UNKNOWN", DXGI_FORMAT_UNKNOWN },
  { L"R32G32B32A32_TYPELESS", DXGI_FORMAT_R32G32B32A32_TYPELESS },
  { L"R32G32B32A32_FLOAT", DXGI_FORMAT_R32G32B32A32_FLOAT },
  { L"R32G32B32A32_UINT", DXGI_FORMAT_R32G32B32A32_UINT },
  { L"R32G32B32A32_SINT", DXGI_FORMAT_R32G32B32A32_SINT },
  { L"R32G32B32_TYPELESS", DXGI_FORMAT_R32G32B32_TYPELESS },
  { L"R32G32B32_FLOAT", DXGI_FORMAT_R32G32B32_FLOAT },
  { L"R32G32B32_UINT", DXGI_FORMAT_R32G32B32_UINT },
  { L"R32G32B32_SINT", DXGI_FORMAT_R32G32B32_SINT },
  { L"R16G16B16A16_TYPELESS", DXGI_FORMAT_R16G16B16A16_TYPELESS },
  { L"R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT },
  { L"R16G16B16A16_UNORM", DXGI_FORMAT_R16G16B16A16_UNORM },
  { L"R16G16B16A16_UINT", DXGI_FORMAT_R16G16B16A16_UINT },
  { L"R16G16B16A16_SNORM", DXGI_FORMAT_R16G16B16A16_SNORM },
  { L"R16G16B16A16_SINT", DXGI_FORMAT_R16G16B16A16_SINT },
  { L"R32G32_TYPELESS", DXGI_FORMAT_R32G32_TYPELESS },
  { L"R32G32_FLOAT", DXGI_FORMAT_R32G32_FLOAT },
  { L"R32G32_UINT", DXGI_FORMAT_R32G32_UINT },
  { L"R32G32_SINT", DXGI_FORMAT_R32G32_SINT },
  { L"R32G8X24_TYPELESS", DXGI_FORMAT_R32G8X24_TYPELESS },
  { L"D32_FLOAT_S8X24_UINT", DXGI_FORMAT_D32_FLOAT_S8X24_UINT },
  { L"R32_FLOAT_X8X24_TYPELESS", DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS },
  { L"X32_TYPELESS_G8X24_UINT", DXGI_FORMAT_X32_TYPELESS_G8X24_UINT },
  { L"R10G10B10A2_TYPELESS", DXGI_FORMAT_R10G10B10A2_TYPELESS },
  { L"R10G10B10A2_UNORM", DXGI_FORMAT_R10G10B10A2_UNORM },
  { L"R10G10B10A2_UINT", DXGI_FORMAT_R10G10B10A2_UINT },
  { L"R11G11B10_FLOAT", DXGI_FORMAT_R11G11B10_FLOAT },
  { L"R8G8B8A8_TYPELESS", DXGI_FORMAT_R8G8B8A8_TYPELESS },
  { L"R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM },
  { L"R8G8B8A8_UNORM_SRGB", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
  { L"R8G8B8A8_UINT", DXGI_FORMAT_R8G8B8A8_UINT },
  { L"R8G8B8A8_SNORM", DXGI_FORMAT_R8G8B8A8_SNORM },
  { L"R8G8B8A8_SINT", DXGI_FORMAT_R8G8B8A8_SINT },
  { L"R16G16_TYPELESS", DXGI_FORMAT_R16G16_TYPELESS },
  { L"R16G16_FLOAT", DXGI_FORMAT_R16G16_FLOAT },
  { L"R16G16_UNORM", DXGI_FORMAT_R16G16_UNORM },
  { L"R16G16_UINT", DXGI_FORMAT_R16G16_UINT },
  { L"R16G16_SNORM", DXGI_FORMAT_R16G16_SNORM },
  { L"R16G16_SINT", DXGI_FORMAT_R16G16_SINT },
  { L"R32_TYPELESS", DXGI_FORMAT_R32_TYPELESS },
  { L"D32_FLOAT", DXGI_FORMAT_D32_FLOAT },
  { L"R32_FLOAT", DXGI_FORMAT_R32_FLOAT },
  { L"R32_UINT", DXGI_FORMAT_R32_UINT },
  { L"R32_SINT", DXGI_FORMAT_R32_SINT },
  { L"R24G8_TYPELESS", DXGI_FORMAT_R24G8_TYPELESS },
  { L"D24_UNORM_S8_UINT", DXGI_FORMAT_D24_UNORM_S8_UINT },
  { L"R24_UNORM_X8_TYPELESS", DXGI_FORMAT_R24_UNORM_X8_TYPELESS },
  { L"X24_TYPELESS_G8_UINT", DXGI_FORMAT_X24_TYPELESS_G8_UINT },
  { L"R8G8_TYPELESS", DXGI_FORMAT_R8G8_TYPELESS },
  { L"R8G8_UNORM", DXGI_FORMAT_R8G8_UNORM },
  { L"R8G8_UINT", DXGI_FORMAT_R8G8_UINT },
  { L"R8G8_SNORM", DXGI_FORMAT_R8G8_SNORM },
  { L"R8G8_SINT", DXGI_FORMAT_R8G8_SINT },
  { L"R16_TYPELESS", DXGI_FORMAT_R16_TYPELESS },
  { L"R16_FLOAT", DXGI_FORMAT_R16_FLOAT },
  { L"D16_UNORM", DXGI_FORMAT_D16_UNORM },
  { L"R16_UNORM", DXGI_FORMAT_R16_UNORM },
  { L"R16_UINT", DXGI_FORMAT_R16_UINT },
  { L"R16_SNORM", DXGI_FORMAT_R16_SNORM },
  { L"R16_SINT", DXGI_FORMAT_R16_SINT },
  { L"R8_TYPELESS", DXGI_FORMAT_R8_TYPELESS },
  { L"R8_UNORM", DXGI_FORMAT_R8_UNORM },
  { L"R8_UINT", DXGI_FORMAT_R8_UINT },
  { L"R8_SNORM", DXGI_FORMAT_R8_SNORM },
  { L"R8_SINT", DXGI_FORMAT_R8_SINT },
  { L"A8_UNORM", DXGI_FORMAT_A8_UNORM },
  { L"R1_UNORM", DXGI_FORMAT_R1_UNORM },
  { L"R9G9B9E5_SHAREDEXP", DXGI_FORMAT_R9G9B9E5_SHAREDEXP },
  { L"R8G8_B8G8_UNORM", DXGI_FORMAT_R8G8_B8G8_UNORM },
  { L"G8R8_G8B8_UNORM", DXGI_FORMAT_G8R8_G8B8_UNORM },
  { L"BC1_TYPELESS", DXGI_FORMAT_BC1_TYPELESS },
  { L"BC1_UNORM", DXGI_FORMAT_BC1_UNORM },
  { L"BC1_UNORM_SRGB", DXGI_FORMAT_BC1_UNORM_SRGB },
  { L"BC2_TYPELESS", DXGI_FORMAT_BC2_TYPELESS },
  { L"BC2_UNORM", DXGI_FORMAT_BC2_UNORM },
  { L"BC2_UNORM_SRGB", DXGI_FORMAT_BC2_UNORM_SRGB },
  { L"BC3_TYPELESS", DXGI_FORMAT_BC3_TYPELESS },
  { L"BC3_UNORM", DXGI_FORMAT_BC3_UNORM },
  { L"BC3_UNORM_SRGB", DXGI_FORMAT_BC3_UNORM_SRGB },
  { L"BC4_TYPELESS", DXGI_FORMAT_BC4_TYPELESS },
  { L"BC4_UNORM", DXGI_FORMAT_BC4_UNORM },
  { L"BC4_SNORM", DXGI_FORMAT_BC4_SNORM },
  { L"BC5_TYPELESS", DXGI_FORMAT_BC5_TYPELESS },
  { L"BC5_UNORM", DXGI_FORMAT_BC5_UNORM },
  { L"BC5_SNORM", DXGI_FORMAT_BC5_SNORM },
  { L"B5G6R5_UNORM", DXGI_FORMAT_B5G6R5_UNORM },
  { L"B5G5R5A1_UNORM", DXGI_FORMAT_B5G5R5A1_UNORM },
  { L"B8G8R8A8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM },
  { L"B8G8R8X8_UNORM", DXGI_FORMAT_B8G8R8X8_UNORM },
  { L"R10G10B10_XR_BIAS_A2_UNORM", DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM },
  { L"B8G8R8A8_TYPELESS", DXGI_FORMAT_B8G8R8A8_TYPELESS },
  { L"B8G8R8A8_UNORM_SRGB", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB },
  { L"B8G8R8X8_TYPELESS", DXGI_FORMAT_B8G8R8X8_TYPELESS },
  { L"B8G8R8X8_UNORM_SRGB", DXGI_FORMAT_B8G8R8X8_UNORM_SRGB },
  { L"BC6H_TYPELESS", DXGI_FORMAT_BC6H_TYPELESS },
  { L"BC6H_UF16", DXGI_FORMAT_BC6H_UF16 },
  { L"BC6H_SF16", DXGI_FORMAT_BC6H_SF16 },
  { L"BC7_TYPELESS", DXGI_FORMAT_BC7_TYPELESS },
  { L"BC7_UNORM", DXGI_FORMAT_BC7_UNORM },
  { L"BC7_UNORM_SRGB", DXGI_FORMAT_BC7_UNORM_SRGB },
  { L"AYUV", DXGI_FORMAT_AYUV },
  { L"Y410", DXGI_FORMAT_Y410 },
  { L"Y416", DXGI_FORMAT_Y416 },
  { L"NV12", DXGI_FORMAT_NV12 },
  { L"P010", DXGI_FORMAT_P010 },
  { L"P016", DXGI_FORMAT_P016 },
  { L"420_OPAQUE", DXGI_FORMAT_420_OPAQUE },
  { L"YUY2", DXGI_FORMAT_YUY2 },
  { L"Y210", DXGI_FORMAT_Y210 },
  { L"Y216", DXGI_FORMAT_Y216 },
  { L"NV11", DXGI_FORMAT_NV11 },
  { L"AI44", DXGI_FORMAT_AI44 },
  { L"IA44", DXGI_FORMAT_IA44 },
  { L"P8", DXGI_FORMAT_P8 },
  { L"A8P8", DXGI_FORMAT_A8P8 },
  { L"B4G4R4A4_UNORM", DXGI_FORMAT_B4G4R4A4_UNORM },
  { L"P208", DXGI_FORMAT_P208 },
  { L"V208", DXGI_FORMAT_V208 },
  { L"V408", DXGI_FORMAT_V408 }
};

static const ParserEnumValue HEAP_TYPE_TABLE[] = {
  { L"DEFAULT", D3D12_HEAP_TYPE_DEFAULT },
  { L"UPLOAD", D3D12_HEAP_TYPE_UPLOAD },
  { L"READBACK", D3D12_HEAP_TYPE_READBACK },
  { L"CUSTOM", D3D12_HEAP_TYPE_CUSTOM }
};

static const ParserEnumValue CPU_PAGE_PROPERTY_TABLE[] = {
  { L"UNKNOWN",       D3D12_CPU_PAGE_PROPERTY_UNKNOWN },
  { L"NOT_AVAILABLE", D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE },
  { L"WRITE_COMBINE", D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE },
  { L"WRITE_BACK",    D3D12_CPU_PAGE_PROPERTY_WRITE_BACK }
};

static const ParserEnumValue MEMORY_POOL_TABLE[] = {
  { L"UNKNOWN", D3D12_MEMORY_POOL_UNKNOWN },
  { L"L0 ",     D3D12_MEMORY_POOL_L0 },
  { L"L1",      D3D12_MEMORY_POOL_L1 }
};

static const ParserEnumValue RESOURCE_DIMENSION_TABLE[] = {
  { L"UNKNOWN",   D3D12_RESOURCE_DIMENSION_UNKNOWN },
  { L"BUFFER",    D3D12_RESOURCE_DIMENSION_BUFFER },
  { L"TEXTURE1D", D3D12_RESOURCE_DIMENSION_TEXTURE1D },
  { L"TEXTURE2D", D3D12_RESOURCE_DIMENSION_TEXTURE2D },
  { L"TEXTURE3D", D3D12_RESOURCE_DIMENSION_TEXTURE3D }
};

static const ParserEnumValue TEXTURE_LAYOUT_TABLE[] = {
  { L"UNKNOWN",           D3D12_TEXTURE_LAYOUT_UNKNOWN },
  { L"ROW_MAJOR",         D3D12_TEXTURE_LAYOUT_ROW_MAJOR },
  { L"UNDEFINED_SWIZZLE", D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE },
  { L"STANDARD_SWIZZLE",  D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE }
};

static const ParserEnumValue RESOURCE_FLAG_TABLE[] = {
  { L"NONE",                      D3D12_RESOURCE_FLAG_NONE },
  { L"ALLOW_RENDER_TARGET",       D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET },
  { L"ALLOW_DEPTH_STENCIL",       D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL },
  { L"ALLOW_UNORDERED_ACCESS",    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS },
  { L"DENY_SHADER_RESOURCE",      D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE },
  { L"ALLOW_CROSS_ADAPTER",       D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER },
  { L"ALLOW_SIMULTANEOUS_ACCESS", D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS }
};

static const ParserEnumValue HEAP_FLAG_TABLE[] = {
  { L"NONE",                          D3D12_HEAP_FLAG_NONE },
  { L"SHARED",                        D3D12_HEAP_FLAG_SHARED },
  { L"DENY_BUFFERS",                  D3D12_HEAP_FLAG_DENY_BUFFERS },
  { L"ALLOW_DISPLAY",                 D3D12_HEAP_FLAG_ALLOW_DISPLAY },
  { L"SHARED_CROSS_ADAPTER",          D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER },
  { L"DENY_RT_DS_TEXTURES",           D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES },
  { L"DENY_NON_RT_DS_TEXTURES",       D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES },
  { L"ALLOW_ALL_BUFFERS_AND_TEXTURES",D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES },
  { L"ALLOW_ONLY_BUFFERS",            D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS },
  { L"ALLOW_ONLY_NON_RT_DS_TEXTURES", D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES },
  { L"ALLOW_ONLY_RT_DS_TEXTURES",     D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES }
};

static const ParserEnumValue RESOURCE_STATE_TABLE[] = {
  { L"COMMON", D3D12_RESOURCE_STATE_COMMON },
  { L"VERTEX_AND_CONSTANT_BUFFER", D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER },
  { L"INDEX_BUFFER", D3D12_RESOURCE_STATE_INDEX_BUFFER },
  { L"RENDER_TARGET", D3D12_RESOURCE_STATE_RENDER_TARGET },
  { L"UNORDERED_ACCESS", D3D12_RESOURCE_STATE_UNORDERED_ACCESS },
  { L"DEPTH_WRITE", D3D12_RESOURCE_STATE_DEPTH_WRITE },
  { L"DEPTH_READ", D3D12_RESOURCE_STATE_DEPTH_READ },
  { L"NON_PIXEL_SHADER_RESOURCE", D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
  { L"PIXEL_SHADER_RESOURCE", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
  { L"STREAM_OUT", D3D12_RESOURCE_STATE_STREAM_OUT },
  { L"INDIRECT_ARGUMENT", D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT },
  { L"COPY_DEST", D3D12_RESOURCE_STATE_COPY_DEST },
  { L"COPY_SOURCE", D3D12_RESOURCE_STATE_COPY_SOURCE },
  { L"RESOLVE_DEST", D3D12_RESOURCE_STATE_RESOLVE_DEST },
  { L"RESOLVE_SOURCE", D3D12_RESOURCE_STATE_RESOLVE_SOURCE },
  { L"GENERIC_READ", D3D12_RESOURCE_STATE_GENERIC_READ },
  { L"PRESENT", D3D12_RESOURCE_STATE_PRESENT },
  { L"PREDICATION", D3D12_RESOURCE_STATE_PREDICATION }
};

static const ParserEnumValue DESCRIPTOR_HEAP_TYPE_TABLE[] = {
  { L"CBV_SRV_UAV", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV },
  { L"SAMPLER", D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER },
  { L"RTV", D3D12_DESCRIPTOR_HEAP_TYPE_RTV },
  { L"DSV", D3D12_DESCRIPTOR_HEAP_TYPE_DSV }
};

static const ParserEnumValue DESCRIPTOR_HEAP_FLAG_TABLE[] = {
  { L"NONE", D3D12_DESCRIPTOR_HEAP_FLAG_NONE },
  { L"SHADER_VISIBLE", D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE }
};

static const ParserEnumValue UAV_DIMENSION_TABLE[] = {
  { L"UNKNOWN", D3D12_UAV_DIMENSION_UNKNOWN },
  { L"BUFFER", D3D12_UAV_DIMENSION_BUFFER },
  { L"TEXTURE1D", D3D12_UAV_DIMENSION_TEXTURE1D },
  { L"TEXTURE1DARRAY", D3D12_UAV_DIMENSION_TEXTURE1DARRAY },
  { L"TEXTURE2D", D3D12_UAV_DIMENSION_TEXTURE2D },
  { L"TEXTURE2DARRAY", D3D12_UAV_DIMENSION_TEXTURE2DARRAY },
  { L"TEXTURE3D", D3D12_UAV_DIMENSION_TEXTURE3D }
};


static const ParserEnumTable g_ParserEnumTables[] = {
  { _countof(INPUT_CLASSIFICATION_TABLE), INPUT_CLASSIFICATION_TABLE, ParserEnumKind::INPUT_CLASSIFICATION },
  { _countof(DXGI_FORMAT_TABLE), DXGI_FORMAT_TABLE, ParserEnumKind::DXGI_FORMAT },
  { _countof(HEAP_TYPE_TABLE), HEAP_TYPE_TABLE, ParserEnumKind::HEAP_TYPE },
  { _countof(CPU_PAGE_PROPERTY_TABLE), CPU_PAGE_PROPERTY_TABLE, ParserEnumKind::CPU_PAGE_PROPERTY },
  { _countof(MEMORY_POOL_TABLE), MEMORY_POOL_TABLE, ParserEnumKind::MEMORY_POOL },
  { _countof(RESOURCE_DIMENSION_TABLE), RESOURCE_DIMENSION_TABLE, ParserEnumKind::RESOURCE_DIMENSION },
  { _countof(TEXTURE_LAYOUT_TABLE), TEXTURE_LAYOUT_TABLE, ParserEnumKind::TEXTURE_LAYOUT },
  { _countof(RESOURCE_FLAG_TABLE), RESOURCE_FLAG_TABLE, ParserEnumKind::RESOURCE_FLAG },
  { _countof(HEAP_FLAG_TABLE), HEAP_FLAG_TABLE, ParserEnumKind::HEAP_FLAG },
  { _countof(RESOURCE_STATE_TABLE), RESOURCE_STATE_TABLE, ParserEnumKind::RESOURCE_STATE },
  { _countof(DESCRIPTOR_HEAP_TYPE_TABLE), DESCRIPTOR_HEAP_TYPE_TABLE, ParserEnumKind::DESCRIPTOR_HEAP_TYPE },
  { _countof(DESCRIPTOR_HEAP_FLAG_TABLE), DESCRIPTOR_HEAP_FLAG_TABLE, ParserEnumKind::DESCRIPTOR_HEAP_FLAG },
  { _countof(UAV_DIMENSION_TABLE), UAV_DIMENSION_TABLE, ParserEnumKind::UAV_DIMENSION }
};

static HRESULT GetEnumValue(LPCWSTR name, ParserEnumKind K, UINT *pValue) {
  for (size_t i = 0; i < _countof(g_ParserEnumTables); ++i) {
    const ParserEnumTable &T = g_ParserEnumTables[i];
    if (T.Kind != K) {
      continue;
    }
    for (size_t j = 0; j < T.ValueCount; ++j) {
      if (_wcsicmp(name, T.Values[j].Name) == 0) {
        *pValue = T.Values[j].Value;
        return S_OK;
      }
    }
  }
  return E_INVALIDARG;
}

template <typename T>
static HRESULT GetEnumValueT(LPCWSTR name, ParserEnumKind K, T *pValue) {
  UINT u;
  HRESULT hr = GetEnumValue(name, K, &u);
  *pValue = (T)u;
  return hr;
}

template <typename T>
static HRESULT ReadAttrEnumT(IXmlReader *pReader, LPCWSTR pAttrName, ParserEnumKind K, T *pValue, T defaultValue, LPCWSTR pStripPrefix = nullptr) {
  if (S_FALSE == CHECK_HR_RET(pReader->MoveToAttributeByName(pAttrName, nullptr))) {
    *pValue = defaultValue;
    return S_FALSE;
  }
  LPCWSTR pText;
  CHECK_HR(pReader->GetValue(&pText, nullptr));
  if (pStripPrefix && *pStripPrefix && _wcsnicmp(pAttrName, pText, wcslen(pStripPrefix)) == 0)
    pText += wcslen(pStripPrefix);
  CHECK_HR(GetEnumValueT(pText, K, pValue));
  CHECK_HR(pReader->MoveToElement());
  return S_OK;
}

static HRESULT ReadAttrINPUT_CLASSIFICATION(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_INPUT_CLASSIFICATION *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::INPUT_CLASSIFICATION, pValue, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA);
}

static HRESULT ReadAttrDESCRIPTOR_HEAP_TYPE(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_DESCRIPTOR_HEAP_TYPE *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::DESCRIPTOR_HEAP_TYPE, pValue, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

static HRESULT ReadAttrDESCRIPTOR_HEAP_FLAGS(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_DESCRIPTOR_HEAP_FLAGS *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::DESCRIPTOR_HEAP_FLAG, pValue, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
}

static HRESULT ReadAttrDXGI_FORMAT(IXmlReader *pReader, LPCWSTR pAttrName, DXGI_FORMAT *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::DXGI_FORMAT, pValue, DXGI_FORMAT_UNKNOWN, L"DXGI_FORMAT_");
}

static HRESULT ReadAttrHEAP_TYPE(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_HEAP_TYPE *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::HEAP_TYPE, pValue, D3D12_HEAP_TYPE_DEFAULT);
}

static HRESULT ReadAttrCPU_PAGE_PROPERTY(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_CPU_PAGE_PROPERTY *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::CPU_PAGE_PROPERTY, pValue, D3D12_CPU_PAGE_PROPERTY_UNKNOWN);
}

static HRESULT ReadAttrMEMORY_POOL(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_MEMORY_POOL *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::MEMORY_POOL, pValue, D3D12_MEMORY_POOL_UNKNOWN);
}

static HRESULT ReadAttrRESOURCE_DIMENSION(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_RESOURCE_DIMENSION *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::RESOURCE_DIMENSION, pValue, D3D12_RESOURCE_DIMENSION_BUFFER);
}

static HRESULT ReadAttrTEXTURE_LAYOUT(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_TEXTURE_LAYOUT *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::TEXTURE_LAYOUT, pValue, D3D12_TEXTURE_LAYOUT_UNKNOWN);
}

static HRESULT ReadAttrRESOURCE_FLAGS(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_RESOURCE_FLAGS *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::RESOURCE_FLAG, pValue, D3D12_RESOURCE_FLAG_NONE);
}

static HRESULT ReadAttrHEAP_FLAGS(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_HEAP_FLAGS *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::HEAP_FLAG, pValue, D3D12_HEAP_FLAG_NONE);
}

static HRESULT ReadAttrRESOURCE_STATES(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_RESOURCE_STATES *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::RESOURCE_STATE, pValue, D3D12_RESOURCE_STATE_COMMON);
}

static HRESULT ReadAttrUAV_DIMENSION(IXmlReader *pReader, LPCWSTR pAttrName, D3D12_UAV_DIMENSION *pValue) {
  return ReadAttrEnumT(pReader, pAttrName, ParserEnumKind::UAV_DIMENSION, pValue, D3D12_UAV_DIMENSION_BUFFER);
}

HRESULT ShaderOpParser::ReadAttrStr(IXmlReader *pReader, LPCWSTR pAttrName, LPCSTR *ppValue) {
  if (S_FALSE == CHECK_HR_RET(pReader->MoveToAttributeByName(pAttrName, nullptr))) {
    *ppValue = nullptr;
    return S_FALSE;
  }
  LPCWSTR pValue;
  CHECK_HR(pReader->GetValue(&pValue, nullptr));
  *ppValue = m_pStrings->insert(pValue);
  CHECK_HR(pReader->MoveToElement());
  return S_OK;
}

HRESULT ShaderOpParser::ReadAttrBOOL(IXmlReader *pReader, LPCWSTR pAttrName, BOOL *pValue, BOOL defaultValue) {
  if (S_FALSE == CHECK_HR_RET(pReader->MoveToAttributeByName(pAttrName, nullptr))) {
    *pValue = defaultValue;
    return S_FALSE;
  }
  LPCWSTR pText;
  CHECK_HR(pReader->GetValue(&pText, nullptr));
  if (_wcsicmp(pText, L"true") == 0) {
    *pValue = TRUE;
  }
  else {
    *pValue = FALSE;
  }
  CHECK_HR(pReader->MoveToElement());
  return S_OK;
}

HRESULT ShaderOpParser::ReadAttrUINT64(IXmlReader *pReader, LPCWSTR pAttrName, UINT64 *pValue, UINT64 defaultValue) {
  if (S_FALSE == CHECK_HR_RET(pReader->MoveToAttributeByName(pAttrName, nullptr))) {
    *pValue = defaultValue;
    return S_FALSE;
  }
  LPCWSTR pText;
  CHECK_HR(pReader->GetValue(&pText, nullptr));
  long long ll = _wtol(pText);
  if (errno == ERANGE) CHECK_HR(E_INVALIDARG);
  *pValue = ll;
  CHECK_HR(pReader->MoveToElement());
  return S_OK;
}

HRESULT ShaderOpParser::ReadAttrUINT(IXmlReader *pReader, LPCWSTR pAttrName, UINT *pValue, UINT defaultValue) {
  UINT64 u64;
  CHECK_HR(ReadAttrUINT64(pReader, pAttrName, &u64, defaultValue));
  CHECK_HR(UInt64ToUInt(u64, pValue));
  return S_OK;
}

HRESULT ShaderOpParser::ReadAttrUINT16(IXmlReader *pReader, LPCWSTR pAttrName, UINT16 *pValue, UINT16 defaultValue) {
  UINT64 u64;
  CHECK_HR(ReadAttrUINT64(pReader, pAttrName, &u64, defaultValue));
  CHECK_HR(UInt64ToUInt16(u64, pValue));
  return S_OK;
}

void ShaderOpParser::ReadElementContentStr(IXmlReader *pReader, LPCSTR *ppValue) {
  *ppValue = nullptr;
  if (pReader->IsEmptyElement())
    return;
  UINT startDepth;
  XmlNodeType nt;
  CHECK_HR(pReader->GetDepth(&startDepth));
  std::wstring value;
  for (;;) {
    UINT depth;
    CHECK_HR(pReader->Read(&nt));
    CHECK_HR(pReader->GetDepth(&depth));
    if (nt == XmlNodeType_EndElement && depth == startDepth + 1)
      break;
    if (nt == XmlNodeType_CDATA || nt == XmlNodeType_Text || nt == XmlNodeType_Whitespace) {
      LPCWSTR pText;
      CHECK_HR(pReader->GetValue(&pText, nullptr));
      value += pText;
    }
  }
  *ppValue = m_pStrings->insert(value.c_str());
}

void ShaderOpParser::ParseDescriptor(IXmlReader *pReader, ShaderOpDescriptor *pDesc) {
  if (!ReadAtElementName(pReader, L"Descriptor"))
    return;
  CHECK_HR(ReadAttrStr(pReader, L"Name", &pDesc->Name));
  CHECK_HR(ReadAttrStr(pReader, L"ResName", &pDesc->ResName));
  CHECK_HR(ReadAttrStr(pReader, L"CounterName", &pDesc->CounterName));
  CHECK_HR(ReadAttrStr(pReader, L"Kind", &pDesc->Kind));
  // D3D12_UNORDERED_ACCESS_VIEW_DESC
  HRESULT hrFormat = ReadAttrDXGI_FORMAT(pReader, L"Format", &pDesc->UavDesc.Format);
  CHECK_HR(hrFormat);
  CHECK_HR(ReadAttrUAV_DIMENSION(pReader, L"Dimension", &pDesc->UavDesc.ViewDimension));
  switch (pDesc->UavDesc.ViewDimension) {
  case D3D12_UAV_DIMENSION_BUFFER:
    CHECK_HR(ReadAttrUINT64(pReader, L"FirstElement", &pDesc->UavDesc.Buffer.FirstElement));
    CHECK_HR(ReadAttrUINT(pReader, L"NumElements", &pDesc->UavDesc.Buffer.NumElements));
    CHECK_HR(ReadAttrUINT(pReader, L"StructureByteStride", &pDesc->UavDesc.Buffer.StructureByteStride));
    CHECK_HR(ReadAttrUINT64(pReader, L"CounterOffsetInBytes", &pDesc->UavDesc.Buffer.CounterOffsetInBytes));
    LPCSTR pFlags;
    CHECK_HR(ReadAttrStr(pReader, L"Flags", &pFlags));
    if (pFlags && *pFlags && 0 == _stricmp(pFlags, "RAW")) {
      pDesc->UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    }
    else {
      pDesc->UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    }
    if (hrFormat == S_FALSE && pDesc->UavDesc.Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) {
      pDesc->UavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    }
    break;
  case D3D12_UAV_DIMENSION_TEXTURE1D:
    CHECK_HR(ReadAttrUINT(pReader, L"MipSlice", &pDesc->UavDesc.Texture1D.MipSlice));
    break;
  case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
    CHECK_HR(ReadAttrUINT(pReader, L"MipSlice", &pDesc->UavDesc.Texture1DArray.MipSlice));
    CHECK_HR(ReadAttrUINT(pReader, L"FirstArraySlice", &pDesc->UavDesc.Texture1DArray.FirstArraySlice));
    CHECK_HR(ReadAttrUINT(pReader, L"ArraySize", &pDesc->UavDesc.Texture1DArray.ArraySize));
    break;
  case D3D12_UAV_DIMENSION_TEXTURE2D:
    CHECK_HR(ReadAttrUINT(pReader, L"MipSlice", &pDesc->UavDesc.Texture2D.MipSlice));
    CHECK_HR(ReadAttrUINT(pReader, L"PlaneSlice", &pDesc->UavDesc.Texture2D.PlaneSlice));
    break;
  case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
    CHECK_HR(ReadAttrUINT(pReader, L"MipSlice", &pDesc->UavDesc.Texture2DArray.MipSlice));
    CHECK_HR(ReadAttrUINT(pReader, L"FirstArraySlice", &pDesc->UavDesc.Texture2DArray.FirstArraySlice));
    CHECK_HR(ReadAttrUINT(pReader, L"ArraySize", &pDesc->UavDesc.Texture2DArray.ArraySize));
    CHECK_HR(ReadAttrUINT(pReader, L"MipSlice", &pDesc->UavDesc.Texture2DArray.PlaneSlice));
    break;
  case D3D12_UAV_DIMENSION_TEXTURE3D:
    CHECK_HR(ReadAttrUINT(pReader, L"MipSlice", &pDesc->UavDesc.Texture3D.MipSlice));
    CHECK_HR(ReadAttrUINT(pReader, L"FirstWSlice", &pDesc->UavDesc.Texture3D.FirstWSlice));
    CHECK_HR(ReadAttrUINT(pReader, L"WSize", &pDesc->UavDesc.Texture3D.WSize));
    break;
  }

  // If either is missing, set one from the other.
  if (pDesc->Name && !pDesc->ResName) pDesc->ResName = pDesc->Name;
  if (pDesc->ResName && !pDesc->Name) pDesc->Name = pDesc->ResName;
  LPCSTR K = pDesc->Kind;
  if (K == nullptr) {
    ShaderOpLogFmt(L"Descriptor '%S' is missing Kind attribute.", pDesc->Name);
    CHECK_HR(E_INVALIDARG);
  } else if (0 != _stricmp(K, "UAV") && 0 != _stricmp(K, "SRV") &&
             0 != _stricmp(K, "CBV") && 0 != _stricmp(K, "RTV")) {
    ShaderOpLogFmt(L"Descriptor '%S' references unknown kind '%S'",
                   pDesc->Name, K);
    CHECK_HR(E_INVALIDARG);
  }
}

void ShaderOpParser::ParseDescriptorHeap(IXmlReader *pReader, ShaderOpDescriptorHeap *pHeap) {
  if (!ReadAtElementName(pReader, L"DescriptorHeap"))
    return;
  CHECK_HR(ReadAttrStr(pReader, L"Name", &pHeap->Name));
  HRESULT hrFlags = ReadAttrDESCRIPTOR_HEAP_FLAGS(pReader, L"Flags", &pHeap->Desc.Flags);
  CHECK_HR(hrFlags);
  CHECK_HR(ReadAttrUINT(pReader, L"NodeMask", &pHeap->Desc.NodeMask));
  CHECK_HR(ReadAttrUINT(pReader, L"NumDescriptors", &pHeap->Desc.NumDescriptors));
  CHECK_HR(ReadAttrDESCRIPTOR_HEAP_TYPE(pReader, L"Type", &pHeap->Desc.Type));
  if (pHeap->Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV && hrFlags == S_FALSE)
    pHeap->Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (pReader->IsEmptyElement())
    return;

  UINT startDepth;
  XmlNodeType nt;
  CHECK_HR(pReader->GetDepth(&startDepth));
  std::wstring value;
  for (;;) {
    UINT depth;
    CHECK_HR(pReader->Read(&nt));
    CHECK_HR(pReader->GetDepth(&depth));
    if (nt == XmlNodeType_EndElement && depth == startDepth + 1)
      break;
    if (nt == XmlNodeType_Element) {
      LPCWSTR pLocalName;
      CHECK_HR(pReader->GetLocalName(&pLocalName, nullptr));
      if (0 == wcscmp(pLocalName, L"Descriptor")) {
        ShaderOpDescriptor D;
        ParseDescriptor(pReader, &D);
        pHeap->Descriptors.push_back(D);
      }
    }
  }
}

void ShaderOpParser::ParseInputElement(IXmlReader *pReader, D3D12_INPUT_ELEMENT_DESC *pInputElement) {
  if (!ReadAtElementName(pReader, L"InputElement"))
    return;
  CHECK_HR(ReadAttrStr(pReader, L"SemanticName", &pInputElement->SemanticName));
  CHECK_HR(ReadAttrUINT(pReader, L"SemanticIndex", &pInputElement->SemanticIndex));
  CHECK_HR(ReadAttrDXGI_FORMAT(pReader, L"Format", &pInputElement->Format));
  CHECK_HR(ReadAttrUINT(pReader, L"InputSlot", &pInputElement->InputSlot));
  CHECK_HR(ReadAttrUINT(pReader, L"AlignedByteOffset", &pInputElement->AlignedByteOffset, D3D12_APPEND_ALIGNED_ELEMENT));
  CHECK_HR(ReadAttrINPUT_CLASSIFICATION(pReader, L"InputSlotClass", &pInputElement->InputSlotClass));
  CHECK_HR(ReadAttrUINT(pReader, L"InstanceDataStepRate", &pInputElement->InstanceDataStepRate));
}

void ShaderOpParser::ParseInputElements(IXmlReader *pReader, std::vector<D3D12_INPUT_ELEMENT_DESC> *pInputElements) {
  if (!ReadAtElementName(pReader, L"InputElements"))
    return;

  if (pReader->IsEmptyElement()) return;

  UINT startDepth;
  XmlNodeType nt;
  CHECK_HR(pReader->GetDepth(&startDepth));
  for (;;) {
    UINT depth;
    CHECK_HR(pReader->Read(&nt));
    CHECK_HR(pReader->GetDepth(&depth));
    if (nt == XmlNodeType_EndElement && depth == startDepth + 1)
      return;
    if (nt == XmlNodeType_Element) {
      LPCWSTR pLocalName;
      CHECK_HR(pReader->GetLocalName(&pLocalName, nullptr));
      if (0 == wcscmp(pLocalName, L"InputElement")) {
        D3D12_INPUT_ELEMENT_DESC desc;
        ParseInputElement(pReader, &desc);
        pInputElements->push_back(desc);
      }
    }
  }
}

void ShaderOpParser::ParseRenderTargets(IXmlReader *pReader, std::vector<LPCSTR> *pRenderTargets) {
  if (!ReadAtElementName(pReader, L"RenderTargets"))
    return;
  if (pReader->IsEmptyElement()) return;

  UINT startDepth;
  XmlNodeType nt;
  CHECK_HR(pReader->GetDepth(&startDepth));
  for (;;) {
    UINT depth;
    CHECK_HR(pReader->Read(&nt));
    CHECK_HR(pReader->GetDepth(&depth));
    if (nt == XmlNodeType_EndElement && depth == startDepth + 1)
      return;
    if (nt == XmlNodeType_Element) {
      LPCWSTR pLocalName;
      CHECK_HR(pReader->GetLocalName(&pLocalName, nullptr));
      if (0 == wcscmp(pLocalName, L"RenderTarget")) {
        LPCSTR pName;
        CHECK_HR(ReadAttrStr(pReader, L"Name", &pName));
        pRenderTargets->push_back(pName);
      }
    }
  }
}

void ShaderOpParser::ParseRootValue(IXmlReader *pReader, ShaderOpRootValue *pRootValue) {
  if (!ReadAtElementName(pReader, L"RootValue"))
    return;
  CHECK_HR(ReadAttrStr(pReader, L"ResName", &pRootValue->ResName));
  CHECK_HR(ReadAttrStr(pReader, L"HeapName", &pRootValue->HeapName));
  CHECK_HR(ReadAttrUINT(pReader, L"Index", &pRootValue->Index));
}

void ShaderOpParser::ParseRootValues(IXmlReader *pReader, std::vector<ShaderOpRootValue> *pRootValues) {
  if (!ReadAtElementName(pReader, L"RootValues"))
    return;

  if (pReader->IsEmptyElement()) return;

  UINT startDepth;
  XmlNodeType nt;
  CHECK_HR(pReader->GetDepth(&startDepth));
  for (;;) {
    UINT depth;
    CHECK_HR(pReader->Read(&nt));
    CHECK_HR(pReader->GetDepth(&depth));
    if (nt == XmlNodeType_EndElement && depth == startDepth + 1)
      return;
    if (nt == XmlNodeType_Element) {
      LPCWSTR pLocalName;
      CHECK_HR(pReader->GetLocalName(&pLocalName, nullptr));
      if (0 == wcscmp(pLocalName, L"RootValue")) {
        ShaderOpRootValue V;
        ParseRootValue(pReader, &V);
        pRootValues->push_back(V);
      }
    }
  }
}

void ShaderOpParser::ParseShaderOpSet(IStream *pStream, ShaderOpSet *pShaderOpSet) {
  CComPtr<IXmlReader> pReader;
  CHECK_HR(CreateXmlReader(__uuidof(IXmlReader), (void **)&pReader, nullptr));
  CHECK_HR(pReader->SetInput(pStream));
  ParseShaderOpSet(pReader, pShaderOpSet);
}

void ShaderOpParser::ParseShaderOpSet(IXmlReader *pReader, ShaderOpSet *pShaderOpSet) {
  if (!ReadAtElementName(pReader, L"ShaderOpSet"))
    return;
  UINT startDepth;
  CHECK_HR(pReader->GetDepth(&startDepth));
  XmlNodeType nt = XmlNodeType_Element;
  for (;;) {
    if (nt == XmlNodeType_Element) {
      LPCWSTR pLocalName;
      CHECK_HR(pReader->GetLocalName(&pLocalName, nullptr));
      if (0 == wcscmp(pLocalName, L"ShaderOp")) {
        std::unique_ptr<ShaderOp> S = std::make_unique<ShaderOp>();
        ParseShaderOp(pReader, S.get());
        pShaderOpSet->ShaderOps.push_back(*S.release());
      }
    }
    else if (nt == XmlNodeType_EndElement) {
      UINT depth;
      CHECK_HR(pReader->GetDepth(&depth));
      if (depth == startDepth + 1)
        return;
    }
    CHECK_HR(pReader->Read(&nt));
  }
}

void ShaderOpParser::ParseShaderOp(IXmlReader *pReader, ShaderOp *pShaderOp) {
  m_pStrings = &pShaderOp->Strings;

  // Look for a ShaderOp element.
  if (!ReadAtElementName(pReader, L"ShaderOp"))
    return;
  CHECK_HR(ReadAttrStr(pReader, L"Name", &pShaderOp->Name));
  CHECK_HR(ReadAttrStr(pReader, L"CS", &pShaderOp->CS));
  CHECK_HR(ReadAttrStr(pReader, L"VS", &pShaderOp->VS));
  CHECK_HR(ReadAttrStr(pReader, L"PS", &pShaderOp->PS));
  CHECK_HR(ReadAttrUINT(pReader, L"DispatchX", &pShaderOp->DispatchX, 1));
  CHECK_HR(ReadAttrUINT(pReader, L"DispatchY", &pShaderOp->DispatchY, 1));
  CHECK_HR(ReadAttrUINT(pReader, L"DispatchZ", &pShaderOp->DispatchZ, 1));
  UINT startDepth;
  CHECK_HR(pReader->GetDepth(&startDepth));
  XmlNodeType nt = XmlNodeType_Element;
  for (;;) {
    if (nt == XmlNodeType_Element) {
      LPCWSTR pLocalName;
      CHECK_HR(pReader->GetLocalName(&pLocalName, nullptr));
      if (0 == wcscmp(pLocalName, L"InputElements")) {
        ParseInputElements(pReader, &pShaderOp->InputElements);
      }
      else if (0 == wcscmp(pLocalName, L"Shader")) {
        ShaderOpShader shader;
        ParseShader(pReader, &shader);
        pShaderOp->Shaders.push_back(shader);
      }
      else if (0 == wcscmp(pLocalName, L"RootSignature")) {
        ReadElementContentStr(pReader, &pShaderOp->RootSignature);
      }
      else if (0 == wcscmp(pLocalName, L"RenderTargets")) {
        ParseRenderTargets(pReader, &pShaderOp->RenderTargets);
      }
      else if (0 == wcscmp(pLocalName, L"Resource")) {
        ShaderOpResource resource;
        ParseResource(pReader, &resource);
        pShaderOp->Resources.push_back(resource);
      }
      else if (0 == wcscmp(pLocalName, L"DescriptorHeap")) {
        ShaderOpDescriptorHeap heap;
        ParseDescriptorHeap(pReader, &heap);
        pShaderOp->DescriptorHeaps.push_back(heap);
      }
      else if (0 == wcscmp(pLocalName, L"RootValues")) {
        ParseRootValues(pReader, &pShaderOp->RootValues);
      }
    }
    else if (nt == XmlNodeType_EndElement) {
      UINT depth;
      CHECK_HR(pReader->GetDepth(&depth));
      if (depth == startDepth + 1)
        return;
    }

    if (S_FALSE == CHECK_HR_RET(pReader->Read(&nt)))
      return;
  }
}

LPCWSTR SkipByteInitSeparators(LPCWSTR pText) {
  while (*pText && (*pText == L' ' || *pText == L'\t' ||
                    *pText == L'\r' || *pText == L'\n' || *pText == L'{' ||
                    *pText == L'}' || *pText == L','))
    ++pText;
  return pText;
}
LPCWSTR FindByteInitSeparators(LPCWSTR pText) {
  while (*pText &&
         !(*pText == L' ' || *pText == L'\t' ||
           *pText == L'\r' || *pText == L'\n' || *pText == L'{' ||
           *pText == L'}' || *pText == L','))
    ++pText;
  return pText;
}

void ShaderOpParser::ParseResource(IXmlReader *pReader, ShaderOpResource *pResource) {
  if (!ReadAtElementName(pReader, L"Resource"))
    return;
  CHECK_HR(ReadAttrStr(pReader, L"Name", &pResource->Name));
  CHECK_HR(ReadAttrStr(pReader, L"Init", &pResource->Init));
  CHECK_HR(ReadAttrBOOL(pReader, L"ReadBack", &pResource->ReadBack));

  CHECK_HR(ReadAttrHEAP_TYPE(pReader, L"HeapType", &pResource->HeapProperties.Type));
  CHECK_HR(ReadAttrCPU_PAGE_PROPERTY(pReader, L"CPUPageProperty", &pResource->HeapProperties.CPUPageProperty));
  CHECK_HR(ReadAttrMEMORY_POOL(pReader, L"MemoryPoolPreference", &pResource->HeapProperties.MemoryPoolPreference));
  CHECK_HR(ReadAttrUINT(pReader, L"CreationNodeMask", &pResource->HeapProperties.CreationNodeMask));
  CHECK_HR(ReadAttrUINT(pReader, L"VisibleNodeMask", &pResource->HeapProperties.VisibleNodeMask));
  // D3D12_RESOURCE_DESC Desc;
  CHECK_HR(ReadAttrRESOURCE_DIMENSION(pReader, L"Dimension", &pResource->Desc.Dimension));
  CHECK_HR(ReadAttrUINT64(pReader, L"Alignment", &pResource->Desc.Alignment));
  CHECK_HR(ReadAttrUINT64(pReader, L"Width", &pResource->Desc.Width));
  CHECK_HR(ReadAttrUINT(pReader, L"Height", &pResource->Desc.Height));
  CHECK_HR(ReadAttrUINT16(pReader, L"DepthOrArraySize", &pResource->Desc.DepthOrArraySize));
  CHECK_HR(ReadAttrUINT16(pReader, L"MipLevels", &pResource->Desc.MipLevels));
  CHECK_HR(ReadAttrDXGI_FORMAT(pReader, L"Format", &pResource->Desc.Format));
  CHECK_HR(ReadAttrUINT(pReader, L"SampleCount", &pResource->Desc.SampleDesc.Count));
  CHECK_HR(ReadAttrUINT(pReader, L"SampleQual", &pResource->Desc.SampleDesc.Quality));
  CHECK_HR(ReadAttrTEXTURE_LAYOUT(pReader, L"Layout", &pResource->Desc.Layout));
  CHECK_HR(ReadAttrRESOURCE_FLAGS(pReader, L"Flags", &pResource->Desc.Flags));

  CHECK_HR(ReadAttrHEAP_FLAGS(pReader, L"HeapFlags", &pResource->HeapFlags));
  CHECK_HR(ReadAttrRESOURCE_STATES(pReader, L"InitialResourceState", &pResource->InitialResourceState));
  CHECK_HR(ReadAttrRESOURCE_STATES(pReader, L"TransitionTo", &pResource->TransitionTo));

  // Set some fixed values.
  if (pResource->Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
    pResource->Desc.Height = 1;
    pResource->Desc.DepthOrArraySize = 1;
    pResource->Desc.MipLevels = 1;
    pResource->Desc.Format = DXGI_FORMAT_UNKNOWN;
    pResource->Desc.SampleDesc.Count = 1;
    pResource->Desc.SampleDesc.Quality = 0;
    pResource->Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  }
  if (pResource->Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
    if (pResource->Desc.Height == 0) pResource->Desc.Height = 1;
    if (pResource->Desc.DepthOrArraySize == 0) pResource->Desc.DepthOrArraySize = 1;
    if (pResource->Desc.SampleDesc.Count == 0) pResource->Desc.SampleDesc.Count = 1;
  }
  if (pResource->Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
    if (pResource->Desc.DepthOrArraySize == 0) pResource->Desc.DepthOrArraySize = 1;
    if (pResource->Desc.SampleDesc.Count == 0 ) pResource->Desc.SampleDesc.Count = 1;
  }

  // If the resource has text, that goes into the bytes initialization area.
  if (pReader->IsEmptyElement())
    return;
  std::vector<BYTE> &V = pResource->InitBytes;
  XmlNodeType nt;
  CHECK_HR(pReader->GetNodeType(&nt));
  for (;;) {
    if (nt == XmlNodeType_EndElement) {
      return;
    }
    if (nt == XmlNodeType_Text) {
      // Handle the byte payload. '{', '}', ',', whitespace - these are all
      // separators and are ignored in terms of structure. We simply read
      // literals, figure out their type based on suffix, and write the bytes
      // into the target array.
      LPCWSTR pText;
      pReader->GetValue(&pText, nullptr);
      while (*pText) {
        pText = SkipByteInitSeparators(pText);
        if (!*pText) continue;
        LPCWSTR pEnd = FindByteInitSeparators(pText);
        // Consider looking for prefixes/suffixes to handle bases and types.
        float fVal;
        if (0 == _wcsicmp(pText, L"nan")) {
          fVal = NAN;
        }
        else if (0 == _wcsicmp(pText, L"-inf")) {
          fVal = INFINITY;
        }
        else if (0 == _wcsicmp(pText, L"inf") || 0 == _wcsicmp(pText, L"+inf")) {
          fVal = -(INFINITY);
        }
        else if (0 == _wcsicmp(pText, L"-denorm")) {
          fVal = -(FLT_MIN / 2);
        }
        else if (0 == _wcsicmp(pText, L"denorm")) {
          fVal = (FLT_MIN / 2);
        }
        else {
          fVal = wcstof(pText, nullptr);
        }
        BYTE *pB = (BYTE *)&fVal;
        for (size_t i = 0; i < sizeof(float); ++i) {
          V.push_back(*pB);
          ++pB;
        }
        pText = pEnd;
      }
    }
    if (S_FALSE == CHECK_HR_RET(pReader->Read(&nt)))
      return;
  }
}

void ShaderOpParser::ParseShader(IXmlReader *pReader, ShaderOpShader *pShader) {
  if (!ReadAtElementName(pReader, L"Shader"))
    return;
  CHECK_HR(ReadAttrStr(pReader, L"Name", &pShader->Name));
  CHECK_HR(ReadAttrStr(pReader, L"EntryPoint", &pShader->EntryPoint));
  CHECK_HR(ReadAttrStr(pReader, L"Target", &pShader->Target));
  ReadElementContentStr(pReader, &pShader->Text);
  bool hasText = pShader->Text && *pShader->Text;
  if (hasText) {
    LPCSTR pCheck;
    CHECK_HR(ReadAttrStr(pReader, L"Text", &pCheck));
    if (pCheck && *pCheck) {
      ShaderOpLogFmt(L"Shader %S has text content and a Text attribute; it "
                     L"should only have one",
                     pShader->Name);
      CHECK_HR(E_INVALIDARG);
    }
  }
  else {
    CHECK_HR(ReadAttrStr(pReader, L"Text", &pShader->Text));
  }

  if (pShader->EntryPoint == nullptr)
    pShader->EntryPoint = m_pStrings->insert("main");
}

bool ShaderOpParser::ReadAtElementName(IXmlReader *pReader, LPCWSTR pName) {
  XmlNodeType nt;
  CHECK_HR(pReader->GetNodeType(&nt));
  for (;;) {
    if (nt == XmlNodeType_Element) {
      LPCWSTR pLocalName;
      CHECK_HR(pReader->GetLocalName(&pLocalName, nullptr));
      if (0 == wcscmp(pLocalName, pName)) {
        return true;
      }
    }
    if (S_FALSE == CHECK_HR_RET(pReader->Read(&nt)))
      return false;
  }
}

#pragma endregion Parsing support

} // namespace st
