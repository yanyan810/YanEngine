# FPS foundation reset

## Active game

Startup registers only `Game` and enters it directly. `Game/scene/Main/GameScene.cpp`
contains a fixed camera, a cube Player, one stationary Enemy using
`resources/enemy/boss/boss.gltf`, a cube floor, and directional lighting.
Player and Enemy own their transforms through Object3d. They have initialization,
Update and Draw only; no combat, input, movement, HP, AI or spawning.
The enemy uses its model bind pose with animation playback disabled.

## Removed from the build and runtime

The original source is preserved under `archive/legacy-2.5d` because this copy
has no Git history. `archive/legacy-2.5d/files.txt` lists every retired file.
Do not add that directory to compiler include paths or source globs.

- Old Player, attacks, specials, combo, guard, and 2.5D movement.
- Old Enemy, EnemyManager, BossAI, Bullet, attacks, and spawning.
- Old Main GameScene, combat HUD, battle flow, debug adapter and game profiles.
- Title, GameOver, GameClear and combat TestScene/tuning/trajectory previews.
- ParticleTestScene editor: this scene mixes reusable editing code with old
  player attack timelines and Boss hitbox previews. Its source is archived for
  future extraction; the engine particle/effect systems remain compiled.

The vcxproj and filters contain no references to archived source. Core/GameApp
no longer registers old scenes, preloads old assets, or configures combat bots.
Old battle-specific ImGui controls/docking entries are removed.

## Preserved

Engine rendering, Object3d, Model, animation/skinning, camera, input, collision
math, sound, particles/effects, Sprite, ImGui, SceneManager, resource management,
lighting and post effects are preserved. DebugAI infrastructure is retained,
without attaching a battle adapter or bot to GameScene.
CGTestScene, DebugScene and DebugAITestScene source remains available for engine
verification but is not registered in the minimal game's runtime.
Resources are retained, including Boss and all original assets. Old DebugAI
profiles/examples are retained as reference data and are not loaded by GameScene.

## Build and launch

Open CG2_Setup.sln in Visual Studio with the v143 C++ toolset and Windows SDK.
Build x64 Debug or Release. Start CG2_Setup with the project directory as the
working directory (resources are loaded relative to that directory).

Optional PowerShell helper: `./tools/build.ps1 -Configuration Debug` (or Release).
It normalizes Path/PATH only for the build process and logs to build-Debug.log.
The project uses an MSBuild Copy task for dxcompiler.dll and dxil.dll instead of
an interactive shell copy command.

FPS movement, mouse look, weapons, raycasts, AI, body-part damage and stage
progression are intentionally not implemented.

## Startup robustness

The executable first uses the current directory's resources, then a resources
folder beside the executable, then the source project directory recorded by /FC
in development builds. If none exists it reports an error before initializing
DirectX. Visual Studio's working directory is also explicitly set to ProjectDir.

## Verification (2026-09-14)

- Final x64 Debug build: succeeded, 0 warnings / 0 errors.
- Final x64 Release build: succeeded, 0 warnings / 0 errors.
- Project source/header references: 0 missing files, 0 duplicates, 0 archive references.
- Debug runtime: inspected the rendered fixed camera, cube Player, one Boss in
  stationary bind pose, floor and lighting. No old combat HUD or scene flow.
- Debug window closed normally without an assertion or error dialog.
- 59 legacy source/header files are archived and excluded from compilation.

Build logs: build-Debug.log and build-Release.log in the project directory.
- Release runtime: also launched and visually verified; left open for review.
