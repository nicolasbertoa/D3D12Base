#include "HeightCmdListRecorder.h"

#include <DirectXMath.h>

#include <Material/Material.h>
#include <MathUtils/MathUtils.h>
#include <PSOCreator/PSOCreator.h>
#include <ResourceManager/ResourceManager.h>
#include <ResourceManager/UploadBuffer.h>
#include <ShaderUtils\CBuffers.h>
#include <Utils/DebugUtils.h>

// Root signature:
// "DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_VERTEX), " \ 0 -> Object CBuffers
// "CBV(b1, visibility = SHADER_VISIBILITY_VERTEX), " \ 1 -> Frame CBuffer
// "CBV(b0, visibility = SHADER_VISIBILITY_DOMAIN), " \ 2 -> Frame CBuffer
// "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_DOMAIN), " \ 3 -> Height Texture
// "DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL), " \ 4 -> Material CBuffers
// "CBV(b1, visibility = SHADER_VISIBILITY_PIXEL), " \ 5 -> Frame CBuffer
// "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \ 6 -> Diffuse Texture
// "DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL), " \ 7 -> Normal Texture

namespace {
	ID3D12PipelineState* sPSO{ nullptr };
	ID3D12RootSignature* sRootSign{ nullptr };
}

HeightCmdListRecorder::HeightCmdListRecorder(ID3D12Device& device)
	: GeometryPassCmdListRecorder(device)
{
}

void HeightCmdListRecorder::InitPSO(const DXGI_FORMAT* geometryBufferFormats, const std::uint32_t geometryBufferCount) noexcept {
	ASSERT(geometryBufferFormats != nullptr);
	ASSERT(geometryBufferCount > 0U);
	ASSERT(sPSO == nullptr);
	ASSERT(sRootSign == nullptr);

	// Build pso and root signature
	PSOCreator::PSOParams psoParams{};
	psoParams.mDSFilename = "GeometryPass/Shaders/HeightMapping/DS.cso";
	psoParams.mHSFilename = "GeometryPass/Shaders/HeightMapping/HS.cso";
	psoParams.mInputLayout = D3DFactory::PosNormalTangentTexCoordInputLayout();
	psoParams.mPSFilename = "GeometryPass/Shaders/HeightMapping/PS.cso";
	psoParams.mRootSignFilename = "GeometryPass/Shaders/HeightMapping/RS.cso";
	psoParams.mVSFilename = "GeometryPass/Shaders/HeightMapping/VS.cso";
	psoParams.mTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	psoParams.mNumRenderTargets = geometryBufferCount;
	memcpy(psoParams.mRtFormats, geometryBufferFormats, sizeof(DXGI_FORMAT) * psoParams.mNumRenderTargets);
	PSOCreator::CreatePSO(psoParams, sPSO, sRootSign);

	ASSERT(sPSO != nullptr);
	ASSERT(sRootSign != nullptr);
}

void HeightCmdListRecorder::Init(
	const GeometryData* geometryDataVec,
	const std::uint32_t numGeomData,
	const Material* materials,
	ID3D12Resource** textures,
	ID3D12Resource** normals,
	ID3D12Resource** heights,
	const std::uint32_t numResources) noexcept
{
	ASSERT(ValidateData() == false);
	ASSERT(geometryDataVec != nullptr);
	ASSERT(numGeomData != 0U);
	ASSERT(materials != nullptr);
	ASSERT(numResources > 0UL);
	ASSERT(textures != nullptr);
	ASSERT(normals != nullptr);
	ASSERT(heights != nullptr);

	// Check that the total number of matrices (geometry to be drawn) will be equal to available materials
#ifdef _DEBUG
	std::size_t totalNumMatrices{ 0UL };
	for (std::size_t i = 0UL; i < numGeomData; ++i) {
		const std::size_t numMatrices{ geometryDataVec[i].mWorldMatrices.size() };
		totalNumMatrices += numMatrices;
		ASSERT(numMatrices != 0UL);
	}
	ASSERT(totalNumMatrices == numResources);
#endif
	mGeometryDataVec.reserve(numGeomData);
	for (std::uint32_t i = 0U; i < numGeomData; ++i) {
		mGeometryDataVec.push_back(geometryDataVec[i]);
	}

	BuildBuffers(materials, textures, normals, heights, numResources);

	ASSERT(ValidateData());
}

void HeightCmdListRecorder::RecordAndPushCommandLists(const FrameCBuffer& frameCBuffer) noexcept {
	ASSERT(ValidateData());
	ASSERT(sPSO != nullptr);
	ASSERT(sRootSign != nullptr);
	ASSERT(mCmdListQueue != nullptr);
	ASSERT(mGeometryBuffersCpuDescs != nullptr);
	ASSERT(mGeometryBuffersCpuDescCount != 0U);
	ASSERT(mDepthBufferCpuDesc.ptr != 0U);

	ID3D12CommandAllocator* cmdAlloc{ mCmdAlloc[mCurrFrameIndex] };
	ASSERT(cmdAlloc != nullptr);

	// Update frame constants
	UploadBuffer& uploadFrameCBuffer(*mFrameCBuffer[mCurrFrameIndex]);
	uploadFrameCBuffer.CopyData(0U, &frameCBuffer, sizeof(frameCBuffer));

	CHECK_HR(cmdAlloc->Reset());
	CHECK_HR(mCmdList->Reset(cmdAlloc, sPSO));

	mCmdList->RSSetViewports(1U, &Settings::sScreenViewport);
	mCmdList->RSSetScissorRects(1U, &Settings::sScissorRect);
	mCmdList->OMSetRenderTargets(mGeometryBuffersCpuDescCount, mGeometryBuffersCpuDescs, false, &mDepthBufferCpuDesc);

	mCmdList->SetDescriptorHeaps(1U, &mCbvSrvUavDescHeap);
	mCmdList->SetGraphicsRootSignature(sRootSign);

	const std::size_t descHandleIncSize{ mDevice.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) };
	D3D12_GPU_DESCRIPTOR_HANDLE objectCBufferGpuDescHandle(mObjectCBufferGpuDescHandleBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE materialsCBufferGpuDescHandle(mMaterialsCBufferGpuDescHandleBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE texturesBufferGpuDescHandle(mTexturesBufferGpuDescHandleBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE normalsBufferGpuDescHandle(mNormalsBufferGpuDescHandleBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE heightsBufferGpuDescHandle(mHeightsBufferGpuDescHandleBegin);

	mCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	// Set frame constants root parameters
	D3D12_GPU_VIRTUAL_ADDRESS frameCBufferGpuVAddress(uploadFrameCBuffer.Resource()->GetGPUVirtualAddress());
	mCmdList->SetGraphicsRootConstantBufferView(1U, frameCBufferGpuVAddress);
	mCmdList->SetGraphicsRootConstantBufferView(2U, frameCBufferGpuVAddress);
	mCmdList->SetGraphicsRootConstantBufferView(5U, frameCBufferGpuVAddress);
	
	// Draw objects
	const std::size_t geomCount{ mGeometryDataVec.size() };
	for (std::size_t i = 0UL; i < geomCount; ++i) {
		GeometryData& geomData{ mGeometryDataVec[i] };
		mCmdList->IASetVertexBuffers(0U, 1U, &geomData.mVertexBufferData.mBufferView);
		mCmdList->IASetIndexBuffer(&geomData.mIndexBufferData.mBufferView);
		const std::size_t worldMatsCount{ geomData.mWorldMatrices.size() };
		for (std::size_t j = 0UL; j < worldMatsCount; ++j) {
			mCmdList->SetGraphicsRootDescriptorTable(0U, objectCBufferGpuDescHandle);
			objectCBufferGpuDescHandle.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(3U, heightsBufferGpuDescHandle);
			heightsBufferGpuDescHandle.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(4U, materialsCBufferGpuDescHandle);
			materialsCBufferGpuDescHandle.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(6U, texturesBufferGpuDescHandle);
			texturesBufferGpuDescHandle.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(7U, normalsBufferGpuDescHandle);
			normalsBufferGpuDescHandle.ptr += descHandleIncSize;
			
			mCmdList->DrawIndexedInstanced(geomData.mIndexBufferData.mCount, 1U, 0U, 0U, 0U);
		}
	}

	mCmdList->Close();

	mCmdListQueue->push(mCmdList);

	// Next frame
	mCurrFrameIndex = (mCurrFrameIndex + 1) % Settings::sQueuedFrameCount;
}

bool HeightCmdListRecorder::ValidateData() const noexcept {
	const bool result =
		GeometryPassCmdListRecorder::ValidateData() &&
		mTexturesBufferGpuDescHandleBegin.ptr != 0UL &&
		mNormalsBufferGpuDescHandleBegin.ptr != 0UL &&
		mHeightsBufferGpuDescHandleBegin.ptr != 0UL;

	return result;
}

void HeightCmdListRecorder::BuildBuffers(
	const Material* materials,
	ID3D12Resource** textures,
	ID3D12Resource** normals,
	ID3D12Resource** heights,
	const std::uint32_t dataCount) noexcept {
	ASSERT(materials != nullptr);
	ASSERT(textures != nullptr);
	ASSERT(normals != nullptr);
	ASSERT(heights != nullptr);
	ASSERT(dataCount != 0UL);

	ASSERT(mCbvSrvUavDescHeap == nullptr);
#ifdef _DEBUG
	for (std::uint32_t i = 0U; i < Settings::sQueuedFrameCount; ++i) {
		ASSERT(mFrameCBuffer[i] == nullptr);
	}
#endif
	ASSERT(mObjectCBuffer == nullptr);
	ASSERT(mMaterialsCBuffer == nullptr);

	// Create CBV_SRV_UAV cbuffer descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc{};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0U;
	// (1 obj cbuffer + 1 material cbuffer + 1 texture + 1 normal texture + 1 height texture) per geometry
	descHeapDesc.NumDescriptors = dataCount * 5U ;
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	ResourceManager::Get().CreateDescriptorHeap(descHeapDesc, mCbvSrvUavDescHeap);

	// Create object cbuffer and fill it
	const std::size_t objCBufferElemSize{ UploadBuffer::CalcConstantBufferByteSize(sizeof(ObjectCBuffer)) };
	ResourceManager::Get().CreateUploadBuffer(objCBufferElemSize, dataCount, mObjectCBuffer);
	mObjectCBufferGpuDescHandleBegin = mCbvSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart();
	std::uint32_t k = 0U;
	const std::size_t numGeomData{ mGeometryDataVec.size() };
	ObjectCBuffer objCBuffer;
	for (std::size_t i = 0UL; i < numGeomData; ++i) {
		GeometryData& geomData{ mGeometryDataVec[i] };
		const std::uint32_t worldMatsCount{ static_cast<std::uint32_t>(geomData.mWorldMatrices.size()) };
		for (std::uint32_t j = 0UL; j < worldMatsCount; ++j) {
			const DirectX::XMMATRIX wMatrix = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&geomData.mWorldMatrices[j]));
			DirectX::XMStoreFloat4x4(&objCBuffer.mWorld, wMatrix);
			mObjectCBuffer->CopyData(k + j, &objCBuffer, sizeof(objCBuffer));
		}

		k += worldMatsCount;
	}

	// Create materials cbuffer		
	const std::size_t matCBufferElemSize{ UploadBuffer::CalcConstantBufferByteSize(sizeof(Material)) };
	ResourceManager::Get().CreateUploadBuffer(matCBufferElemSize, dataCount, mMaterialsCBuffer);
	const std::size_t descHandleIncSize{ ResourceManager::Get().GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) };
	mMaterialsCBufferGpuDescHandleBegin.ptr = mObjectCBufferGpuDescHandleBegin.ptr + dataCount * descHandleIncSize;

	// Set begin for textures in GPU
	mTexturesBufferGpuDescHandleBegin.ptr = mObjectCBufferGpuDescHandleBegin.ptr + dataCount * 2U * descHandleIncSize;

	// Set begin for normals in GPU
	mNormalsBufferGpuDescHandleBegin.ptr = mObjectCBufferGpuDescHandleBegin.ptr + dataCount * 3U * descHandleIncSize;

	// Set begin for heights in GPU
	mHeightsBufferGpuDescHandleBegin.ptr = mObjectCBufferGpuDescHandleBegin.ptr + dataCount * 4U * descHandleIncSize;

	// Create object cbuffer descriptors
	// Create material cbuffer descriptors
	// Fill materials cbuffers data
	D3D12_GPU_VIRTUAL_ADDRESS materialsGpuAddress{ mMaterialsCBuffer->Resource()->GetGPUVirtualAddress() };
	D3D12_GPU_VIRTUAL_ADDRESS objCBufferGpuAddress{ mObjectCBuffer->Resource()->GetGPUVirtualAddress() };
	D3D12_CPU_DESCRIPTOR_HANDLE currObjCBufferDescHandle(mCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart());
	D3D12_CPU_DESCRIPTOR_HANDLE currMaterialCBufferDescHandle{ mCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart().ptr + dataCount * descHandleIncSize };
	D3D12_CPU_DESCRIPTOR_HANDLE currTextureBufferDescHandle{ mCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart().ptr + dataCount * 2U * descHandleIncSize };
	D3D12_CPU_DESCRIPTOR_HANDLE currNormalsBufferDescHandle{ mCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart().ptr + dataCount * 3U * descHandleIncSize };
	D3D12_CPU_DESCRIPTOR_HANDLE currHeightsBufferDescHandle{ mCbvSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart().ptr + dataCount * 4U * descHandleIncSize };

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	for (std::size_t i = 0UL; i < dataCount; ++i) {
		// Create object cbuffers descriptors
		D3D12_CONSTANT_BUFFER_VIEW_DESC cBufferDesc{};
		cBufferDesc.BufferLocation = objCBufferGpuAddress + i * objCBufferElemSize;
		cBufferDesc.SizeInBytes = static_cast<std::uint32_t>(objCBufferElemSize);
		ResourceManager::Get().CreateConstantBufferView(cBufferDesc, currObjCBufferDescHandle);

		// Create materials CBuffer descriptor
		cBufferDesc.BufferLocation = materialsGpuAddress + i * matCBufferElemSize;
		cBufferDesc.SizeInBytes = static_cast<std::uint32_t>(matCBufferElemSize);
		ResourceManager::Get().CreateConstantBufferView(cBufferDesc, currMaterialCBufferDescHandle);

		// Create texture descriptor
		ID3D12Resource* res{ textures[i] };
		srvDesc.Format = res->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = res->GetDesc().MipLevels;
		ResourceManager::Get().CreateShaderResourceView(*res, srvDesc, currTextureBufferDescHandle);

		// Create normal texture descriptor
		res = normals[i];
		srvDesc.Format = res->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = res->GetDesc().MipLevels;
		ResourceManager::Get().CreateShaderResourceView(*res, srvDesc, currNormalsBufferDescHandle);

		// Create height texture descriptor
		res = heights[i];
		srvDesc.Format = res->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = res->GetDesc().MipLevels;
		ResourceManager::Get().CreateShaderResourceView(*res, srvDesc, currHeightsBufferDescHandle);

		mMaterialsCBuffer->CopyData(static_cast<std::uint32_t>(i), &materials[i], sizeof(Material));

		currMaterialCBufferDescHandle.ptr += descHandleIncSize;
		currObjCBufferDescHandle.ptr += descHandleIncSize;
		currTextureBufferDescHandle.ptr += descHandleIncSize;
		currNormalsBufferDescHandle.ptr += descHandleIncSize;
		currHeightsBufferDescHandle.ptr += descHandleIncSize;
	}

	// Create frame cbuffers
	const std::size_t frameCBufferElemSize{ UploadBuffer::CalcConstantBufferByteSize(sizeof(FrameCBuffer)) };
	for (std::uint32_t i = 0U; i < Settings::sQueuedFrameCount; ++i) {
		ResourceManager::Get().CreateUploadBuffer(frameCBufferElemSize, 1U, mFrameCBuffer[i]);
	}
}