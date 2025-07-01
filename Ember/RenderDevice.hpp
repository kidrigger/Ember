#pragma once

#include "Base/DirectXHeaders.hpp"
#include "Base/Runtime.hpp"

namespace Ember
{

class RenderDevice
{
  // Device and queues.
  ComPtr<ID3D12Device>       m_Device;
  ComPtr<ID3D12CommandQueue> m_DirectQueue;

  // Swapchain and internal images.
  UINT32                              m_SwapchainWidth{ 640 };
  UINT32                              m_SwapchainHeight{ 480 };
  ComPtr<IDXGISwapChain4>             m_Swapchain;
  std::vector<ComPtr<ID3D12Resource>> m_Backbuffers;

  // Views to swapchain images.
  ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
  UINT32                       m_RTVDescriptorSize{ 0 };
  UINT32                       m_CurrentBackbufferIndex{ 0 };

  // Commands
  ComPtr<ID3D12GraphicsCommandList>           m_CommandList;
  std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;

  // Synchronization
  ComPtr<ID3D12Fence> m_Fence;
  UINT64              m_CurrentFenceValue{ 0 };
  std::vector<UINT64> m_FenceValues;
  HANDLE              m_FenceEvent{ nullptr };

  // Meta
  bool m_IsInitialized{ false };

public:
  RenderDevice(
      ComPtr<ID3D12Device> const&                   device,
      ComPtr<ID3D12CommandQueue> const&             direct_queue,
      UINT32                                        swapchain_width,
      UINT32                                        swapchain_height,
      ComPtr<IDXGISwapChain4> const&                swapchain,
      std::vector<ComPtr<ID3D12Resource>>&&         backbuffers,
      ComPtr<ID3D12DescriptorHeap> const&           rtv_descriptor_heap,
      UINT32                                        rtv_descriptor_size,
      UINT32                                        current_backbuffer_index,
      ComPtr<ID3D12GraphicsCommandList> const&      command_list,
      std::vector<ComPtr<ID3D12CommandAllocator>>&& command_allocators,
      ComPtr<ID3D12Fence> const&                    fence,
      HANDLE                                        fence_event );

  static RenderDevice Create( HWND window_handle, bool use_warp );
  void                Destroy();

  ~RenderDevice();

  size_t constexpr static NUM_FRAMES = 3;
};

} // namespace Ember
