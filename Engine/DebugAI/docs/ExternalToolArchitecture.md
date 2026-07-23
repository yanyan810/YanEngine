# DebugAI External Tool Architecture

## Goal

DebugAI is designed as an engine-independent external debugging tool. Game and engine code connect through a small runtime adapter and a versioned protocol.

## Dependency boundary

The common protocol layer must not include YanEngine or game-specific types. In particular, it must not depend on `Vector3`, player classes, boss classes, scene classes, or a fixed game-state structure.

Common data is represented by:

- `DebugVec3`
- `DebugValue`
- `DebugPropertyMap`
- `DebugEntity`
- `DebugObservation`
- `DebugGenericAction`

Game-specific values belong in `DebugPropertyMap`.

## Runtime responsibilities

The game runtime is responsible only for:

- capturing game state through an adapter;
- converting state to protocol data;
- applying actions and replay inputs;
- restoring state when supported;
- transporting messages.

AI decisions, provider API calls, reports, coverage, scenarios, and test management should move to the external tool.

## External AI provider

`DebugAIViewer` can perform a single external decision with **AI Step**. It requests the
current `DebugObservation`, sends only that generic observation and its
`availableActions` to the selected provider, validates the returned `actionId` and bounded
semantic parameters, then sends the action back to the game. Provider output cannot
introduce an unknown action or parameters outside the schema.

### Engine-independent movement contract

Movement-capable actions use these semantic parameters:

- `direction`: canonical `DebugVec3`, where X is right, Y is up, and Z is forward;
- `coordinateSpace`: `World`, `ActorLocal`, `TargetRelative`, or `Screen`;
- `durationFrames`: number of simulation frames to hold the action;
- `targetId`: optional entity ID used by `TargetRelative` movement.

Each game adapter converts this contract to its own axes and input system. A 2D adapter
may ignore an unused axis. Legacy `intParam`, `floatParam`, and `holdFrames` remain only
for compatibility with existing integrations and replay files.

Provider credentials are read only from the Viewer process environment:

- OpenAI: `DEBUGAI_OPENAI_ENABLED=1`, `OPENAI_API_KEY`, optional
  `DEBUGAI_OPENAI_MODEL` and `DEBUGAI_OPENAI_GOAL`.
- Gemini: `DEBUGAI_GEMINI_ENABLED=1`, `GEMINI_API_KEY`, optional
  `DEBUGAI_GEMINI_MODEL` and `DEBUGAI_GEMINI_GOAL`.

For per-PC setup, copy `Engine/DebugAI/config/debug_ai.example.json` to
`debug_ai.local.json` and enter that machine's provider settings. The local file is ignored
by Git. `DEBUGAI_CONFIG_PATH` can point to a different local file. API-key environment
variables override the key in JSON; provider-specific model and goal environment variables
override their JSON values.

OpenAI is selected when both providers are enabled. **AI Step** and **Start Continuous**
run provider calls on a Viewer worker thread, so the window remains responsive. Continuous
mode uses the configured interval (250 to 60000 milliseconds) between completed actions
and can be interrupted with **Stop AI**. Requests are serialized inside the Viewer so that
manual controls and the AI worker cannot mix Named Pipe messages.

## Local policy generation

The Viewer separates API execution from local execution. **Generate Local Policy** sends
the current generic observation, available action IDs, and the user goal to the configured
provider once, then stores `profiles/<gameId>/local_policy.json`. API credentials are never
written to the policy. **Start Local** loads that file and evaluates threat, action ability,
and distance properties without further provider requests. If the file is absent or invalid,
the Viewer uses a built-in safe combat policy.

Local execution uses a game-independent rule evaluator. Rules contain property names,
operators (`equals`, numeric comparisons, and `hysteresisAbove`), priorities, action IDs
or semantic action tags, selection strategy, duration, and interrupt permission. The
evaluator does not include engine types or game classes. It keeps the selected action
locked until its duration expires; only a higher-priority interrupt rule can replace it.
Action profiles may assign tags such as `movement.approach`, `defense.evade`, and
`combat.attack`, allowing different games to use different action IDs.

## Protocol

Every message uses `DebugProtocolMessage` and contains:

- `protocolVersion`
- `gameId`
- `gameVersion`
- `sessionId`
- `messageType`
- `sequence`
- `properties`

Unsupported protocol versions must be rejected instead of being interpreted as the current format.

## Transport

Protocol logic depends only on `IDebugAITransport`. Named Pipe is the current Windows transport. TCP or an in-process test transport can be added without changing protocol or DebugAI logic.

## Replay

Exact input replay runs locally in the game runtime. The external tool manages replay files and commands, but complete frame input data should be transferred before playback instead of being requested once per frame.

## Migration rule

Legacy `DebugGameState` and fixed `DebugAction` remain temporarily for compatibility. New shared features must use the generic protocol types. Game-specific fields must not be added to the common legacy structures during migration.
