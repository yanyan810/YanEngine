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
/// デスクリプタヒープの生成
/// </summary>
//DescriptorHeapの作成関数
Microsoft::WRL::ComPtr <ID3D12DescriptorHeap>   DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescripters, bool shaderVisible) {

	//ディスクリプタヒープの生成
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType; // レンダーターゲットビュー用
	descriptorHeapDesc.NumDescriptors = numDescripters; // ダブルバッファように2つ。多くても別に損はない
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device_->CreateDescriptorHeap(
		&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	//ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));

	return descriptorHeap;
}

/// <summary>
///  深度バッファリソースの設定
/// </summary>
Microsoft::WRL::ComPtr<ID3D12Resource>
DirectXCommon::CreateDepthStencilResource(int32_t width, int32_t height) {	//生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;//Textureの幅
	resourceDesc.Height = height;//Textureの高さ
	resourceDesc.MipLevels = 1;//mipmapの数
	resourceDesc.DepthOrArraySize = 1;//奥行きor配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1;//サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;//2次元

	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;//DepthStencilとして使う通知

	//理想するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//VRAM上に作る
	//深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;//1.0f(最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//DepthStencilとして利用可能なフォーマット
	//Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,//Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊な設定。特になし
		&resourceDesc,//Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE,//深度値を書き込む状態にしておく
		&depthClearValue,//Clear最適値
		IID_PPV_ARGS(&resource));//作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));

	resource->SetName(L"DepthStencil");

	return resource;
}


D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index) {
	//CPU側のディスクリプタハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriporSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index) {
	//CPU側のディスクリプタハンドルを取得
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriporSize * index);
	return handleGPU;
}

// ---- CPUハンドル取得 ----


D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV, index);
}

// ---- GPUハンドル取得 ----

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(dsvDescriptorHeap_.Get(), descriptorSizeDSV, index);
}

//CompileShader関数
Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompilesSharder(
	//CompilerするSharderファイルへのパス
	const std::wstring& filePath,
	//Compilerにする使用するProfile
	const wchar_t* profile) {
	//これからシェーダーをコンパイルする旨をログにだす
	Log(ConvertString(std::format(L"Compile Shader: {}\n", filePath)));

	//hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

	//読めなかったら止める
	assert(SUCCEEDED(hr));

	//読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;//UTF8の文字コードであることを通知
	LPCWSTR arguments[] = {
	filePath.c_str(),///コンパイル対象のhlslファイル名
	L"-E", L"main",//エントリーポイントの指定。基本的にmain以外にはしない
	L"-T", profile,//ShaderProfileの設定
	L"-Zi",L"-Qembed_debug",//デバッグ用の情報を埋め込む
	L"-Od",//最適化を外しておく
	L"-Zpr",//メモリレイアウトは行優先
	};

	//実際にshederをコンパイルする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,//読み込んだファイル
		arguments,//コンパイルオプション
		_countof(arguments),//コンパイルオプションの数
		includeHandler,//includeが含まれた諸々
		IID_PPV_ARGS(&shaderResult));//コンパイル結果
	//コンパイルエラーではなくdxcが起動できないなどの致命的な状況
	assert(SUCCEEDED(hr));

	//警告・エラーが出てたらログに出す
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
		assert(false); // ← 本当にエラーがある時だけ止まる
	}

	//コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	//成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeded,path:{},profile\n", filePath, profile)));
	//もう使わないリソースを解放
	shaderSource->Release();
	//実行用のバイナリを返却
	return shaderBlob;

}

Microsoft::WRL::ComPtr<ID3D12Resource>DirectXCommon::CreateBufferResource( size_t sizeInBytes) {
	//sizeInBytes = (sizeInBytes + 0xff) & ~0xff;

	// ヒープの設定
	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	// リソースの設定
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

	// ここで名前を付ける（必要なら引数で名前渡す）
	buffer->SetName(L"GenericUploadBuffer");

	return buffer;
}

Microsoft::WRL::ComPtr<ID3D12Resource>DirectXCommon::CreateTextureResource(const DirectX::TexMetadata& metadata) {

	//metadataをもとにResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);//Textureの幅
	resourceDesc.Height = UINT(metadata.height);;//高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);//mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);;//奥行きor配列Textureの配列数
	resourceDesc.Format = metadata.format;//TextureのFormat
	resourceDesc.SampleDesc.Count = 1;//サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);//Textureの次元数。普段使っているのは2次元

	//利用するHeapの設定。非常に特殊な運用。02_04exで一般的なケースがある
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;//細かい設定を行う
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;//writeBackポリシーでCPUアクセス可能
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;//プロセッサの近くに配置

	//Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,//Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊な設定。特になし
		&resourceDesc,//Resourceの設定
		D3D12_RESOURCE_STATE_COPY_DEST,//初回のResourceState。Textureは基本読むだけ
		nullptr,//Clear最適地。使わないのでnullptr
		IID_PPV_ARGS(&resource));//作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));

	resource->SetName(L"TextureResource");

	return resource;
}

// DirectXCommon.cpp
void DirectXCommon::UploadTextureData(
	const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	const DirectX::ScratchImage& mipImages)
{
	// 1) subresource 配列を用意
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device_.Get(),
		mipImages.GetImages(),
		mipImages.GetImageCount(),
		mipImages.GetMetadata(),
		subresources);

	// 2) 中間バッファを作成（※この lifetime が超重要）
	const UINT numSubresources = UINT(subresources.size());
	const UINT64 intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, numSubresources);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediate = CreateBufferResource(intermediateSize);

	// 3) UpdateSubresources（リソースは COPY_DEST で作ってある想定）
	UpdateSubresources(commandList.Get(),
		texture.Get(),
		intermediate.Get(),
		0, 0,
		numSubresources,
		subresources.data());

	// 4) GENERIC_READ へ遷移
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);

	// 5) ★ここが肝：実行してフェンス待ちするまで intermediate を解放しない
	HRESULT hr = commandList->Close();                         assert(SUCCEEDED(hr));
	ID3D12CommandList* lists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, lists);

	fenceValue++;
	hr = commandQueue->Signal(fence.Get(), fenceValue);        assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent); assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// 6) 実行完了後にようやく中間バッファが自動解放されても OK
	hr = commandAllocator->Reset();                            assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);  assert(SUCCEEDED(hr));
}

// DirectXCommon 側に SrvManager* を持たせる or 引数で渡す
void DirectXCommon::SetDescriptorHeaps(ID3D12DescriptorHeap* srvHeap)
{
	ID3D12DescriptorHeap* heaps[] = { srvHeap };
	commandList->SetDescriptorHeaps(1, heaps);
}


void DirectXCommon::Initialize(WinApp* winApp) {

	//NULL検出
	assert(winApp);

	//メンバ変数に記録
	this->winApp_ = winApp;
	//FPS固定初期化
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

	HRESULT hr = commandList->Close();                                // いったん閉じる（開いていてもOK）
	hr = commandAllocator->Reset();                                    // アロケータをリセット
	hr = commandList->Reset(commandAllocator.Get(), nullptr);          // 開き直す（←重要）

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
		//デバッグレイヤーを有効にする
		debugController->EnableDebugLayer();
		//さらにGPU側でもチェックを行えるようにする
	//	debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif

	//HRESULTWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	//初期化の根本的なエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでasserにしておく
	assert(SUCCEEDED(hr));

	//使用するアダプタ用の変数,最初にnullptrを入れておく
	Microsoft::WRL::ComPtr < IDXGIAdapter4> useAdapter = nullptr;
	//良い順にアダプタを頼む
	for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; i++) {
		//アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));//取得できないのは一大事
		//ソフトウェアのアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			//採用したアダプタの情報をログに出力。wstringの方なので注意
			Log(ConvertString(std::format(L"Use Adapter: {}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr;//ソフトウェアアダプタの場合は見なかったことにする
	}
	//適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

	//D3D12Deviceの生成

	//機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2","12.1","12.0" };
	//高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); i++) {
		//採用したアダプターでデバイス生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
		//生成できたのでログ出力を行ってループを抜ける
		if (SUCCEEDED(hr)) {
			Log(std::format("Use Feature Level: {}\n", featureLevelStrings[i]));
			break;
		}
	}
	//デバイスの生成がうまくいかなかった場合
	assert(device_ != nullptr);
	Log("Complete create D3D12Device!!!\n");//初期化のログを出す

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		// 警告で止めるかどうかは必要に応じて
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

	//コマンドアロケータを生成する

	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator));
	//コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//コマンドリストを生成する

	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&commandList));
	//コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//コマンドキューを生成する

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device_->CreateCommandQueue(&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue));
	//コマンドキュー生成が失敗した場合
	assert(SUCCEEDED(hr));
}

void DirectXCommon::SwapChainSpawn() {

	HRESULT hr;

	// SwapChainを生成する
	swapChainDesc.Width = WinApp::kClientWidth;   // 幅
	swapChainDesc.Height = WinApp::kClientHeight; // 高さ
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // カラー形式
	swapChainDesc.SampleDesc.Count = 1;              // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画対象として使う
	swapChainDesc.BufferCount = 2;                   // ダブルバッファ            // ウィンドウモードで起動
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // フリップ後は破棄
	//コマンドキュー,ウィンドウハンドル,設定を渡して生成する
	Microsoft::WRL::ComPtr<IDXGISwapChain1> tempSwapChain;
	hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue.Get(), winApp_->GetHwnd(), &swapChainDesc, nullptr, nullptr, &tempSwapChain);
	assert(SUCCEEDED(hr));

	// IDXGISwapChain4 へアップキャスト
	hr = tempSwapChain.As(&swapChain);
	assert(SUCCEEDED(hr));


}


void DirectXCommon::DepthBufferSpawn() {

	// 深度ステンシルリソースを生成（メンバに代入）
	depthStencilResource_ = CreateDepthStencilResource(
		WinApp::kClientWidth, WinApp::kClientHeight);

	// DSV用のヒープを作成（メンバに代入）
	dsvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// DSVビューの作成
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	Log("Depth buffer and DSV created successfully.\n");
}


void DirectXCommon::DethCriptorHeapSpawn() {

	// Descriptor サイズ取得

	descriptorSizeRTV = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// RTVヒープ作成（2個）
	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	
}

void DirectXCommon::RenderTargetViewInitialize() {
	HRESULT hr;

	// バックバッファ取得（2枚分）
	for (UINT i = 0; i < 2; i++) {
		hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
		assert(SUCCEEDED(hr));
	}

	// ★ RTVフォーマットを「スワップチェインと揃える」
	//    → DXGI_FORMAT_R8G8B8A8_UNORM が安全
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// RTV作成
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

	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	// RTVとDSVをパイプラインに設定
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

	//ビューポート
	//クラアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = WinApp::kClientWidth;
	viewport.Height = WinApp::kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

}

void DirectXCommon::SizeringInitialize() {

	//シザー矩形
	//基本的にビューポートと同じ矩形が構成される
	scissorRect.left = 0;
	scissorRect.right = WinApp::kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = WinApp::kClientHeight;

}

void DirectXCommon::DXCCompilierSpawn() {

	HRESULT hr;

	//dxCompireを初期化
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	//現時点でincludeしないが、includeに対応するために設定しておく	
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));


}

void DirectXCommon::DXCCompilerSpawn() {
	// ★既存のミス綴り実装へ転送
	DXCCompilierSpawn();
}
//
//void DirectXCommon::ImGuiInitialize() {
//
//
//	//ImGuiの初期化。
//	//こういうもの
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

	//1/60秒ぴったりの時間
	const microseconds kMinTime(uint64_t(1000000.0f / 60.0f));
	//1/60秒よりわずかに短い時間
	const microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));

	// 現在時間と前フレームからの経過時間
	auto now = steady_clock::now();
	auto elapsed = duration_cast<microseconds>(now - reference_);

	// まだほとんど時間が経っていなければスリープして60fpsに近づける
	if (elapsed < kMinCheckTime) {
		while (steady_clock::now() - reference_ < kMinTime) {
			std::this_thread::sleep_for(microseconds(1));
		}
		// スリープ後の正確な経過時間を測り直す
		now = steady_clock::now();
		elapsed = duration_cast<microseconds>(now - reference_);
	}

	// 次フレームの基準時間を更新
	reference_ = now;

	// FPS 計算（経過秒の逆数）
	if (elapsed.count() > 0) {
		float elapsedSec = static_cast<float>(elapsed.count()) / 1'000'000.0f;
		fps_ = 1.0f / elapsedSec;
	}
}


void DirectXCommon::PreDraw() {
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	// Present → RenderTarget
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	// ★ “今の”バックバッファの RTV をセット（固定 0 を使わない）
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);

	// クリア
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

	// ← このログは誤解を招く名前なので移動＆名前修正（後述）
	// OutputDebugStringA(std::format("[PreDraw] backBufferIndex = {}\n", backBufferIndex).c_str());

	// RenderTarget → Present
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	HRESULT hr = commandList->Close(); assert(SUCCEEDED(hr));
	ID3D12CommandList* lists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, lists);

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
		// 画面が隠れている場合。インデックスが進まないのは正常なので待つ
		Sleep(16);
	}

	// フェンス待ち → Reset
	fenceValue++;
	hr = commandQueue->Signal(fence.Get(), fenceValue); assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent); assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}
	hr = commandAllocator->Reset();                           assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr); assert(SUCCEEDED(hr));

	// ★ここで「今フレームの Present 後」の index をログ
	const UINT idxAfter = swapChain->GetCurrentBackBufferIndex();
	OutputDebugStringA(std::format("[After Present] backBufferIndex = {}\n", idxAfter).c_str());
}

void DirectXCommon::ReportLiveObjects()
{
#if _DEBUG
	Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
	if (SUCCEEDED(device_.As(&debugDevice))) {
		// 詳細レポート（名前も出る）
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
	desc.CullMode = D3D12_CULL_MODE_BACK;   // CullMode::None でも OK
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
