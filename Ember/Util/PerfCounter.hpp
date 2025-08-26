#pragma once

#include <vector>

#include "DirectXHeaders.hpp"
#include "Runtime.hpp"

namespace Ember
{

class PerfCounter
{
  double constexpr static kSampleCount = 256.0;
  double constexpr static kMaxDeltaMs  = 1000.0 / 24.0; // Below 24fps, I'd rather slow down than jump.

  D3D12_QUERY_DATA_PIPELINE_STATISTICS1 m_PipelineStats{};
  LARGE_INTEGER                         m_PrevQueryPerfCounter{};
  double                                m_FrameTimeMs{ 0.0f };
  double                                m_256FrameAvgBuffer[256]{};
  double                                m_BufferSumMs{ 0.0f };
  int                                   m_AvgBufferHead{ 0 };
  double                                m_SampleCount{ 0.0f };

  // Query heap.
  std::vector<ComPtr<ID3D12QueryHeap>> m_QueryHeaps;
  std::vector<ComPtr<ID3D12Resource>>  m_QueryReadbackBuffers;

public:
  PerfCounter();
  PerfCounter(
      std::vector<ComPtr<ID3D12QueryHeap>> query_heaps, std::vector<ComPtr<ID3D12Resource>> query_readback_buffers );

  static void          Create( PerfCounter* perf_counter, ID3D12Device* device, uint32_t num_frames );

  void                 Tick();

  [[nodiscard]] double GetAvgFrameTime() const;
  [[nodiscard]] double GetDeltaMilliSeconds() const;
  [[nodiscard]] D3D12_QUERY_DATA_PIPELINE_STATISTICS1 const& GetPipelineStats() const;

  void BeginQuery( ID3D12GraphicsCommandList* command_list, uint32_t frame_index ) const;
  void EndQuery( ID3D12GraphicsCommandList* command_list, uint32_t frame_index ) const;
  void UpdatePipelineStats( uint32_t frame_index );
};

} // namespace Ember
