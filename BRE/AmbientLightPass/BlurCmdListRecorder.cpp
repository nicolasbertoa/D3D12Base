#include "BlurCmdListRecorder.h"

#include <DirectXMath.h>

#include <CommandListExecutor\CommandListExecutor.h>
#include <CommandManager/CommandAllocatorManager.h>
#include <CommandManager/CommandListManager.h>
#include <DescriptorManager\CbvSrvUavDescriptorManager.h>
#include <PSOManager/PSOManager.h>
#include <Utils/DebugUtils.h>

// Root Signature:
// "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)" 0 -> Color Buffer Texture

namespace {
	ID3D12PipelineState* sPSO{ nullptr };
	ID3D12RootSignature* sRootSign{ nullptr };

	void BuildCommandObjects(ID3D12GraphicsCommandList* &cmdList, ID3D12CommandAllocator* cmdAlloc[], const std::size_t cmdAllocCount) noexcept {
		ASSERT(cmdList == nullptr);

#ifdef _DEBUG
		for (std::uint32_t i = 0U; i < cmdAllocCount; ++i) {
			ASSERT(cmdAlloc[i] == nullptr);
		}
#endif

		for (std::uint32_t i = 0U; i < cmdAllocCount; ++i) {
			CommandAllocatorManager::Get().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc[i]);
		}

		CommandListManager::Get().CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, *cmdAlloc[0], cmdList);

		// Start off in a closed state.  This is because the first time we refer 
		// to the command list we will Reset it, and it needs to be closed before
		// calling Reset.
		cmdList->Close();
	}
}

BlurCmdListRecorder::BlurCmdListRecorder() {
	BuildCommandObjects(mCmdList, mCmdAlloc, _countof(mCmdAlloc));
}

void BlurCmdListRecorder::InitPSO() noexcept {
	ASSERT(sPSO == nullptr);
	ASSERT(sRootSign == nullptr);

	// Build pso and root signature
	PSOManager::PSOCreationData psoData{};
	const std::size_t rtCount{ _countof(psoData.mRtFormats) };
	psoData.mDepthStencilDesc = D3DFactory::GetDisabledDepthStencilDesc();
	psoData.mPSFilename = "AmbientLightPass/Shaders/Blur/PS.cso";
	psoData.mRootSignFilename = "AmbientLightPass/Shaders/Blur/RS.cso";
	psoData.mVSFilename = "AmbientLightPass/Shaders/Blur/VS.cso";
	psoData.mNumRenderTargets = 1U;
	psoData.mRtFormats[0U] = DXGI_FORMAT_R16_UNORM;
	for (std::size_t i = psoData.mNumRenderTargets; i < rtCount; ++i) {
		psoData.mRtFormats[i] = DXGI_FORMAT_UNKNOWN;
	}
	psoData.mTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PSOManager::Get().CreateGraphicsPSO(psoData, sPSO, sRootSign);

	ASSERT(sPSO != nullptr);
	ASSERT(sRootSign != nullptr);
}

void BlurCmdListRecorder::Init(
	ID3D12Resource& ambientAccessibilityBuffer,
	const D3D12_CPU_DESCRIPTOR_HANDLE& blurBufferCpuDesc) noexcept
{
	ASSERT(ValidateData() == false);

	mBlurBufferCpuDesc = blurBufferCpuDesc;

	BuildBuffers(ambientAccessibilityBuffer);

	ASSERT(ValidateData());
}

void BlurCmdListRecorder::RecordAndPushCommandLists() noexcept {
	ASSERT(ValidateData());
	ASSERT(sPSO != nullptr);
	ASSERT(sRootSign != nullptr);

	static std::uint32_t currFrameIndex = 0U;

	ID3D12CommandAllocator* cmdAlloc{ mCmdAlloc[currFrameIndex] };
	ASSERT(cmdAlloc != nullptr);

	CHECK_HR(cmdAlloc->Reset());
	CHECK_HR(mCmdList->Reset(cmdAlloc, sPSO));

	mCmdList->RSSetViewports(1U, &SettingsManager::sScreenViewport);
	mCmdList->RSSetScissorRects(1U, &SettingsManager::sScissorRect);
	mCmdList->OMSetRenderTargets(1U, &mBlurBufferCpuDesc, false, nullptr);

	ID3D12DescriptorHeap* heaps[] = { &CbvSrvUavDescriptorManager::Get().GetDescriptorHeap() };
	mCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
	mCmdList->SetGraphicsRootSignature(sRootSign);

	// Set root parameters
	mCmdList->SetGraphicsRootDescriptorTable(0U, mColorBufferGpuDesc);

	// Draw object
	mCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCmdList->DrawInstanced(6U, 1U, 0U, 0U);

	mCmdList->Close();

	CommandListExecutor::Get().AddCommandList(*mCmdList);

	// Next frame
	currFrameIndex = (currFrameIndex + 1) % SettingsManager::sQueuedFrameCount;
}

bool BlurCmdListRecorder::ValidateData() const noexcept {

	for (std::uint32_t i = 0UL; i < SettingsManager::sQueuedFrameCount; ++i) {
		if (mCmdAlloc[i] == nullptr) {
			return false;
		}
	}

	const bool result =
		mCmdList != nullptr &&
		mColorBufferGpuDesc.ptr != 0UL && 
		mBlurBufferCpuDesc.ptr != 0UL;

	return result;
}

void BlurCmdListRecorder::BuildBuffers(ID3D12Resource& colorBuffer) noexcept {
	// Create color buffer texture descriptor
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = colorBuffer.GetDesc().Format;
	srvDesc.Texture2D.MipLevels = colorBuffer.GetDesc().MipLevels;
	mColorBufferGpuDesc = CbvSrvUavDescriptorManager::Get().CreateShaderResourceView(colorBuffer, srvDesc);
}