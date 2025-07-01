#pragma once

#include "Base/DirectXHeaders.hpp"
#include "Base/HelperUtils.hpp"
#include "Base/Runtime.hpp"

namespace Ember
{

class RenderDevice
{
  // Device and queues.
  ComPtr<ID3D12Device2>      m_Device;
  ComPtr<ID3D12CommandQueue> m_DirectQueue;

  // Swapchain and internal images.
  uint32_t                            m_SwapchainWidth{ 640 };
  uint32_t                            m_SwapchainHeight{ 480 };
  ComPtr<IDXGISwapChain4>             m_Swapchain;
  std::vector<ComPtr<ID3D12Resource>> m_Backbuffers;

  constexpr static uint32_t           USE_VSYNC_BIT       = 1 << 0;
  constexpr static uint32_t           SUPPORT_TEARING_BIT = 1 << 1;

  uint32_t                            m_VsyncAndTearing{ 0 };

  // Views to swapchain images.
  ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
  uint32_t                     m_RTVDescriptorSize{ 0 };
  uint32_t                     m_CurrentBackbufferIndex{ 0 };

  // Commands
  ComPtr<ID3D12GraphicsCommandList>           m_CommandList;
  std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;

  // Synchronization
  ComPtr<ID3D12Fence>   m_Fence;
  uint64_t              m_CurrentFenceValue{ 0 };
  std::vector<uint64_t> m_FenceValues;
  HANDLE                m_FenceEvent{ nullptr }; // Also acts as 'initialized'

public:
  RenderDevice(
      ComPtr<ID3D12Device2> const&                  device,
      ComPtr<ID3D12CommandQueue> const&             direct_queue,
      uint32_t                                      swapchain_width,
      uint32_t                                      swapchain_height,
      ComPtr<IDXGISwapChain4> const&                swapchain,
      std::vector<ComPtr<ID3D12Resource>>&&         backbuffers,
      ComPtr<ID3D12DescriptorHeap> const&           rtv_descriptor_heap,
      uint32_t                                      rtv_descriptor_size,
      uint32_t                                      current_backbuffer_index,
      ComPtr<ID3D12GraphicsCommandList> const&      command_list,
      std::vector<ComPtr<ID3D12CommandAllocator>>&& command_allocators,
      ComPtr<ID3D12Fence> const&                    fence,
      HANDLE                                        fence_event,
      bool                                          is_tearing_supported );

  static RenderDevice Create( HWND window_handle, bool use_warp );
  void                Destroy();

  void                ResizeSwapchain( uint32_t width, uint32_t height );

  // Wait until the all queues have finished all commands.
  void WaitIdle();

  ~RenderDevice();

  [[nodiscard]] ID3D12CommandAllocator*       GetCurrentCommandAllocator() const;
  [[nodiscard]] ID3D12Resource*               GetCurrentBackbuffer() const;
  [[nodiscard]] ID3D12GraphicsCommandList*    GetGraphicsCommandList() const;
  [[nodiscard]] CD3DX12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVCpuDescriptorHandle() const;
  void                                        ExecuteCommandList( ID3D12CommandList* command_list ) const;
  void                                        Present();

  [[nodiscard]] bool                          IsVsyncEnabled() const;
  [[nodiscard]] bool                          IsTearingSupported() const;

  size_t constexpr static NUM_FRAMES = 3;
};

} // namespace Ember
