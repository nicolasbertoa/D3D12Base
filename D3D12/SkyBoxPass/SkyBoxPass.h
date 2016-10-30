#pragma once

#include <memory>

#include <SkyBoxPass\SkyBoxCmdListRecorder.h>

class CommandListExecutor;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct FrameCBuffer;

// Pass that renders the sky box
class SkyBoxPass {
public:
	using Recorder = std::unique_ptr<SkyBoxCmdListRecorder>;

	SkyBoxPass() = default;
	SkyBoxPass(const SkyBoxPass&) = delete;
	const SkyBoxPass& operator=(const SkyBoxPass&) = delete;

	// You should call this method after filling recorder and before Execute()
	void Init(
		ID3D12Device& device,
		CommandListExecutor& cmdListProcessor,
		ID3D12CommandQueue& cmdQueue,
		tbb::concurrent_queue<ID3D12CommandList*>& cmdListQueue,
		ID3D12Resource& skyBoxCubeMap,
		const D3D12_CPU_DESCRIPTOR_HANDLE& colorBufferCpuDesc,
		const D3D12_CPU_DESCRIPTOR_HANDLE& depthBufferCpuDesc) noexcept;

	void Execute(const FrameCBuffer& frameCBuffer) const noexcept;

private:
	// Method used internally for validation purposes
	bool ValidateData() const noexcept;
	
	CommandListExecutor* mCmdListProcessor{ nullptr };
	ID3D12CommandAllocator* mCmdAlloc{ nullptr };
	ID3D12GraphicsCommandList* mCmdList{ nullptr };

	ID3D12Fence* mFence{ nullptr };

	Recorder mRecorder;
};
