#include "DirectXCommon.h"
#include <cassert>
#include <dxgidebug.h>

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")


using namespace Logger;
using namespace StringUtility;

const uint32_t DirectXCommon::kMaxSRVCount = 512;

/// <summary>
/// 繝・せ繧ｯ繝ｪ繝励ち繝偵・繝励・逕滓・
/// </summary>
//DescriptorHeap縺ｮ菴懈・髢｢謨ｰ
Microsoft::WRL::ComPtr <ID3D12DescriptorHeap>   DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescripters, bool shaderVisible) {

	//繝・ぅ繧ｹ繧ｯ繝ｪ繝励ち繝偵・繝励・逕滓・
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType; // 繝ｬ繝ｳ繝繝ｼ繧ｿ繝ｼ繧ｲ繝・ヨ繝薙Η繝ｼ逕ｨ
	descriptorHeapDesc.NumDescriptors = numDescripters; // 繝繝悶Ν繝舌ャ繝輔ぃ繧医≧縺ｫ2縺､縲ょ､壹￥縺ｦ繧ょ挨縺ｫ謳阪・縺ｪ縺・
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device_->CreateDescriptorHeap(
		&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	//繝・ぅ繧ｹ繧ｯ繝ｪ繝励ち繝偵・繝励′菴懊ｌ縺ｪ縺九▲縺溘・縺ｧ襍ｷ蜍輔〒縺阪↑縺・
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

/// <summary>
///  豺ｱ蠎ｦ繝舌ャ繝輔ぃ繝ｪ繧ｽ繝ｼ繧ｹ縺ｮ險ｭ螳・
/// </summary>
Microsoft::WRL::ComPtr<ID3D12Resource>
DirectXCommon::CreateDepthStencilResource(int32_t width, int32_t height) {	//逕滓・縺吶ｋResource縺ｮ險ｭ螳・
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;//Texture縺ｮ蟷・
	resourceDesc.Height = height;//Texture縺ｮ鬮倥＆
	resourceDesc.MipLevels = 1;//mipmap縺ｮ謨ｰ
	resourceDesc.DepthOrArraySize = 1;//螂･陦後″or驟榊・Texture縺ｮ驟榊・謨ｰ
	resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;//DepthStencil縺ｨSRV縺ｧ蜈ｱ譛峨☆繧九◆繧√・Typeless繝輔か繝ｼ繝槭ャ繝・
	resourceDesc.SampleDesc.Count = 1;//繧ｵ繝ｳ繝励Μ繝ｳ繧ｰ繧ｫ繧ｦ繝ｳ繝医・蝗ｺ螳・
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;//2谺｡蜈・

	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;//DepthStencil縺ｨ縺励※菴ｿ縺・夂衍

	//逅・Φ縺吶ｋHeap縺ｮ險ｭ螳・
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//VRAM荳翫↓菴懊ｋ
	//豺ｱ蠎ｦ蛟､縺ｮ繧ｯ繝ｪ繧｢險ｭ螳・
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;//1.0f(譛螟ｧ蛟､)縺ｧ繧ｯ繝ｪ繧｢
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;//DSV逕ｨ縺ｮ繝輔か繝ｼ繝槭ャ繝・
	//Resource縺ｮ逕滓・
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,//Heap縺ｮ險ｭ螳・
		D3D12_HEAP_FLAG_NONE,//Heap縺ｮ迚ｹ谿翫↑險ｭ螳壹ら音縺ｫ縺ｪ縺・
		&resourceDesc,//Resource縺ｮ險ｭ螳・
		D3D12_RESOURCE_STATE_DEPTH_WRITE,//豺ｱ蠎ｦ蛟､繧呈嶌縺崎ｾｼ繧迥ｶ諷九↓縺励※縺翫￥
		&depthClearValue,//Clear譛驕ｩ蛟､
		IID_PPV_ARGS(&resource));//菴懈・縺吶ｋResource繝昴う繝ｳ繧ｿ縺ｸ縺ｮ繝昴う繝ｳ繧ｿ
	assert(SUCCEEDED(hr));

	resource->SetName(L"DepthStencil");

	return resource;
}


D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index) {
	//CPU蛛ｴ縺ｮ繝・ぅ繧ｹ繧ｯ繝ｪ繝励ち繝上Φ繝峨Ν繧貞叙蠕・
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriporSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index) {
	//CPU蛛ｴ縺ｮ繝・ぅ繧ｹ繧ｯ繝ｪ繝励ち繝上Φ繝峨Ν繧貞叙蠕・
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriporSize * index);
	return handleGPU;
}

// ---- CPU繝上Φ繝峨Ν蜿門ｾ・----


D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV, index);
}

// ---- GPU繝上Φ繝峨Ν蜿門ｾ・----

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(dsvDescriptorHeap_.Get(), descriptorSizeDSV, index);
}

//CompileShader髢｢謨ｰ
Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompilesSharder(
	//Compiler縺吶ｋSharder繝輔ぃ繧､繝ｫ縺ｸ縺ｮ繝代せ
	const std::wstring& filePath,
	//Compiler縺ｫ縺吶ｋ菴ｿ逕ｨ縺吶ｋProfile
	const wchar_t* profile) {
	//縺薙ｌ縺九ｉ繧ｷ繧ｧ繝ｼ繝繝ｼ繧偵さ繝ｳ繝代う繝ｫ縺吶ｋ譌ｨ繧偵Ο繧ｰ縺ｫ縺縺・
	Log(ConvertString(std::format(L"Compile Shader: {}\n", filePath)));

	//hlsl繝輔ぃ繧､繝ｫ繧定ｪｭ繧
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

	//隱ｭ繧√↑縺九▲縺溘ｉ豁｢繧√ｋ
	assert(SUCCEEDED(hr));

	//隱ｭ縺ｿ霎ｼ繧薙□繝輔ぃ繧､繝ｫ縺ｮ蜀・ｮｹ繧定ｨｭ螳壹☆繧・
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;//UTF8縺ｮ譁・ｭ励さ繝ｼ繝峨〒縺ゅｋ縺薙→繧帝夂衍
	LPCWSTR arguments[] = {
	filePath.c_str(),///繧ｳ繝ｳ繝代う繝ｫ蟇ｾ雎｡縺ｮhlsl繝輔ぃ繧､繝ｫ蜷・
	L"-E", L"main",//繧ｨ繝ｳ繝医Μ繝ｼ繝昴う繝ｳ繝医・謖・ｮ壹ょ渕譛ｬ逧・↓main莉･螟悶↓縺ｯ縺励↑縺・
	L"-T", profile,//ShaderProfile縺ｮ險ｭ螳・
	L"-Zi",L"-Qembed_debug",//繝・ヰ繝・げ逕ｨ縺ｮ諠・ｱ繧貞沂繧∬ｾｼ繧
	L"-Od",//譛驕ｩ蛹悶ｒ螟悶＠縺ｦ縺翫￥
	L"-Zpr",//繝｡繝｢繝ｪ繝ｬ繧､繧｢繧ｦ繝医・陦悟━蜈・
	};

	//螳滄圀縺ｫsheder繧偵さ繝ｳ繝代う繝ｫ縺吶ｋ
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,//隱ｭ縺ｿ霎ｼ繧薙□繝輔ぃ繧､繝ｫ
		arguments,//繧ｳ繝ｳ繝代う繝ｫ繧ｪ繝励す繝ｧ繝ｳ
		_countof(arguments),//繧ｳ繝ｳ繝代う繝ｫ繧ｪ繝励す繝ｧ繝ｳ縺ｮ謨ｰ
		includeHandler,//include縺悟性縺ｾ繧後◆隲ｸ縲・
		IID_PPV_ARGS(&shaderResult));//繧ｳ繝ｳ繝代う繝ｫ邨先棡
	//繧ｳ繝ｳ繝代う繝ｫ繧ｨ繝ｩ繝ｼ縺ｧ縺ｯ縺ｪ縺重xc縺瑚ｵｷ蜍輔〒縺阪↑縺・↑縺ｩ縺ｮ閾ｴ蜻ｽ逧・↑迥ｶ豕・
	assert(SUCCEEDED(hr));

	//隴ｦ蜻翫・繧ｨ繝ｩ繝ｼ縺悟・縺ｦ縺溘ｉ繝ｭ繧ｰ縺ｫ蜃ｺ縺・
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
		assert(false); // 竊・譛ｬ蠖薙↓繧ｨ繝ｩ繝ｼ縺後≠繧区凾縺縺第ｭ｢縺ｾ繧・
	}

	//繧ｳ繝ｳ繝代う繝ｫ邨先棡縺九ｉ螳溯｡檎畑縺ｮ繝舌う繝翫Μ驛ｨ蛻・ｒ蜿門ｾ・
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	//謌仙粥縺励◆繝ｭ繧ｰ繧貞・縺・
	Log(ConvertString(std::format(L"Compile Succeded,path:{},profile\n", filePath, profile)));
	//繧ゅ≧菴ｿ繧上↑縺・Μ繧ｽ繝ｼ繧ｹ繧定ｧ｣謾ｾ
	shaderSource->Release();
	//螳溯｡檎畑縺ｮ繝舌う繝翫Μ繧定ｿ泌唆
	return shaderBlob;

}

Microsoft::WRL::ComPtr<ID3D12Resource>DirectXCommon::CreateBufferResource( size_t sizeInBytes) {
	if (sizeInBytes == 0) {
		OutputDebugStringA("[DirectXCommon] CreateBufferResource requested zero bytes. Using 256 bytes instead.\n");
		sizeInBytes = 256;
	}
	sizeInBytes = (sizeInBytes + 0xff) & ~0xff;

	// 繝偵・繝励・險ｭ螳・
	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	// 繝ｪ繧ｽ繝ｼ繧ｹ縺ｮ險ｭ螳・
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer));
	assert(SUCCEEDED(hr));

	// 縺薙％縺ｧ蜷榊燕繧剃ｻ倥￠繧具ｼ亥ｿ・ｦ√↑繧牙ｼ墓焚縺ｧ蜷榊燕貂｡縺呻ｼ・
	if (FAILED(hr) || !buffer) {
		HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : E_POINTER;
		char msg[256]{};
		sprintf_s(msg, "[DirectXCommon] CreateBufferResource failed. size=%zu hr=0x%08X reason=0x%08X\n",
			sizeInBytes, static_cast<unsigned>(hr), static_cast<unsigned>(reason));
		OutputDebugStringA(msg);
		return nullptr;
	}
	buffer->SetName(L"GenericUploadBuffer");

	return buffer;
}

Microsoft::WRL::ComPtr<ID3D12Resource>DirectXCommon::CreateTextureResource(const DirectX::TexMetadata& metadata) {

	//metadata繧偵ｂ縺ｨ縺ｫResource縺ｮ險ｭ螳・
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);//Texture縺ｮ蟷・
	resourceDesc.Height = UINT(metadata.height);;//鬮倥＆
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);//mipmap縺ｮ謨ｰ
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);;//螂･陦後″or驟榊・Texture縺ｮ驟榊・謨ｰ
	resourceDesc.Format = metadata.format;//Texture縺ｮFormat
	resourceDesc.SampleDesc.Count = 1;//繧ｵ繝ｳ繝励Μ繝ｳ繧ｰ繧ｫ繧ｦ繝ｳ繝医・蝗ｺ螳・
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);//Texture縺ｮ谺｡蜈・焚縲よ勸谿ｵ菴ｿ縺｣縺ｦ縺・ｋ縺ｮ縺ｯ2谺｡蜈・

	//蛻ｩ逕ｨ縺吶ｋHeap縺ｮ險ｭ螳壹る撼蟶ｸ縺ｫ迚ｹ谿翫↑驕狗畑縲・2_04ex縺ｧ荳闊ｬ逧・↑繧ｱ繝ｼ繧ｹ縺後≠繧・
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//邏ｰ縺九＞險ｭ螳壹ｒ陦後≧
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;//writeBack繝昴Μ繧ｷ繝ｼ縺ｧCPU繧｢繧ｯ繧ｻ繧ｹ蜿ｯ閭ｽ
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;//繝励Ο繧ｻ繝・し縺ｮ霑代￥縺ｫ驟咲ｽｮ

	//Resource縺ｮ逕滓・
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,//Heap縺ｮ險ｭ螳・
		D3D12_HEAP_FLAG_NONE,//Heap縺ｮ迚ｹ谿翫↑險ｭ螳壹ら音縺ｫ縺ｪ縺・
		&resourceDesc,//Resource縺ｮ險ｭ螳・
		D3D12_RESOURCE_STATE_COPY_DEST,//蛻晏屓縺ｮResourceState縲５exture縺ｯ蝓ｺ譛ｬ隱ｭ繧縺縺・
		nullptr,//Clear譛驕ｩ蝨ｰ縲ゆｽｿ繧上↑縺・・縺ｧnullptr
		IID_PPV_ARGS(&resource));//菴懈・縺吶ｋResource繝昴う繝ｳ繧ｿ縺ｸ縺ｮ繝昴う繝ｳ繧ｿ
	assert(SUCCEEDED(hr));

	resource->SetName(L"TextureResource");

	return resource;
}

// DirectXCommon.cpp
void DirectXCommon::UploadTextureData(
	const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	const DirectX::ScratchImage& mipImages)
{
	// 1) subresource 驟榊・繧堤畑諢・
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device_.Get(),
		mipImages.GetImages(),
		mipImages.GetImageCount(),
		mipImages.GetMetadata(),
		subresources);

	// 2) 荳ｭ髢薙ヰ繝・ヵ繧｡繧剃ｽ懈・・遺ｻ縺薙・ lifetime 縺瑚ｶ・㍾隕・ｼ・
	const UINT numSubresources = UINT(subresources.size());
	const UINT64 intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, numSubresources);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediate = CreateBufferResource(intermediateSize);

	// 3) UpdateSubresources・医Μ繧ｽ繝ｼ繧ｹ縺ｯ COPY_DEST 縺ｧ菴懊▲縺ｦ縺ゅｋ諠ｳ螳夲ｼ・
	UpdateSubresources(commandList.Get(),
		texture.Get(),
		intermediate.Get(),
		0, 0,
		numSubresources,
		subresources.data());

	// 4) GENERIC_READ 縺ｸ驕ｷ遘ｻ
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);

	// 5) 笘・％縺薙′閧晢ｼ壼ｮ溯｡後＠縺ｦ繝輔ぉ繝ｳ繧ｹ蠕・■縺吶ｋ縺ｾ縺ｧ intermediate 繧定ｧ｣謾ｾ縺励↑縺・
	HRESULT hr = commandList->Close();                         assert(SUCCEEDED(hr));
	ID3D12CommandList* lists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, lists);

	fenceValue++;
	hr = commandQueue->Signal(fence.Get(), fenceValue);        assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent); assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// 6) 螳溯｡悟ｮ御ｺ・ｾ後↓繧医≧繧・￥荳ｭ髢薙ヰ繝・ヵ繧｡縺瑚・蜍戊ｧ｣謾ｾ縺輔ｌ縺ｦ繧・OK
	hr = commandAllocator->Reset();                            assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);  assert(SUCCEEDED(hr));
}

// DirectXCommon 蛛ｴ縺ｫ SrvManager* 繧呈戟縺溘○繧・or 蠑墓焚縺ｧ貂｡縺・
void DirectXCommon::SetDescriptorHeaps(ID3D12DescriptorHeap* srvHeap)
{
	ID3D12DescriptorHeap* heaps[] = { srvHeap };
	commandList->SetDescriptorHeaps(1, heaps);
}


void DirectXCommon::Initialize(WinApp* winApp) {

	//NULL讀懷・
	assert(winApp);

	//繝｡繝ｳ繝仙､画焚縺ｫ險倬鹸
	this->winApp_ = winApp;
	//FPS蝗ｺ螳壼・譛溷喧
	InitializeFixFPS();

	DeviceInitialize();
	CommandInitialize();
	SwapChainSpawn();
	DepthBufferSpawn();
	DethCriptorHeapSpawn();
	RenderTargetViewInitialize();
	DepthStencilViewInitialize();
	FanceInitialize();
	ViewPortInitialize();
	SizeringInitialize();
	DXCCompilierSpawn();
//	ImGuiInitialize();

	HRESULT hr = commandList->Close();                                // 縺・▲縺溘ｓ髢峨§繧具ｼ磯幕縺・※縺・※繧０K・・
	hr = commandAllocator->Reset();                                    // 繧｢繝ｭ繧ｱ繝ｼ繧ｿ繧偵Μ繧ｻ繝・ヨ
	hr = commandList->Reset(commandAllocator.Get(), nullptr);          // 髢九″逶ｴ縺呻ｼ遺・驥崎ｦ・ｼ・

	hr = computeCommandList->Close();
	hr = computeCommandAllocator->Reset();
	hr = computeCommandList->Reset(computeCommandAllocator.Get(), nullptr);
}

DirectXCommon::~DirectXCommon() {
	if (fenceEvent) {
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}
}


void DirectXCommon::DeviceInitialize() {

	HRESULT hr;


#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		//繝・ヰ繝・げ繝ｬ繧､繝､繝ｼ繧呈怏蜉ｹ縺ｫ縺吶ｋ
		debugController->EnableDebugLayer();
		//縺輔ｉ縺ｫGPU蛛ｴ縺ｧ繧ゅメ繧ｧ繝・け繧定｡後∴繧九ｈ縺・↓縺吶ｋ
	//	debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif

	//HRESULTWindows邉ｻ縺ｮ繧ｨ繝ｩ繝ｼ繧ｳ繝ｼ繝峨〒縺ゅｊ縲・
	// 髢｢謨ｰ縺梧・蜉溘＠縺溘°縺ｩ縺・°繧担UCCEEDED繝槭け繝ｭ縺ｧ蛻､螳壹〒縺阪ｋ
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	//蛻晄悄蛹悶・譬ｹ譛ｬ逧・↑繧ｨ繝ｩ繝ｼ縺悟・縺溷ｴ蜷医・繝励Ο繧ｰ繝ｩ繝縺碁俣驕輔▲縺ｦ縺・ｋ縺九√←縺・↓繧ゅ〒縺阪↑縺・ｴ蜷医′螟壹＞縺ｮ縺ｧasser縺ｫ縺励※縺翫￥
	assert(SUCCEEDED(hr));

	//菴ｿ逕ｨ縺吶ｋ繧｢繝繝励ち逕ｨ縺ｮ螟画焚,譛蛻昴↓nullptr繧貞・繧後※縺翫￥
	Microsoft::WRL::ComPtr < IDXGIAdapter4> useAdapter = nullptr;
	//濶ｯ縺・・↓繧｢繝繝励ち繧帝ｼ繧
	for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; i++) {
		//繧｢繝繝励ち繝ｼ縺ｮ諠・ｱ繧貞叙蠕励☆繧・
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));//蜿門ｾ励〒縺阪↑縺・・縺ｯ荳螟ｧ莠・
		//繧ｽ繝輔ヨ繧ｦ繧ｧ繧｢縺ｮ繧｢繝繝励ち縺ｧ縺ｪ縺代ｌ縺ｰ謗｡逕ｨ
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			//謗｡逕ｨ縺励◆繧｢繝繝励ち縺ｮ諠・ｱ繧偵Ο繧ｰ縺ｫ蜃ｺ蜉帙Ｘstring縺ｮ譁ｹ縺ｪ縺ｮ縺ｧ豕ｨ諢・
			Log(ConvertString(std::format(L"Use Adapter: {}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr;//繧ｽ繝輔ヨ繧ｦ繧ｧ繧｢繧｢繝繝励ち縺ｮ蝣ｴ蜷医・隕九↑縺九▲縺溘％縺ｨ縺ｫ縺吶ｋ
	}
	//驕ｩ蛻・↑繧｢繝繝励ち縺瑚ｦ九▽縺九ｉ縺ｪ縺九▲縺溘・縺ｧ襍ｷ蜍輔〒縺阪↑縺・
	assert(useAdapter != nullptr);

	//D3D12Device縺ｮ逕滓・

	//讖溯・繝ｬ繝吶Ν縺ｨ繝ｭ繧ｰ蜃ｺ蜉帷畑縺ｮ譁・ｭ怜・
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2","12.1","12.0" };
	//鬮倥＞鬆・↓逕滓・縺ｧ縺阪ｋ縺玖ｩｦ縺励※縺・￥
	for (size_t i = 0; i < _countof(featureLevels); i++) {
		//謗｡逕ｨ縺励◆繧｢繝繝励ち繝ｼ縺ｧ繝・ヰ繧､繧ｹ逕滓・
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
		//逕滓・縺ｧ縺阪◆縺ｮ縺ｧ繝ｭ繧ｰ蜃ｺ蜉帙ｒ陦後▲縺ｦ繝ｫ繝ｼ繝励ｒ謚懊￠繧・
		if (SUCCEEDED(hr)) {
			Log(std::format("Use Feature Level: {}\n", featureLevelStrings[i]));
			break;
		}
	}
	//繝・ヰ繧､繧ｹ縺ｮ逕滓・縺後≧縺ｾ縺上＞縺九↑縺九▲縺溷ｴ蜷・
	assert(device_ != nullptr);
	Log("Complete create D3D12Device!!!\n");//蛻晄悄蛹悶・繝ｭ繧ｰ繧貞・縺・

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		// 隴ｦ蜻翫〒豁｢繧√ｋ縺九←縺・°縺ｯ蠢・ｦ√↓蠢懊§縺ｦ
		//infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		infoQueue->PushStorageFilter(&filter);
	}
#endif

}

void DirectXCommon::CommandInitialize() {

	HRESULT hr;

	//繧ｳ繝槭Φ繝峨い繝ｭ繧ｱ繝ｼ繧ｿ繧堤函謌舌☆繧・

	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator));
	//繧ｳ繝槭Φ繝峨い繝ｭ繧ｱ繝ｼ繧ｿ縺ｮ逕滓・縺後≧縺ｾ縺上＞縺九↑縺九▲縺溘・縺ｧ襍ｷ蜍輔〒縺阪↑縺・
	assert(SUCCEEDED(hr));

	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&computeCommandAllocator));
	assert(SUCCEEDED(hr));

	//繧ｳ繝槭Φ繝峨Μ繧ｹ繝医ｒ逕滓・縺吶ｋ

	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&commandList));
	//繧ｳ繝槭Φ繝峨Μ繧ｹ繝医・逕滓・縺後≧縺ｾ縺上＞縺九↑縺九▲縺溘・縺ｧ襍ｷ蜍輔〒縺阪↑縺・
	assert(SUCCEEDED(hr));

	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		computeCommandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&computeCommandList));
	assert(SUCCEEDED(hr));

	//繧ｳ繝槭Φ繝峨く繝･繝ｼ繧堤函謌舌☆繧・

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device_->CreateCommandQueue(&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue));
	//繧ｳ繝槭Φ繝峨く繝･繝ｼ逕滓・縺悟､ｱ謨励＠縺溷ｴ蜷・
	assert(SUCCEEDED(hr));
}

void DirectXCommon::SwapChainSpawn() {

	HRESULT hr;

	// SwapChain繧堤函謌舌☆繧・
	swapChainDesc.Width = WinApp::kClientWidth;   // 蟷・
	swapChainDesc.Height = WinApp::kClientHeight; // 鬮倥＆
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 繧ｫ繝ｩ繝ｼ蠖｢蠑・
	swapChainDesc.SampleDesc.Count = 1;              // 繝槭Ν繝√し繝ｳ繝励Ν縺励↑縺・
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 謠冗判蟇ｾ雎｡縺ｨ縺励※菴ｿ縺・
	swapChainDesc.BufferCount = 2;                   // 繝繝悶Ν繝舌ャ繝輔ぃ            // 繧ｦ繧｣繝ｳ繝峨え繝｢繝ｼ繝峨〒襍ｷ蜍・
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 繝輔Μ繝・・蠕後・遐ｴ譽・
	//繧ｳ繝槭Φ繝峨く繝･繝ｼ,繧ｦ繧｣繝ｳ繝峨え繝上Φ繝峨Ν,險ｭ螳壹ｒ貂｡縺励※逕滓・縺吶ｋ
	Microsoft::WRL::ComPtr<IDXGISwapChain1> tempSwapChain;
	hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue.Get(), winApp_->GetHwnd(), &swapChainDesc, nullptr, nullptr, &tempSwapChain);
	assert(SUCCEEDED(hr));

	// IDXGISwapChain4 縺ｸ繧｢繝・・繧ｭ繝｣繧ｹ繝・
	hr = tempSwapChain.As(&swapChain);
	assert(SUCCEEDED(hr));


}


void DirectXCommon::DepthBufferSpawn() {

	// 豺ｱ蠎ｦ繧ｹ繝・Φ繧ｷ繝ｫ繝ｪ繧ｽ繝ｼ繧ｹ繧堤函謌撰ｼ医Γ繝ｳ繝舌↓莉｣蜈･・・
	depthStencilResource_ = CreateDepthStencilResource(
		WinApp::kClientWidth, WinApp::kClientHeight);

	// DSV逕ｨ縺ｮ繝偵・繝励ｒ菴懈・・医Γ繝ｳ繝舌↓莉｣蜈･・・
	dsvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// DSV繝薙Η繝ｼ縺ｮ菴懈・
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	Log("Depth buffer and DSV created successfully.\n");
}


void DirectXCommon::DethCriptorHeapSpawn() {

	// Descriptor 繧ｵ繧､繧ｺ蜿門ｾ・

	descriptorSizeRTV = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// RTV繝偵・繝嶺ｽ懈・・・蛟具ｼ・
	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 5, false);

	
}

void DirectXCommon::RenderTargetViewInitialize() {
	HRESULT hr;

	// 繝舌ャ繧ｯ繝舌ャ繝輔ぃ蜿門ｾ暦ｼ・譫壼・・・
	for (UINT i = 0; i < 2; i++) {
		hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
		assert(SUCCEEDED(hr));
	}

	// 笘・RTV繝輔か繝ｼ繝槭ャ繝医ｒ縲後せ繝ｯ繝・・繝√ぉ繧､繝ｳ縺ｨ謠・∴繧九・
	//    竊・DXGI_FORMAT_R8G8B8A8_UNORM 縺悟ｮ牙・
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// RTV菴懈・
	for (UINT i = 0; i < 2; ++i) {
		rtvHandles[i] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, i);
		device_->CreateRenderTargetView(
			swapChainResources[i].Get(),
			&rtvDesc,
			rtvHandles[i]
		);
	}

	Log("RenderTargetViewInitialize: created RTV for both buffers.\n");
}


void DirectXCommon::DepthStencilViewInitialize() {

	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	// RTV縺ｨDSV繧偵ヱ繧､繝励Λ繧､繝ｳ縺ｫ險ｭ螳・
	dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV, 0);

	//commandList->OMSetRenderTargets(1, &rtvHandles[0], FALSE, &dsvHandle);

	Log("Depth Stencil View initialized successfully.\n");
}

void DirectXCommon::FanceInitialize() {

	HRESULT hr;

	fenceValue = 0;
	hr = device_->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);

}

void DirectXCommon::ViewPortInitialize() {

	//繝薙Η繝ｼ繝昴・繝・
	//繧ｯ繝ｩ繧｢繝ｳ繝磯伜沺縺ｮ繧ｵ繧､繧ｺ縺ｨ荳邱偵↓縺励※逕ｻ髱｢蜈ｨ菴薙↓陦ｨ遉ｺ
	viewport.Width = WinApp::kClientWidth;
	viewport.Height = WinApp::kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

}

void DirectXCommon::SizeringInitialize() {

	//繧ｷ繧ｶ繝ｼ遏ｩ蠖｢
	//蝓ｺ譛ｬ逧・↓繝薙Η繝ｼ繝昴・繝医→蜷後§遏ｩ蠖｢縺梧ｧ区・縺輔ｌ繧・
	scissorRect.left = 0;
	scissorRect.right = WinApp::kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = WinApp::kClientHeight;

}

void DirectXCommon::DXCCompilierSpawn() {

	HRESULT hr;

	//dxCompire繧貞・譛溷喧
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	//迴ｾ譎らせ縺ｧinclude縺励↑縺・′縲（nclude縺ｫ蟇ｾ蠢懊☆繧九◆繧√↓險ｭ螳壹＠縺ｦ縺翫￥	
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));


}

void DirectXCommon::DXCCompilerSpawn() {
	// 笘・里蟄倥・繝溘せ邯ｴ繧雁ｮ溯｣・∈霆｢騾・
	DXCCompilierSpawn();
}
//
//void DirectXCommon::ImGuiInitialize() {
//
//
//	//ImGui縺ｮ蛻晄悄蛹悶・
//	//縺薙≧縺・≧繧ゅ・
//	IMGUI_CHECKVERSION();
//	ImGui::CreateContext();
//	ImGui::StyleColorsDark();
//	ImGui_ImplWin32_Init(winApp_->GetHwnd());
//	ImGui_ImplDX12_Init(device_.Get(),
//		swapChainDesc.BufferCount,
//		rtvDesc.Format,
//		srvDescriptorHeap.Get(),
//		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
//		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
//
//}

void DirectXCommon::InitializeFixFPS() {

	reference_ = std::chrono::steady_clock::now();
	fps_ = 0.0f;

}

void DirectXCommon::UpdateFixFPS() {

	using namespace std::chrono;

	//1/60遘偵・縺｣縺溘ｊ縺ｮ譎る俣
	const microseconds kMinTime(uint64_t(1000000.0f / 60.0f));
	//1/60遘偵ｈ繧翫ｏ縺壹°縺ｫ遏ｭ縺・凾髢・
	const microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));

	// 迴ｾ蝨ｨ譎る俣縺ｨ蜑阪ヵ繝ｬ繝ｼ繝縺九ｉ縺ｮ邨碁℃譎る俣
	auto now = steady_clock::now();
	auto elapsed = duration_cast<microseconds>(now - reference_);

	// 縺ｾ縺縺ｻ縺ｨ繧薙←譎る俣縺檎ｵ後▲縺ｦ縺・↑縺代ｌ縺ｰ繧ｹ繝ｪ繝ｼ繝励＠縺ｦ60fps縺ｫ霑代▼縺代ｋ
	if (elapsed < kMinCheckTime) {
		while (steady_clock::now() - reference_ < kMinTime) {
			std::this_thread::sleep_for(microseconds(1));
		}
		// 繧ｹ繝ｪ繝ｼ繝怜ｾ後・豁｣遒ｺ縺ｪ邨碁℃譎る俣繧呈ｸｬ繧顔峩縺・
		now = steady_clock::now();
		elapsed = duration_cast<microseconds>(now - reference_);
	}

	// 谺｡繝輔Ξ繝ｼ繝縺ｮ蝓ｺ貅匁凾髢薙ｒ譖ｴ譁ｰ
	reference_ = now;

	// FPS 險育ｮ暦ｼ育ｵ碁℃遘偵・騾・焚・・
	if (elapsed.count() > 0) {
		float elapsedSec = static_cast<float>(elapsed.count()) / 1'000'000.0f;
		fps_ = 1.0f / elapsedSec;
	}
}


void DirectXCommon::PreDraw() {
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	// Present 竊・RenderTarget
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	// 笘・窶應ｻ翫・窶昴ヰ繝・け繝舌ャ繝輔ぃ縺ｮ RTV 繧偵そ繝・ヨ・亥崋螳・0 繧剃ｽｿ繧上↑縺・ｼ・
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);

	// 繧ｯ繝ｪ繧｢
	const float clearColor[4] = { 0.10f, 0.25f, 0.50f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// VP/Scissor
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

}


// DirectXCommon::PostDraw()

void DirectXCommon::PostDraw() {
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	// 竊・縺薙・繝ｭ繧ｰ縺ｯ隱､隗｣繧呈魚縺丞錐蜑阪↑縺ｮ縺ｧ遘ｻ蜍包ｼ・錐蜑堺ｿｮ豁｣・亥ｾ瑚ｿｰ・・
	// OutputDebugStringA(std::format("[PreDraw] backBufferIndex = {}\n", backBufferIndex).c_str());

	// RenderTarget 竊・Present
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	HRESULT hr = commandList->Close(); assert(SUCCEEDED(hr));
	hr = computeCommandList->Close(); assert(SUCCEEDED(hr));
	
	ID3D12CommandList* lists[] = { computeCommandList.Get(), commandList.Get() };
	commandQueue->ExecuteCommandLists(2, lists);

	UpdateFixFPS();

	hr = swapChain->Present(1, 0);
	if (FAILED(hr)) {
		char buf[256];
		sprintf_s(buf, "[Present] hr=0x%08X\n", hr);
		OutputDebugStringA(buf);

		HRESULT reason = device_->GetDeviceRemovedReason();
		sprintf_s(buf, "[DeviceRemovedReason] 0x%08X\n", reason);
		OutputDebugStringA(buf);
	}

	if (hr == DXGI_STATUS_OCCLUDED) {
		// 逕ｻ髱｢縺碁國繧後※縺・ｋ蝣ｴ蜷医ゅう繝ｳ繝・ャ繧ｯ繧ｹ縺碁ｲ縺ｾ縺ｪ縺・・縺ｯ豁｣蟶ｸ縺ｪ縺ｮ縺ｧ蠕・▽
		Sleep(16);
	}

	// 繝輔ぉ繝ｳ繧ｹ蠕・■ 竊・Reset
	fenceValue++;
	hr = commandQueue->Signal(fence.Get(), fenceValue); assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent); assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}
	hr = commandAllocator->Reset();                           assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr); assert(SUCCEEDED(hr));

	hr = computeCommandAllocator->Reset(); assert(SUCCEEDED(hr));
	hr = computeCommandList->Reset(computeCommandAllocator.Get(), nullptr); assert(SUCCEEDED(hr));

	// 笘・％縺薙〒縲御ｻ翫ヵ繝ｬ繝ｼ繝縺ｮ Present 蠕後阪・ index 繧偵Ο繧ｰ
	const UINT idxAfter = swapChain->GetCurrentBackBufferIndex();
	OutputDebugStringA(std::format("[After Present] backBufferIndex = {}\n", idxAfter).c_str());
}

void DirectXCommon::WaitForGPU() {
	if (!commandQueue || !fence || !fenceEvent) {
		return;
	}

	++fenceValue;
	HRESULT hr = commandQueue->Signal(fence.Get(), fenceValue);
	if (FAILED(hr)) {
		OutputDebugStringA("[DirectXCommon] WaitForGPU Signal failed.\n");
		return;
	}

	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		if (SUCCEEDED(hr)) {
			WaitForSingleObject(fenceEvent, INFINITE);
		} else {
			OutputDebugStringA("[DirectXCommon] WaitForGPU SetEventOnCompletion failed.\n");
		}
	}
}

void DirectXCommon::ReportLiveObjects()
{
#if _DEBUG
	Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
	if (SUCCEEDED(device_.As(&debugDevice))) {
		// 隧ｳ邏ｰ繝ｬ繝昴・繝茨ｼ亥錐蜑阪ｂ蜃ｺ繧具ｼ・
		debugDevice->ReportLiveDeviceObjects(
			D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL
		);
	}
#endif
}

// === BlendDesc ===
D3D12_BLEND_DESC DirectXCommon::GetBlendDesc() const {
	D3D12_BLEND_DESC desc{};
	desc.AlphaToCoverageEnable = FALSE;
	desc.IndependentBlendEnable = FALSE;

	const D3D12_RENDER_TARGET_BLEND_DESC defaultBlend = {
		TRUE, FALSE,
		D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL
	};

	for (int i = 0; i < 8; ++i) {
		desc.RenderTarget[i] = defaultBlend;
	}
	return desc;
}

// === Rasterizer ===
D3D12_RASTERIZER_DESC DirectXCommon::GetRasterizerDesc() const {
	D3D12_RASTERIZER_DESC desc{};
	desc.FillMode = D3D12_FILL_MODE_SOLID;
	desc.CullMode = D3D12_CULL_MODE_BACK;   // CullMode::None 縺ｧ繧・OK
	desc.FrontCounterClockwise = FALSE;
	desc.DepthClipEnable = TRUE;
	return desc;
}

// === DepthStencil ===
D3D12_DEPTH_STENCIL_DESC DirectXCommon::GetDepthStencilDesc() const {
	D3D12_DEPTH_STENCIL_DESC desc{};
	desc.DepthEnable = TRUE;
	desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.StencilEnable = FALSE;
	return desc;
}

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile
) {
	return CompilesSharder(filePath, profile);
}

//=======================
//RenderTexture髢｢謨ｰ
//=======================

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateRenderTextureResource(
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	const Vector4& clearColor)
{
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;
	clearValue.Color[0] = clearColor.x;
	clearValue.Color[1] = clearColor.y;
	clearValue.Color[2] = clearColor.z;
	clearValue.Color[3] = clearColor.w;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearValue,
		IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	resource->SetName(L"RenderTexture");

	return resource;
}

// RenderTexture逕ｨ縺ｮRTV繧剃ｽ懈・縺吶ｋ髢｢謨ｰ
void DirectXCommon::CreateRenderTextureRTV(
	ID3D12Resource* resource,
	uint32_t rtvIndex,
	DXGI_FORMAT format)
{
	assert(resource);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	device_->CreateRenderTargetView(resource, &rtvDesc, handle);
}

// RenderTexture繧呈緒逕ｻ蟇ｾ雎｡縺ｫ縺吶ｋ蜑阪・蜃ｦ逅・
void DirectXCommon::PreDrawRenderTexture(uint32_t rtvIndex, const Vector4& clearColor)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	commandList->OMSetRenderTargets(1, &handle, FALSE, &dsvHandle);

	float color[4] = {
		clearColor.x,
		clearColor.y,
		clearColor.z,
		clearColor.w
	};

	commandList->ClearRenderTargetView(handle, color, 0, nullptr);
	commandList->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);

	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PreDrawPostEffectBuffer(uint32_t rtvIndex)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	// DSVをバインドしない
	commandList->OMSetRenderTargets(1, &handle, FALSE, nullptr);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

// RenderTextureを描画対象から外す後の処理繧呈緒逕ｻ蟇ｾ雎｡縺九ｉ螟悶☆蠕後・蜃ｦ逅・
void DirectXCommon::SetBackBufferRenderTarget()
{
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::SetBackBufferRenderTargetForPostEffect()
{
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	// DSVをバインドしない
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, nullptr);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::TransitionResource(
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES before,
	D3D12_RESOURCE_STATES after)
{
	assert(resource);

	if (before == after) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;

	commandList->ResourceBarrier(1, &barrier);
}
