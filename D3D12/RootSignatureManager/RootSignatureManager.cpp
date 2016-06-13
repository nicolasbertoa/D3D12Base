#include "RootSignatureManager.h"

#include <d3dcompiler.h>

#include <Utils/DebugUtils.h>
#include <Utils/NumberGeneration.h>

std::unique_ptr<RootSignatureManager> RootSignatureManager::gManager = nullptr;

std::size_t RootSignatureManager::CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc, ID3D12RootSignature* &rootSign) noexcept {
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	mMutex.lock();
	CHECK_HR(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
	CHECK_HR(mDevice.CreateRootSignature(0U, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&rootSign)));
	mMutex.unlock();

	const std::size_t id{ NumberGeneration::IncrementalSizeT() };
	RootSignatureById::accessor accessor;
#ifdef _DEBUG
	mRootSignatureById.find(accessor, id);
	ASSERT(accessor.empty());
#endif
	mRootSignatureById.insert(accessor, id);
	accessor->second = Microsoft::WRL::ComPtr<ID3D12RootSignature>(rootSign);
	accessor.release();

	return id;
}

std::size_t RootSignatureManager::CreateRootSignature(const D3D12_SHADER_BYTECODE& shaderByteCode, ID3D12RootSignature* &rootSign) noexcept {
	Microsoft::WRL::ComPtr<ID3DBlob> rootSignBlob{ nullptr };
	mMutex.lock();
	CHECK_HR(D3DGetBlobPart(shaderByteCode.pShaderBytecode, shaderByteCode.BytecodeLength, D3D_BLOB_ROOT_SIGNATURE, 0U, rootSignBlob.GetAddressOf()));
	mDevice.CreateRootSignature(0U, rootSignBlob->GetBufferPointer(), rootSignBlob->GetBufferSize(), IID_PPV_ARGS(&rootSign));
	mMutex.unlock();

	const std::size_t id{ NumberGeneration::IncrementalSizeT() };
	RootSignatureById::accessor accessor;
#ifdef _DEBUG
	mRootSignatureById.find(accessor, id);
	ASSERT(accessor.empty());
#endif
	mRootSignatureById.insert(accessor, id);
	accessor->second = Microsoft::WRL::ComPtr<ID3D12RootSignature>(rootSign);
	accessor.release();

	return id;
}

ID3D12RootSignature& RootSignatureManager::GetRootSignature(const std::size_t id) noexcept {
	RootSignatureById::accessor accessor;
	mRootSignatureById.find(accessor, id);
	ASSERT(!accessor.empty());
	ID3D12RootSignature* rootSign{ accessor->second.Get() };
	accessor.release();

	return *rootSign;
}

void RootSignatureManager::Erase(const std::size_t id) noexcept {
	RootSignatureById::accessor accessor;
	mRootSignatureById.find(accessor, id);
	ASSERT(!accessor.empty());
	mRootSignatureById.erase(accessor);
	accessor.release();
}