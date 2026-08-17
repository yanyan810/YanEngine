# DebugAI scenarios

Scenario files are JSON documents placed in `scenarios/<GameId>/`.
The Viewer reads the folder name of the configured game project as `GameId`.

Supported scenario fields:

- `id`, `name`, `description`
- `sceneId`: scene that the host should load before running the scenario
- `actor`: `Player`, `Boss`, or `Both`
- `autoRecord`: automatically record replay and coverage while running
- `timeoutSeconds`
- `goals`

Supported goal types:

- `allActions`: use every Action ID in `actionIds`
- `allActionsWithTag`: use every currently available Action with `tag`
- `anyAction`: use at least one Action ID in `actionIds`
- `phaseReached`: observe `game.phase` equal to `value`
- `enemyDamage`: reduce `enemy.hp` by at least `amount`
- `propertyEquals`: observe the string `property` equal to `value`

The Viewer executes scenarios with the local policy, displays live goal progress,
records an optional replay, updates coverage, and writes a result JSON to
`generated/debug_ai/scenarios/results/` in the configured game project.

`Start One` runs the selected scenario. `Run All` captures the connected game's
current observation as a baseline, restores that baseline before every scenario,
and continues even when an individual scenario fails. Each scenario keeps its own
replay and result JSON. A `batch_*.result.json` file summarizes pass/fail counts,
elapsed time, and every individual result path.

For a clean batch, open the intended gameplay scene in its initial state before
pressing `Run All`. The game adapter must implement `RestoreDebugObservation` for
baseline restoration; no engine-specific scene manager is required by the Viewer.
