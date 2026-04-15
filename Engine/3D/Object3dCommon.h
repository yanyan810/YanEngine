#pragma once
#include "DirectXCommon.h"
#include "Camera.h"
#include "Model.h"

class SrvManager;
class SkinningCommon;

class Object3dCommon
{

public:

    enum class BlendMode {
        //!<ブレンド無し
        kBlendModeNone,
        //!<通常αブレンド。デフォルト。Src* SrcA+Dest*(1-SrcA)
        kBlendModeNormal,
        //!<加算。 Src*SrcA+Dest*1
        kBlendModeAdd,
        //!<減算。Dest*1-Src*SrA
        kBlendModeSubtract,
        //!<乗算。Src*0+Dest*Src
        kBlendModeMultily,
        //!<スクリーン。Src*(1-Dest)+Dest*1
        kBlendModeScreen,
        //!<利用してはいけない
        kCountOfBlendMode,

    };

public:

	void Initialize(DirectXCommon*dxCommon);

    void SetGraphicsPipelineState();

    void SetBlendMode(BlendMode m) { blendMode_ = m; CreateGraphicsPipelineState(); }

    // ★追加：共通依存を持たせる
    void SetSrvManager(SrvManager* srv) { srv_ = srv; }
    void SetSkinningCommon(SkinningCommon* skin) { skinCom_ = skin; }

    SrvManager* GetSrvManager() const { return srv_; }
    SkinningCommon* GetSkinningCommon() const { return skinCom_; }

    //setter
	void SetDefaultCamera(Camera* camera) { this->defaultCamera_ = camera; }
    //getter
	Camera* GetDefaultCamera() const { return defaultCamera_; }

private:

    // ルートシグネチャの作成
    void CreateRootSignature();
    // グラフィックスパイプラインの生成
    void CreateGraphicsPipelineState();



private:

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;

    DirectXCommon* dx_;

    // ※ 今の頂点構造(VertexData)に合わせて POSITION/TEXCOORD/NORMAL の3要素

    D3D12_INPUT_ELEMENT_DESC inputElems_[3] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, position),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, texcoord),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, normal),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_INPUT_LAYOUT_DESC inputLayout_{ inputElems_, 3 };

    //ブレンドモード設定
    BlendMode blendMode_;

	Camera* defaultCamera_ = nullptr;


    // ★追加
    SrvManager* srv_ = nullptr;
    SkinningCommon* skinCom_ = nullptr;

};

