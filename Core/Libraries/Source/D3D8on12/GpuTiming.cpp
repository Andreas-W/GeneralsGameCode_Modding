/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// GpuTiming.cpp
// GPU time of the parts of a frame, from timestamp queries, while the engine asks for it
// (ID3D8On12PostProcess::Set_Gpu_Timing). A frame is recorded into one command list that the GPU runs
// at Present, so the time from its first to its last timestamp is the GPU's work for it.

#include "Device.h"

#include <string.h>

namespace D3D8On12
{

static const UINT TIMESTAMPS_PER_FRAME = 64;

void Device::Set_Gpu_Timing(bool enable)
{
	if (enable && m_timestampHeap == nullptr)
	{
		D3D12_QUERY_HEAP_DESC heap_desc = {};
		heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		heap_desc.Count = FRAME_COUNT * TIMESTAMPS_PER_FRAME;
		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = FRAME_COUNT * TIMESTAMPS_PER_FRAME * sizeof(UINT64);
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(m_device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&m_timestampHeap)))
			|| FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
				IID_PPV_ARGS(&m_timestampReadback)))
			|| FAILED(m_queue->GetTimestampFrequency(&m_timestampFrequency)) || m_timestampFrequency == 0)
		{
			Log("GPU timing is not available");
			m_timestampHeap.Reset();
			m_timestampReadback.Reset();
			return;
		}
	}
	m_timing = enable && m_timestampHeap != nullptr;
	memset(&m_timingSums, 0, sizeof(m_timingSums));
}

void Device::Mark_Time(UINT marker)
{
	if (!m_timing || !m_recording)
		return;
	std::vector<UINT>& markers = m_timingMarkers[m_backBufferIndex];
	if (markers.size() >= TIMESTAMPS_PER_FRAME)
		return;
	m_commandList->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_backBufferIndex * TIMESTAMPS_PER_FRAME + (UINT)markers.size());
	markers.push_back(marker);
}

// Before the frame's command list closes: its timestamps go to its part of the readback buffer.
void Device::Resolve_Times()
{
	const std::vector<UINT>& markers = m_timingMarkers[m_backBufferIndex];
	if (m_timestampHeap == nullptr || markers.empty())
		return;
	const UINT first = m_backBufferIndex * TIMESTAMPS_PER_FRAME;
	m_commandList->ResolveQueryData(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, (UINT)markers.size(),
		m_timestampReadback.Get(), first * sizeof(UINT64));
}

// When the GPU is done with the frame that last used this back buffer index: adds up its times.
void Device::Collect_Times()
{
	std::vector<UINT>& markers = m_timingMarkers[m_backBufferIndex];
	if (markers.empty())
		return;
	if (m_timing)
	{
		const UINT first = m_backBufferIndex * TIMESTAMPS_PER_FRAME;
		const D3D12_RANGE range = { first * sizeof(UINT64), (first + markers.size()) * sizeof(UINT64) };
		void* mapped = nullptr;
		if (SUCCEEDED(m_timestampReadback->Map(0, &range, &mapped)))
		{
			const UINT64* stamps = (const UINT64*)mapped + first;
			const double to_ms = 1000.0 / (double)m_timestampFrequency;
			UINT64 found[TIMING_PASS + MAX_TIMED_PASSES] = {};
			bool seen[TIMING_PASS + MAX_TIMED_PASSES] = {};
			for (size_t i = 0; i < markers.size(); ++i)
			{
				if (markers[i] < TIMING_PASS + MAX_TIMED_PASSES && !seen[markers[i]])
				{
					seen[markers[i]] = true;
					found[markers[i]] = stamps[i];
				}
			}
			auto span = [&](UINT begin, UINT end) { return seen[begin] && seen[end] && found[end] > found[begin] ? (found[end] - found[begin]) * to_ms : 0.0; };
			if (seen[TIMING_FRAME_BEGIN] && seen[TIMING_FRAME_END])
			{
				TimingSums& sums = m_timingSums;
				++sums.frames;
				sums.frame += span(TIMING_FRAME_BEGIN, TIMING_FRAME_END);
				sums.shadowMap += span(TIMING_SHADOW_MAP_BEGIN, TIMING_SHADOW_MAP_END);
				sums.shadows += span(TIMING_SHADOWS_BEGIN, TIMING_SHADOWS_END);
				sums.postProcess += span(TIMING_POST_BEGIN, TIMING_POST_END);
				// A pass lasts until the next pass starts, the last one until the chain ends.
				for (UINT pass = 0; pass < MAX_TIMED_PASSES && seen[TIMING_PASS + pass]; ++pass)
				{
					const UINT end = pass + 1 < MAX_TIMED_PASSES && seen[TIMING_PASS + pass + 1] ? TIMING_PASS + pass + 1 : TIMING_POST_END;
					sums.passes[pass] += span(TIMING_PASS + pass, end);
					sums.passCount = max(sums.passCount, pass + 1);
				}
			}
			const D3D12_RANGE written = { 0, 0 };
			m_timestampReadback->Unmap(0, &written);
		}
	}
	markers.clear();
}

} // namespace D3D8On12
