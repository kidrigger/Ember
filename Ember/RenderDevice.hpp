#pragma once

#include "BufferManager.hpp"
#include "DepthBuffer.hpp"
#include "ResourceHandles.hpp"
#include "Util/DirectXHeaders.hpp"
#include "Util/Runtime.hpp"

namespace Ember
{
class BufferManager;

class RenderDevice
{
public:
  constexpr static size_t kNumFrames = 3;

private:
  // Device and queues.
  ComPtr<ID3D12Device2>      m_Device;
  ComPtr<D3D12MA::Allocator> m_Allocator;
  ComPtr<ID3D12CommandQueue> m_DirectQueue;

  // Swapchain and internal images.
  uint32_t                            m_SwapchainWidth{ 640 };
  uint32_t                            m_SwapchainHeight{ 480 };
  ComPtr<IDXGISwapChain4>             m_Swapchain;
  std::vector<ComPtr<ID3D12Resource>> m_Backbuffers;

  constexpr static uint32_t           kUseVSyncBit       = 1 << 0;
  constexpr static uint32_t           kSupportTearingBit = 1 << 1;

  uint32_t                            m_VsyncAndTearing{ 0 };

  // Views to swapchain images.
  ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
  uint32_t                     m_RTVDescriptorSize{ 0 };
  uint32_t                     m_CurrentBackbufferIndex{ 0 };

  // Depth Buffer
  ComPtr<ID3D12DescriptorHeap> m_DSVDescriptorHeap;

  // Commands
  ComPtr<ID3D12GraphicsCommandList>           m_CommandList;
  std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;

  // Synchronization
  ComPtr<ID3D12Fence>   m_Fence;
  uint64_t              m_CurrentFenceValue{ 0 };
  std::vector<uint64_t> m_FenceValues;
  HANDLE                m_FenceEvent{ nullptr }; // Also acts as 'initialized'

  // Resource Management
  BufferManager m_BufferManager;

public:
  RenderDevice(
      ComPtr<ID3D12Device2> const&                  device,
      ComPtr<D3D12MA::Allocator> const&             allocator,
      ComPtr<ID3D12CommandQueue> const&             direct_queue,
      uint32_t                                      swapchain_width,
      uint32_t                                      swapchain_height,
      ComPtr<IDXGISwapChain4> const&                swapchain,
      std::vector<ComPtr<ID3D12Resource>>&&         backbuffers,
      ComPtr<ID3D12DescriptorHeap> const&           rtv_descriptor_heap,
      uint32_t                                      rtv_descriptor_size,
      uint32_t                                      current_backbuffer_index,
      ComPtr<ID3D12DescriptorHeap> const&           dsv_descriptor_heap,
      ComPtr<ID3D12GraphicsCommandList> const&      command_list,
      std::vector<ComPtr<ID3D12CommandAllocator>>&& command_allocators,
      ComPtr<ID3D12Fence> const&                    fence,
      HANDLE                                        fence_event,
      bool                                          is_tearing_supported,
      BufferManager&&                               buffer_manager );

  ComPtr<ID3D12Device2>     GetDevice();

  static RenderDevice       Create( HWND window_handle, bool use_warp );
  void                      Destroy();

  void                      ResizeSwapchain( uint32_t width, uint32_t height );
  Buffer                    CreateUniformBuffer( size_t size );
  [[nodiscard]] DepthBuffer CreateDepthBuffer( uint32_t width, uint32_t height ) const;

  void                      SetDepthBuffer( DepthBuffer const& depth_buffer ) const;

  // Wait until the all queues have finished all commands.
  void               WaitIdle();
  [[nodiscard]] bool IsInit() const;

  // Per Frame getters.
  [[nodiscard]] ID3D12CommandAllocator*       GetCurrentCommandAllocator() const;
  [[nodiscard]] ID3D12Resource*               GetCurrentBackbuffer() const;
  [[nodiscard]] ID3D12GraphicsCommandList*    GetGraphicsCommandList() const;
  [[nodiscard]] CD3DX12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVCpuDescriptorHandle() const;
  [[nodiscard]] CD3DX12_CPU_DESCRIPTOR_HANDLE GetCurrentDSVCpuDescriptorHandle() const;
  void                                        ExecuteCommandList( ID3D12CommandList* command_list ) const;
  void                                        Present();

  [[nodiscard]] bool                          IsVsyncEnabled() const;
  [[nodiscard]] bool                          IsTearingSupported() const;
};

} // namespace Ember
