#include "PlayerCombo.h"
#include "Input.h"
#include "Enemy.h"   // Enemy / EnemyManager

#ifdef USE_IMGUI
#include <imgui.h>
#endif



void PlayerCombo::Start(AttackType type) {
	if (attacking_) return;

	attackType_ = type;
	attacking_ = true;
	t_ = 0.0f;      // 攻撃経過時間
	//hitDone_ = false;   // 多段ヒット防止などがあるなら
}

void PlayerCombo::Reset() {
	buf_.clear();
	attacking_ = false;
	attackAir_ = false;
	step_ = 0;
	t_ = 0.0f;
	curBtn_ = AttackBtn::Weak;
	startDirY_ = 0;

	tuningO_ = DefaultO_(); // ★追加
}


void PlayerCombo::Push_(AttackBtn b) { buf_.push_back({ b, bufKeep_ }); }

bool PlayerCombo::Pop_(AttackBtn& out) {
	if (buf_.empty()) return false;
	out = buf_.front().btn;

	char b[64];
	sprintf_s(b, "[Pop] btn=%d\n", (int)out);
	OutputDebugStringA(b);

	buf_.erase(buf_.begin());
	return true;
}


void PlayerCombo::UpdateBuf_(float dt) {
	for (auto& it : buf_) it.life -= dt;
	buf_.erase(std::remove_if(buf_.begin(), buf_.end(),
		[](const BufItem& i) { return i.life <= 0.0f; }), buf_.end());
}

int PlayerCombo::ReadDirY_(const Input& in) const {
	// 押しっぱなし方向を読む（攻撃開始時に固定する）
	// ↑ or W : +1
	if (in.IsKeyPressed(DIK_UP) || in.IsKeyPressed(DIK_W))   return +1;
	// ↓ or S : -1（※今は使ってないけど残してOK）
	if (in.IsKeyPressed(DIK_DOWN) || in.IsKeyPressed(DIK_S)) return -1;
	return 0;
}


AttackData PlayerCombo::GetData_(bool airborne, int step, AttackBtn btn) const {
	AttackData a{};
	if (!airborne) {
		if (btn == AttackBtn::Weak) {
			a.duration = (step == 2) ? 0.42f : 0.34f;
			a.hitStart = 0.08f; a.hitEnd = 0.18f;
		//	a.chainOpen = 0.12f; a.chainClose = a.duration - 0.08f;
			a.knockX = 6.0f + step * 1.0f;
			a.launchY = 7.0f + step * 1.0f;
		} else {
			a.duration = (step == 2) ? 0.55f : 0.48f;
			a.hitStart = 0.12f; a.hitEnd = 0.24f;
		//	a.chainOpen = 0.18f; a.chainClose = a.duration - 0.10f;
			a.knockX = 8.0f + step * 1.0f;
			a.launchY = 9.0f + step * 1.0f;
		}
	} else {
		if (btn == AttackBtn::Weak) {
			a.duration = (step == 2) ? 0.40f : 0.32f;
			a.hitStart = 0.06f; a.hitEnd = 0.16f;
			//a.chainOpen = 0.10f; a.chainClose = a.duration - 0.06f;
			a.knockX = 5.5f + step * 1.0f;
			a.launchY = 6.5f + step * 0.8f;
			a.airFloatOnHit = true;
		} else {
			a.duration = (step == 2) ? 0.52f : 0.44f;
			a.hitStart = 0.10f; a.hitEnd = 0.22f;
		//	a.chainOpen = 0.16f; a.chainClose = a.duration - 0.08f;
			a.knockX = 7.0f + step * 1.2f;
			a.launchY = 8.0f + step * 1.0f;
			a.airFloatOnHit = true;
		}
	}

	// =========================
  // ★ I / O の時間仕様を強制
  // I(Weak): 全体0.2秒、0.1秒で判定出て、0.2秒で消える
  // O(Strong): 全体0.5秒、0.3秒で判定出て、0.5秒で消える
  // =========================
	if (btn == AttackBtn::Weak) { // I
		a.duration = 0.6f;
		a.hitStart = 0.20f;
		a.hitEnd = 0.60f;
		

		a.hbHalfX = 1.2f;
		a.hbHalfY = 1.6f;
		a.hbOffX = 1.2f;
		a.hbOffY = 0.0f;

		// ★ダメージ：Iは軽め（stepで少し増やす例）
		a.damage = 5 + step;   // 1,2,3

	} else { // O(Strong) - ★ImGui調整値で上書き
		// まず既存ロジック（空中/地上の基礎値）で a を作っているが、
		// 最終的には O は tuningO_ を使う方針にする

		a.duration = tuningO_.duration;
		a.hitStart = tuningO_.hitStart;
		a.hitEnd = tuningO_.hitEnd;
		a.chainOpen = tuningO_.chainOpen;
		a.chainClose = tuningO_.chainClose;

		a.hbHalfX = tuningO_.hbHalfX;
		a.hbHalfY = tuningO_.hbHalfY;
		a.hbOffX = tuningO_.hbOffX;
		a.hbOffY = tuningO_.hbOffY;

		a.hitZ = tuningO_.hitZ;
		a.damage = tuningO_.damage;
	}


	return a;
}

AABB2 PlayerCombo::MakeHitBox_(const Vector2& p, int facing, const AttackData& a) const {
	AABB2 hb{};
	hb.x = p.x + a.hbOffX * float(facing);
	hb.y = p.y + a.hbOffY;
	hb.hx = a.hbHalfX;
	hb.hy = a.hbHalfY;
	return hb;
}

AABB2 PlayerCombo::MakeEnemyBody2D_(const Enemy& e) const {
	// Enemy::GetBodyAABB() は 3D AABB（min/max）なので XY だけ使う
	AABB a3 = e.GetBodyAABB();

	float cx = (a3.min.x + a3.max.x) * 0.5f;
	float cy = (a3.min.y + a3.max.y) * 0.5f;
	float hx = (a3.max.x - a3.min.x) * 0.5f;
	float hy = (a3.max.y - a3.min.y) * 0.5f;

	AABB2 b{};
	b.x = cx; b.y = cy;
	b.hx = hx; b.hy = hy;
	return b;
}

void PlayerCombo::StartAttack_(bool airborne, AttackBtn btn, int dirY) {
	hitSet_.clear();

	char b[128];
	sprintf_s(b, "[Combo Start] btn=%d\n", (int)btn);
	OutputDebugStringA(b);

	attacking_ = true;
	attackAir_ = airborne;
	step_ = 0;
	t_ = 0.0f;
	curBtn_ = btn;
	startDirY_ = dirY;
}
//
//void PlayerCombo::NextStep_(bool airborne, AttackBtn btn) {
//	hitSet_.clear();
//
//	char b[64];
//	sprintf_s(b, "[NextStep] btn=%d\n", (int)btn);
//	OutputDebugStringA(b);
//
//	step_ = std::min(step_ + 1, 2);
//	attackAir_ = airborne;
//	curBtn_ = btn;
//
//	// ★チェインで次段に入った瞬間、攻撃判定がすぐ出るようにする
//	AttackData a = GetData_(airborne, step_, btn);
//	t_ = a.hitStart;   // ← ここがポイント（0.10 or 0.30まで待たない）
//}


void PlayerCombo::Update(float dt,
	const Input& in,
	Vector2& playerPos, Vector2& playerVel,
	bool onGround,
	int facing,
	float playerZ,
	EnemyManager& enemyMgr) {

	// ★毎フレーム最初に無効化
	debugHbValid_ = false;
	debugHb3Valid_ = false;

	UpdateBuf_(dt);

	// I/O をバッファへ
	if (!attacking_) {
		if (in.IsKeyTrigger(DIK_I)) { OutputDebugStringA("[Trigger] I\n"); Push_(AttackBtn::Weak); }
		if (in.IsKeyTrigger(DIK_O)) { OutputDebugStringA("[Trigger] O\n"); Push_(AttackBtn::Strong); }
	} else {
		// 事故防止：攻撃中に押されても捨てる（必要なら）
		// if (in.IsKeyTrigger(DIK_I) || in.IsKeyTrigger(DIK_O)) OutputDebugStringA("[Trigger ignored]\n");
	}


	// 開始
	if (!attacking_) {
		AttackBtn b;
		if (Pop_(b)) {
			const bool airborne = !onGround;
			const int dirY = ReadDirY_(in);
			StartAttack_(airborne, b, dirY);
		}
		return;
	}

	const bool airborneNow = !onGround;
	const bool treatAir = attackAir_ || airborneNow;

	AttackData a = GetData_(treatAir, step_, curBtn_);
	lastData_ = a;
	lastDataValid_ = true;

	t_ += dt;


	const bool hitActive = (t_ >= a.hitStart && t_ <= a.hitEnd);
	bool hittingNow = false;

	// ★敵配列を EnemyManager から取る
	auto& enemies = enemyMgr.GetEnemies();

	if (hitActive) {
		AABB2 hb = MakeHitBox_(playerPos, facing, a);

		// ★デバッグ可視化用に保存
		debugHb_ = hb;
		debugHbValid_ = true;

		// ★3D可視化用（Zも含めたヒットボックス）
		debugHb3_ = { hb.x, hb.y, playerZ, hb.hx, hb.hy, a.hitZ };
		debugHb3Valid_ = true;

		for (auto& e : enemies) {
			if (!e.IsAlive()) continue;

			if (hitSet_.contains(&e)) continue;

			AABB2 body2 = MakeEnemyBody2D_(e);
			if (!Intersect(hb, body2)) continue;


			// ★Z判定（奥行き）
			AABB a3 = e.GetBodyAABB();
			float ezC = (a3.min.z + a3.max.z) * 0.5f;
			float ehz = (a3.max.z - a3.min.z) * 0.5f;
			if (std::abs(ezC - playerZ) > (a.hitZ + ehz)) continue;

			hittingNow = true;

			hitSet_.insert(&e);


			// ★ 吹き飛ばしは「↑キー時の上方向のみ」
			float knockX = 0.0f;   // 横吹き飛ばし完全に無し
			float launchY = 0.0f;

			if (startDirY_ > 0) {
				launchY = a.launchY;   // ↑押し時だけ上に飛ばす
			}

			// ボス：ひるませない＆浮かせない（仕様）
			if (e.IsBoss()) {
				e.ApplyHit2D(0.0f, 0.0f, false, a.damage);   // ★damage渡す
			} else {
				e.ApplyHit2D(knockX, launchY, true, a.damage); // ★damage渡す
			}

		}
	}

	// 空中ヒット中だけ浮遊（プレイヤーだけ止める）
	if (!onGround && a.airFloatOnHit) {
		if (hittingNow) {
			playerVel.y = 0.0f;
		}
	}

	if (t_ >= a.duration) {
		attacking_ = false;
		step_ = 0;
		t_ = 0.0f;

		// バッファに溜まってても繋がらないように捨てるなら
		buf_.clear();
	}


}

#ifdef USE_IMGUI
void PlayerCombo::DebugImGui() {
	if (!ImGui::Begin("PlayerCombo Tuning")) {
		ImGui::End();
		return;
	}

	ImGui::Text("O (Strong) Hitbox / Timing");

	ImGui::Separator();

	ImGui::DragFloat("O duration", &tuningO_.duration, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("O hitStart", &tuningO_.hitStart, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("O hitEnd", &tuningO_.hitEnd, 0.01f, 0.0f, 3.0f);

	// 事故防止
	tuningO_.duration = std::max(tuningO_.duration, 0.01f);
	tuningO_.hitStart = std::clamp(tuningO_.hitStart, 0.0f, tuningO_.duration);
	tuningO_.hitEnd = std::clamp(tuningO_.hitEnd, tuningO_.hitStart, tuningO_.duration);

	ImGui::Separator();

	ImGui::DragFloat("O hbHalfX", &tuningO_.hbHalfX, 0.05f, 0.1f, 10.0f);
	ImGui::DragFloat("O hbHalfY", &tuningO_.hbHalfY, 0.05f, 0.1f, 10.0f);
	ImGui::DragFloat("O hbOffX", &tuningO_.hbOffX, 0.05f, -10.0f, 10.0f);
	ImGui::DragFloat("O hbOffY", &tuningO_.hbOffY, 0.05f, -10.0f, 10.0f);

	ImGui::Separator();

	ImGui::DragFloat("O hitZ", &tuningO_.hitZ, 0.05f, 0.1f, 10.0f);
	ImGui::DragInt("O damage", &tuningO_.damage, 1, 0, 999);

	ImGui::Separator();
	if (ImGui::Button("Reset O")) {
		tuningO_ = DefaultO_();
	}

	// 参考：今使ってる攻撃データ
	if (lastDataValid_) {
		ImGui::Separator();
		ImGui::Text("Last Attack Data");
		ImGui::Text("dur %.2f  hit %.2f..%.2f", lastData_.duration, lastData_.hitStart, lastData_.hitEnd);
		ImGui::Text("hb off(%.2f,%.2f) half(%.2f,%.2f) z=%.2f dmg=%d",
			lastData_.hbOffX, lastData_.hbOffY, lastData_.hbHalfX, lastData_.hbHalfY, lastData_.hitZ, lastData_.damage);
	}

	ImGui::End();
}
#endif
