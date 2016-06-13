#include "InitTask.h"

#include <CommandManager/CommandManager.h>
#include <PSOManager/PSOManager.h>
#include <ResourceManager\ResourceManager.h>
#include <RootSignatureManager/RootSignatureManager.h>
#include <ShaderManager/ShaderManager.h>
#include <Utils/DebugUtils.h>

bool InitTaskInput::ValidateData() const {
	return !mMeshInfoVec.empty() && !mInputLayout.empty();
}

void InitTask::Execute(ID3D12Device& /*device*/, tbb::concurrent_queue<ID3D12CommandList*>& /*cmdLists*/, CmdBuilderTaskInput& output) noexcept {
	ASSERT(mInput.ValidateData());
	
	output.mTopology = mInput.mTopology;

	BuildPSO(output.mRootSign, output.mPSO);
	BuildCommandObjects(output.mCmdList1, output.mCmdAlloc1);
	BuildCommandObjects(output.mCmdList2, output.mCmdAlloc2);
}

void InitTask::BuildPSO(ID3D12RootSignature* &rootSign, ID3D12PipelineState* &pso) noexcept {
	ASSERT(pso == nullptr);
	ASSERT(mInput.ValidateData());
	ASSERT(mInput.mRootSignFilename != nullptr);
	ASSERT(rootSign == nullptr);

	ID3DBlob* rootSignBlob{ nullptr };
	ShaderManager::gManager->LoadShaderFile(mInput.mRootSignFilename, rootSignBlob);
	RootSignatureManager::gManager->CreateRootSignature(*rootSignBlob, rootSign);

	D3D12_SHADER_BYTECODE vertexShader{};
	if (mInput.mVSFilename != nullptr) {
		ShaderManager::gManager->LoadShaderFile(mInput.mVSFilename, vertexShader);
	}
	
	D3D12_SHADER_BYTECODE geomShader{};
	if (mInput.mGSFilename != nullptr) {
		ShaderManager::gManager->LoadShaderFile(mInput.mGSFilename, geomShader);
	}

	D3D12_SHADER_BYTECODE domainShader{};
	if (mInput.mDSFilename != nullptr) {
		ShaderManager::gManager->LoadShaderFile(mInput.mDSFilename, domainShader);
	}

	D3D12_SHADER_BYTECODE hullShader{};
	if (mInput.mHSFilename != nullptr) {
		ShaderManager::gManager->LoadShaderFile(mInput.mHSFilename, hullShader);
	}

	D3D12_SHADER_BYTECODE pixelShader{};
	if (mInput.mPSFilename != nullptr) {
		ShaderManager::gManager->LoadShaderFile(mInput.mPSFilename, pixelShader);
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.BlendState = mInput.mBlendDesc;
	desc.DepthStencilState = mInput.mDepthStencilDesc;
	desc.DS = domainShader;
	desc.DSVFormat = mInput.mDepthStencilFormat;
	desc.GS = geomShader;
	desc.HS = hullShader;
	desc.InputLayout = { mInput.mInputLayout.data(), (std::uint32_t)mInput.mInputLayout.size() };
	desc.NumRenderTargets = mInput.mNumRenderTargets;
	desc.PrimitiveTopologyType = mInput.mTopology;
	desc.pRootSignature = rootSign;
	desc.PS = pixelShader;
	desc.RasterizerState = D3DFactory::DefaultRasterizerDesc();
	memcpy(desc.RTVFormats, mInput.mRTVFormats, sizeof(mInput.mRTVFormats));
	desc.SampleDesc = mInput.mSampleDesc;
	desc.SampleMask = mInput.mSampleMask;
	desc.VS = vertexShader;

	PSOManager::gManager->CreateGraphicsPSO(desc, pso);
}

void InitTask::BuildVertexAndIndexBuffers(
	GeometryData& geomData,
	const void* vertsData,
	const std::uint32_t numVerts,
	const std::size_t vertexSize,
	const void* indexData,
	const std::uint32_t numIndices,
	ID3D12GraphicsCommandList& cmdList) noexcept {

	ASSERT(geomData.mIndexBuffer == nullptr);
	ASSERT(geomData.mVertexBuffer == nullptr);
	ASSERT(vertsData != nullptr);
	ASSERT(numVerts > 0U);
	ASSERT(vertexSize > 0UL);
	ASSERT(indexData != nullptr);
	ASSERT(numIndices > 0U);

	std::uint32_t byteSize{ numVerts * (std::uint32_t)vertexSize };

	ResourceManager::gManager->CreateDefaultBuffer(cmdList, vertsData, byteSize, geomData.mVertexBuffer, geomData.mUploadVertexBuffer);
	geomData.mVertexBufferView.BufferLocation = geomData.mVertexBuffer->GetGPUVirtualAddress();
	geomData.mVertexBufferView.SizeInBytes = byteSize;
	geomData.mVertexBufferView.StrideInBytes = (std::uint32_t)vertexSize;

	geomData.mIndexCount = numIndices;
	byteSize = numIndices * sizeof(std::uint32_t);
	ResourceManager::gManager->CreateDefaultBuffer(cmdList, indexData, byteSize, geomData.mIndexBuffer, geomData.mUploadIndexBuffer);
	geomData.mIndexBufferView.BufferLocation = geomData.mIndexBuffer->GetGPUVirtualAddress();
	geomData.mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	geomData.mIndexBufferView.SizeInBytes = byteSize;
}

void InitTask::BuildCommandObjects(ID3D12GraphicsCommandList* &cmdList, ID3D12CommandAllocator* &cmdAlloc) noexcept {
	ASSERT(cmdList == nullptr);
	ASSERT(cmdAlloc == nullptr);

	CommandManager::gManager->CreateCmdAlloc(D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc);
	CommandManager::gManager->CreateCmdList(D3D12_COMMAND_LIST_TYPE_DIRECT, *cmdAlloc, cmdList);

	// Start off in a closed state.  This is because the first time we refer 
	// to the command list we will Reset it, and it needs to be closed before
	// calling Reset.
	cmdList->Close();
}