#pragma once

#include <cstdint>

#include <DXUtils\D3dUtils.h>
#include <Timer\Timer.h>

#if defined(DEBUG) || defined(_DEBUG)                                                                                                                                                            
#define _CRTDBG_MAP_ALLOC          
#include <cstdlib>             
#include <crtdbg.h>               
#endif 

class D3DApp {
protected:
	D3DApp(HINSTANCE hInstance);
	D3DApp(const D3DApp& rhs) = delete;
	D3DApp& operator=(const D3DApp& rhs) = delete;
	virtual ~D3DApp();

public:
	static D3DApp* GetApp() noexcept { return mApp; }

	HINSTANCE AppInst() const noexcept { return mAppInst; }
	HWND MainWnd() const noexcept { return mMainWnd; }
	float AspectRatio() const noexcept { return (float)mWindowWidth / mWindowHeight; }
	
	std::int32_t Run() noexcept;

	virtual void Initialize() noexcept;
	virtual LRESULT MsgProc(HWND hwnd, const std::int32_t msg, WPARAM wParam, LPARAM lParam) noexcept;

protected:
	virtual void CreateRtvAndDsvDescriptorHeaps() noexcept;
	virtual void CreateRtvAndDsv() noexcept;
	virtual void Update(const Timer& timer) noexcept;
	virtual void Draw(const Timer& timer) noexcept = 0;

	virtual void OnMouseMove(const WPARAM btnState, const std::int32_t x, const std::int32_t y) noexcept;
	
	void InitSystems() noexcept;
	void InitMainWindow() noexcept;
	void InitDirect3D() noexcept;
	void CreateCommandObjects() noexcept;
	void CreateSwapChain() noexcept;

	void FlushCommandQueue() noexcept;

	ID3D12Resource* CurrentBackBuffer() const noexcept;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const noexcept;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const noexcept;

	void CalculateFrameStats() noexcept;
	
protected:
	static D3DApp* mApp;

	HINSTANCE mAppInst = nullptr; // application instance handle
	HWND mMainWnd = nullptr; // main window handle
	bool mAppPaused = false;  // is the application paused?

	Timer mTimer;

	Microsoft::WRL::ComPtr<IDXGIFactory4> mDxgiFactory;
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> mD3dDevice;

	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	std::uint64_t mCurrentFence{0U};

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCmdQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCmdList;

	static const std::uint32_t sSwapChainBufferCount{2U};
	std::uint32_t mCurrBackBuffer{0U};
	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[sSwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;

	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT mScissorRect;

	std::uint32_t mRtvDescSize{0U};
	std::uint32_t mDsvDescSize{0U};
	std::uint32_t mCbvSrvUavDescSize{0U};
	std::uint32_t mSamplerDescSize{0U};

	// Derived class should set these in derived constructor to customize starting values.
	DXGI_FORMAT mBackBufferFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
	DXGI_FORMAT mDepthStencilFormat{DXGI_FORMAT_D24_UNORM_S8_UINT};
	int32_t mWindowWidth{1920};
	int32_t mWindowHeight{1080};
};