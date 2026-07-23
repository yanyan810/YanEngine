#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShObjIdl.h>

#include "DebugProtocol.h"
#include "ExternalGenericAIProvider.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"DebugAIViewerWindow";
constexpr char kPipeName[] = "\\\\.\\pipe\\DebugAI_CG5";
constexpr DWORD kPipeIoTimeoutMilliseconds = 10000;

enum ControlId {
    StartRecordingId = 1001,
    StopRecordingId,
    PlayLatestId,
    StopReplayId,
    RefreshId,
    ExecuteFirstActionId,
    AIStepId,
    AIStartId,
    LocalStartId,
    AIStopId,
    AIIntervalId,
    AIActorModeId,
    AIGoalId,
    ProjectFolderId,
    ScanTargetsId,
    BrowseProjectId,
    ScanProjectId,
    GenerateProfileId,
    GenerateStateProfileId,
    GenerateLocalPolicyId,
    StatusTextId,
};

constexpr UINT kAIStatusMessage = WM_APP + 1;

HWND gStatusText = nullptr;
DebugObservation gLastObservation;
bool gHasObservation = false;
ExternalGenericAIProvider gAIProvider;
HWND gAIInterval = nullptr;
HWND gAIActorMode = nullptr;
HWND gAIGoal = nullptr;
HWND gProjectFolder = nullptr;
HWND gScanTargets = nullptr;
std::thread gAIWorker;
std::atomic_bool gAIWorkerRunning = false;
std::atomic_bool gAIStopRequested = false;
std::atomic_bool gAIConnectionVerified = false;
std::mutex gAIWaitMutex;
std::condition_variable gAIWaitCondition;
std::mutex gAIStatusMutex;
std::string gPendingAIStatus;
std::mutex gTransportMutex;
ULONGLONG gAutoRefreshResumeTick = 0;

enum class ControlledActorMode { Player, Boss, Both };

void PostAIStatus(HWND window, std::string text, bool preserveResult);
bool IsPlayerActorAction(const DebugGenericAction& action);

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ReadWindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0u);
    return text;
}

std::filesystem::path WorkspaceConfigPath() {
    if (!gAIProvider.ConfigPath().empty()) {
        return std::filesystem::path(gAIProvider.ConfigPath()).parent_path() /
            "debug_ai.workspace.local.json";
    }
    return std::filesystem::current_path() / "Engine/DebugAI/config/debug_ai.workspace.local.json";
}

void SaveWorkspaceSettings(const std::filesystem::path& folder, const std::wstring& scanTargets = {}) {
    const auto path = WorkspaceConfigPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    nlohmann::json config = {
        { "schemaVersion", 1 },
        { "projectFolder", WideToUtf8(folder.wstring()) },
        { "scanTargets", WideToUtf8(scanTargets) },
    };
    std::ofstream output(path);
    if (output) output << config.dump(2) << '\n';
}

void SaveProjectFolder(const std::filesystem::path& folder) {
    SaveWorkspaceSettings(folder, gScanTargets ? ReadWindowText(gScanTargets) : std::wstring{});
}

std::wstring LoadProjectFolder() {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return {};
    return Utf8ToWide(config.value("projectFolder", ""));
}

std::wstring LoadScanTargets() {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return {};
    return Utf8ToWide(config.value("scanTargets", ""));
}

bool IsExcludedProjectPath(const std::filesystem::path& path) {
    static const std::set<std::wstring> excluded = {
        L".git", L".vs", L"generated", L"build", L"debug", L"release", L"x64", L"x86",
        L"externals", L"node_modules", L"packages", L"obj", L"bin"
    };
    for (const auto& component : path) {
        std::wstring name = component.wstring();
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
        if (excluded.contains(name)) return true;
    }
    return false;
}

struct ProjectScanResult {
    std::filesystem::path root;
    std::uint64_t bytes = 0;
    std::size_t files = 0;
    std::size_t skippedLarge = 0;
    std::size_t analysisErrors = 0;
    std::string firstAnalysisError;
    std::map<std::string, std::size_t> categories;
    std::map<std::string, std::set<std::string>> actionSources;
    struct SignalCandidate {
        std::string genericProperty;
        std::string symbol;
        std::string valueKind;
        float confidence = 0.0f;
        std::set<std::string> sources;
    };
    std::map<std::string, SignalCandidate> signalCandidates;
    std::map<std::string, std::set<std::string>> stateValues;
    struct AttackRangeCandidate {
        std::string symbol;
        std::string label;
        std::string source;
        double range = 0.0;
        float confidence = 0.0f;
    };
    std::vector<AttackRangeCandidate> attackRangeCandidates;
};

std::string ClassifyProjectFile(const std::filesystem::path& relative) {
    std::wstring value = relative.wstring();
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    if (value.find(L"debugai") != std::wstring::npos || value.find(L"adapter") != std::wstring::npos) return "DebugAI/Adapter";
    if (value.find(L"input") != std::wstring::npos) return "Input";
    if (value.find(L"action") != std::wstring::npos || value.find(L"attack") != std::wstring::npos ||
        value.find(L"player") != std::wstring::npos) return "Player/Action";
    if (value.find(L"enemy") != std::wstring::npos || value.find(L"boss") != std::wstring::npos) return "Enemy/Boss";
    if (value.find(L"scene") != std::wstring::npos) return "Scene";
    if (relative.extension() == L".json") return "JSON/Config";
    return "Other Source";
}

void ExtractActionIds(const std::string& text, const std::string& source,
    std::map<std::string, std::set<std::string>>& actions) {
    static const std::array<std::regex, 3> patterns = {
        std::regex(R"re(action\.name\s*==\s*"([A-Za-z][A-Za-z0-9_.-]+)")re"),
        std::regex(R"re(actionId\s*[=:]\s*"([A-Za-z][A-Za-z0-9_.-]+)")re"),
        std::regex(R"re(\{\s*"([A-Z][A-Za-z0-9_.-]+)"\s*\})re"),
    };
    for (const auto& pattern : patterns) {
        for (std::sregex_iterator match(text.begin(), text.end(), pattern), end; match != end; ++match) {
            actions[(*match)[1].str()].insert(source);
        }
    }
}

std::string LowerAscii(std::string value);

void AddAttackRangeCandidate(ProjectScanResult& result, const std::string& symbol,
    const std::string& label, const std::string& source, double range, float confidence) {
    range = std::abs(range);
    if (!std::isfinite(range) || range < 0.05 || range > 100.0) return;
    result.attackRangeCandidates.push_back({ symbol, label, source, range, confidence });
}

void ExtractAttackRangeCandidates(const std::string& text, const std::string& source,
    const std::string& category, ProjectScanResult& result) {
    if (category != "Player/Action") return;
    static const std::string number = R"re([-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))re";
    static const std::string capturedNumber = "(" + number + ")";

    // Named reach/range constants are the strongest engine-independent hints.
    const std::regex scalarPattern(
        "(?:constexpr\\s+)?(?:const\\s+)?(?:float|double)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*" +
        capturedNumber + "[fF]?");
    for (std::sregex_iterator match(text.begin(), text.end(), scalarPattern), end; match != end; ++match) {
        const std::string symbol = (*match)[1].str();
        const std::string name = LowerAscii(symbol);
        const bool rangeName = name.find("range") != std::string::npos ||
            name.find("reach") != std::string::npos || name.find("radius") != std::string::npos;
        const bool unrelated = name.find("time") != std::string::npos || name.find("sec") != std::string::npos ||
            name.find("interval") != std::string::npos || name.find("count") != std::string::npos ||
            name.find("damage") != std::string::npos || name.find("scale") != std::string::npos ||
            name.find("rate") != std::string::npos;
        if (rangeName && !unrelated)
            AddAttackRangeCandidate(result, symbol, {}, source, std::stod((*match)[2].str()), 0.78f);
    }

    // Hitbox half-size vectors provide a conservative reach when no explicit
    // range constant exists.
    const std::regex vectorPattern(
        "(?:constexpr\\s+)?(?:const\\s+)?(?:Vector[234]|XMFLOAT3)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\\{\\s*" +
        capturedNumber + "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*,\\s*" +
        capturedNumber + "[fF]?\\s*\\}");
    for (std::sregex_iterator match(text.begin(), text.end(), vectorPattern), end; match != end; ++match) {
        const std::string symbol = (*match)[1].str();
        const std::string name = LowerAscii(symbol);
        if (name.find("hitbox") == std::string::npos && name.find("halfsize") == std::string::npos &&
            name.find("range") == std::string::npos && name.find("reach") == std::string::npos) continue;
        const double x = std::abs(std::stod((*match)[2].str()));
        const double z = std::abs(std::stod((*match)[4].str()));
        AddAttackRangeCandidate(result, symbol, {}, source, (std::max)(x, z), 0.66f);
    }

    // Common data-driven attack definition: label, center offset, half-size.
    // Reach along the attack direction is abs(offset.x) + halfSize.x.
    const std::regex definitionPattern(
        "\\{\\s*\"([^\"]+)\"\\s*,\\s*\\{\\s*" + capturedNumber +
        "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*,\\s*" + capturedNumber +
        "[fF]?\\s*\\}\\s*,\\s*\\{\\s*" + capturedNumber +
        "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*,\\s*" + capturedNumber + "[fF]?\\s*\\}");
    for (std::sregex_iterator match(text.begin(), text.end(), definitionPattern), end; match != end; ++match) {
        const double offsetX = std::abs(std::stod((*match)[2].str()));
        const double halfSizeX = std::abs(std::stod((*match)[5].str()));
        AddAttackRangeCandidate(result, "attackDefinition", (*match)[1].str(), source,
            offsetX + halfSizeX, 0.90f);
    }
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void AddSignalCandidate(ProjectScanResult& result, const std::string& property,
    const std::string& symbol, const std::string& kind, float confidence, const std::string& source) {
    const std::string key = property + "\n" + symbol;
    auto& candidate = result.signalCandidates[key];
    candidate.genericProperty = property;
    candidate.symbol = symbol;
    candidate.valueKind = kind;
    if (confidence > candidate.confidence) candidate.confidence = confidence;
    candidate.sources.insert(source);
}

void ExtractStateSignals(const std::string& text, const std::string& source,
    const std::string& category, ProjectScanResult& result) {
    // Extract declared fields and zero-argument getters. This intentionally uses names only;
    // generated mappings remain reviewable candidates and never execute source code.
    static const std::regex symbolPattern(
        R"re(\b(bool|float|double|int|uint\w*|size_t|State|Phase|PlayerAction)\s+((?:Get|Is|Can|Has|Did)?[A-Za-z_][A-Za-z0-9_]*)\s*(?:\(\s*\)\s*const|[_{=;]))re");
    const bool bossSource = category == "Enemy/Boss";
    const bool playerSource = category == "Player/Action" || category == "Input";
    for (std::sregex_iterator match(text.begin(), text.end(), symbolPattern), end; match != end; ++match) {
        const std::string kind = (*match)[1].str();
        const std::string symbol = (*match)[2].str();
        const std::string name = LowerAscii(symbol);
        if (bossSource) {
            if (name == "getstate" || name == "state" || name == "state_" || name == "st_")
                AddSignalCandidate(result, "enemy.intent", symbol, kind, 0.92f, source);
            if (name.find("attackactive") != std::string::npos || name.find("isattacking") != std::string::npos ||
                name.find("requestmeleeattack") != std::string::npos)
                AddSignalCandidate(result, "enemy.attackActive", symbol, kind, 0.92f, source);
            if ((name.find("startup") != std::string::npos || name.find("windup") != std::string::npos ||
                 name.find("charge") != std::string::npos) &&
                (name.find("frame") != std::string::npos || name.find("time") != std::string::npos || name.find("sec") != std::string::npos))
                AddSignalCandidate(result, "enemy.attackStartup", symbol, kind, 0.84f, source);
            if (name.find("attackrange") != std::string::npos || name.find("attackhitbox") != std::string::npos)
                AddSignalCandidate(result, "enemy.attackRange", symbol, kind, 0.88f, source);
            if (name.find("dist") != std::string::npos &&
                (name.find("player") != std::string::npos || name.find("target") != std::string::npos || name.find("chase") != std::string::npos))
                AddSignalCandidate(result, "enemy.distanceToPlayer", symbol, kind, 0.72f, source);
            if (name == "getphase" || name == "phase" || name == "phase_")
                AddSignalCandidate(result, "enemy.phase", symbol, kind, 0.90f, source);
        }
        if (playerSource) {
            if (name.find("isonground") != std::string::npos || name == "onground")
                AddSignalCandidate(result, "player.onGround", symbol, kind, 0.96f, source);
            if (name.find("canstartattack") != std::string::npos || name.find("canattack") != std::string::npos)
                AddSignalCandidate(result, "player.canAttack", symbol, kind, 0.90f, source);
            if (name.find("currentaction") != std::string::npos)
                AddSignalCandidate(result, "player.action", symbol, kind, 0.91f, source);
            if (name.find("currentattacktype") != std::string::npos || name == "attacktype_")
                AddSignalCandidate(result, "player.attackType", symbol, kind, 0.91f, source);
        }
    }

    // Also inspect identifier references and functions with parameters. This finds
    // helpers such as IsBossIncomingAttack(state), not only fields/getters.
    static const std::regex identifierPattern(R"re(\b([A-Za-z_][A-Za-z0-9_]*)\b)re");
    for (std::sregex_iterator match(text.begin(), text.end(), identifierPattern), end; match != end; ++match) {
        const std::string symbol = (*match)[1].str();
        const std::string name = LowerAscii(symbol);
        if (bossSource) {
            if (name.find("threat") != std::string::npos || name.find("incomingattack") != std::string::npos ||
                name.find("danger") != std::string::npos)
                AddSignalCandidate(result, "enemy.threat", symbol, "inferred", 0.86f, source);
            if (name.find("attackactive") != std::string::npos || name.find("isattacking") != std::string::npos ||
                name.find("requestmeleeattack") != std::string::npos)
                AddSignalCandidate(result, "enemy.attackActive", symbol, "bool", 0.88f, source);
            if ((name.find("distance") != std::string::npos || name.find("dist") != std::string::npos) &&
                (name.find("player") != std::string::npos || name.find("target") != std::string::npos ||
                 name.find("chase") != std::string::npos))
                AddSignalCandidate(result, "enemy.distanceToPlayer", symbol, "number", 0.78f, source);
        }
        if (playerSource) {
            if (name.find("canmove") != std::string::npos || name.find("movementenabled") != std::string::npos)
                AddSignalCandidate(result, "player.canMove", symbol, "bool", 0.90f, source);
            if (name.find("canjump") != std::string::npos || name.find("jumpenabled") != std::string::npos)
                AddSignalCandidate(result, "player.canJump", symbol, "bool", 0.90f, source);
            if (name.find("canattack") != std::string::npos || name.find("canstartattack") != std::string::npos)
                AddSignalCandidate(result, "player.canAttack", symbol, "bool", 0.90f, source);
            if (name.find("isattacking") != std::string::npos || name.find("attackactive") != std::string::npos ||
                name.find("currentattacktype") != std::string::npos)
                AddSignalCandidate(result, "player.isAttacking", symbol, "bool/inferred", 0.84f, source);
        }
    }

    static const std::regex enumPattern(R"re(enum\s+class\s+(State|Phase)\s*(?::[^\{]+)?\{([^\}]+)\})re");
    static const std::regex valuePattern(R"re(\b([A-Za-z][A-Za-z0-9_]*)\b\s*(?:=[^,]+)?(?:,|$))re");
    for (std::sregex_iterator match(text.begin(), text.end(), enumPattern), end; match != end; ++match) {
        const std::string enumName = (*match)[1].str();
        const std::string body = (*match)[2].str();
        for (std::sregex_iterator value(body.begin(), body.end(), valuePattern), valueEnd; value != valueEnd; ++value) {
            result.stateValues[enumName].insert((*value)[1].str());
        }
    }
}

bool AnalyzeProjectFolder(const std::filesystem::path& root, ProjectScanResult& result) {
    std::error_code rootError;
    if (!std::filesystem::is_directory(root, rootError) || rootError) return false;
    result.root = root;
    static const std::set<std::wstring> extensions = {
        L".cpp", L".c", L".h", L".hpp", L".cs", L".json", L".lua", L".py"
    };
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root,
        std::filesystem::directory_options::skip_permission_denied, error), end;
    for (; iterator != end && result.files < 5000; iterator.increment(error)) {
        if (error) { error.clear(); continue; }
        const auto& entry = *iterator;
        if (entry.is_directory(error) && IsExcludedProjectPath(entry.path().lexically_relative(root))) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(error) || IsExcludedProjectPath(entry.path().lexically_relative(root))) continue;
        std::wstring filename = entry.path().filename().wstring();
        std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);
        if (filename == L".env" || filename.ends_with(L".user") ||
            filename.find(L"secret") != std::wstring::npos ||
            filename.find(L"credential") != std::wstring::npos ||
            filename == L"debug_ai.local.json" || filename == L"debug_ai.workspace.local.json") continue;
        std::wstring extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (!extensions.contains(extension)) continue;
        const auto size = entry.file_size(error);
        if (error) { error.clear(); continue; }
        if (size > 512 * 1024) { ++result.skippedLarge; continue; }
        const auto relative = entry.path().lexically_relative(root);
        ++result.files;
        result.bytes += size;
        const std::string category = ClassifyProjectFile(relative);
        ++result.categories[category];
        std::ifstream input(entry.path(), std::ios::binary);
        if (input) {
            try {
                std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                const std::string source = WideToUtf8(relative.wstring());
                ExtractActionIds(text, source, result.actionSources);
                ExtractStateSignals(text, source, category, result);
                ExtractAttackRangeCandidates(text, source, category, result);
            } catch (const std::exception& exception) {
                ++result.analysisErrors;
                if (result.firstAnalysisError.empty()) {
                    result.firstAnalysisError = WideToUtf8(relative.wstring()) + ": " + exception.what();
                }
            } catch (...) {
                ++result.analysisErrors;
                if (result.firstAnalysisError.empty()) {
                    result.firstAnalysisError = WideToUtf8(relative.wstring()) + ": unknown analysis error";
                }
            }
        }
    }
    return true;
}

std::string ScanProjectFolder(const std::filesystem::path& root) {
    ProjectScanResult result;
    if (!AnalyzeProjectFolder(root, result)) return "Project scan failed: folder does not exist.";
    std::ostringstream output;
    output << "Project scan completed.\r\nFolder: " << WideToUtf8(root.wstring())
        << "\r\nSafe candidate files: " << result.files
        << "\r\nTotal candidate bytes: " << result.bytes
        << "\r\nLarge files skipped: " << result.skippedLarge
        << "\r\nFiles skipped after analysis error: " << result.analysisErrors;
    if (!result.firstAnalysisError.empty()) {
        output << "\r\nFirst analysis error: " << result.firstAnalysisError;
    }
    output << "\r\n\r\nClassification:";
    for (const auto& [category, count] : result.categories) output << "\r\n  " << category << ": " << count;
    output << "\r\n\r\nDiscovered Action IDs: " << result.actionSources.size()
        << "\r\nState mapping candidates: " << result.signalCandidates.size()
        << "\r\nAttack range candidates: " << result.attackRangeCandidates.size()
        << "\r\nNo source files were sent to an API.";
    return output.str();
}

std::filesystem::path StateProfilePath(const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "profiles" / gameId / "state_mapping_profile.json";
}

std::filesystem::path LocalPolicyPath(const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "profiles" / gameId / "local_policy.json";
}

std::vector<std::string> ParseScanTargets(const std::string& text) {
    std::vector<std::string> targets;
    std::string current;
    auto append = [&] {
        const auto first = current.find_first_not_of(" \t\r\n");
        const auto last = current.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) targets.push_back(LowerAscii(current.substr(first, last - first + 1)));
        current.clear();
    };
    for (char c : text) {
        if (c == ',' || c == ';' || c == '\r' || c == '\n') append();
        else current.push_back(c);
    }
    append();
    return targets;
}

bool MatchesScanTargets(const ProjectScanResult::SignalCandidate& candidate,
    const std::vector<std::string>& targets) {
    if (targets.empty()) return true;
    std::string searchable = LowerAscii(candidate.genericProperty + " " + candidate.symbol);
    for (const auto& source : candidate.sources) searchable += " " + LowerAscii(source);
    return std::any_of(targets.begin(), targets.end(), [&](const std::string& target) {
        return searchable.find(target) != std::string::npos;
    });
}

std::string GenerateStateMappingProfile(const std::filesystem::path& root, const std::string& scanTargetText) {
    ProjectScanResult result;
    if (!AnalyzeProjectFolder(root, result)) return "State Mapping generation failed: folder does not exist.";
    const auto scanTargets = ParseScanTargets(scanTargetText);
    nlohmann::json mappings = nlohmann::json::array();
    for (const auto& [key, candidate] : result.signalCandidates) {
        if (!MatchesScanTargets(candidate, scanTargets)) continue;
        mappings.push_back({
            { "genericProperty", candidate.genericProperty },
            { "sourceSymbol", candidate.symbol },
            { "valueKind", candidate.valueKind },
            { "confidence", candidate.confidence },
            { "sources", candidate.sources },
            { "approved", false },
            { "runtimeObserved", false },
        });
    }
    nlohmann::json enumValues = nlohmann::json::object();
    for (const auto& [name, values] : result.stateValues) enumValues[name] = values;
    std::string gameId = WideToUtf8(root.filename().wstring());
    if (gameId.empty()) gameId = "game";
    nlohmann::json profile = {
        { "schemaVersion", 1 }, { "gameId", gameId }, { "generatedLocally", true },
        { "reviewRequired", true }, { "mappings", std::move(mappings) },
        { "discoveredEnumValues", std::move(enumValues) },
        { "scanTargets", scanTargets },
    };
    const auto outputPath = StateProfilePath(root);
    std::error_code error;
    std::filesystem::create_directories(outputPath.parent_path(), error);
    std::ofstream output(outputPath, std::ios::trunc);
    if (!output) return "State Mapping generation failed: output file could not be opened.";
    output << profile.dump(2) << '\n';
    return "State Mapping Profile generated locally.\r\nPath: " + WideToUtf8(outputPath.wstring()) +
        "\r\nMapping candidates: " + std::to_string(profile["mappings"].size()) +
        "\r\nFiles skipped after analysis error: " + std::to_string(result.analysisErrors) +
        (result.firstAnalysisError.empty() ? std::string{} :
            "\r\nFirst analysis error: " + result.firstAnalysisError) +
        "\r\nScan targets: " + (scanTargets.empty() ? std::string("automatic (all)") : scanTargetText) +
        "\r\nSet approved=true only after checking each mapping.\r\nNo source files were sent to an API.";
}

std::string GuessActionCategory(const std::string& actionId) {
    std::string value = actionId;
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value.find("attack") != std::string::npos) return "attack";
    if (value.find("guard") != std::string::npos) return "defense";
    if (value.find("move") != std::string::npos || value.find("retreat") != std::string::npos) return "movement";
    if (value.find("jump") != std::string::npos || value.find("dodge") != std::string::npos) return "mobility";
    if (value.find("wait") != std::string::npos) return "idle";
    if (value.find("skip") != std::string::npos) return "flow";
    return "generic";
}

std::vector<std::string> GuessActionTags(const std::string& actionId) {
    const std::string category = GuessActionCategory(actionId);
    std::vector<std::string> tags{ category };
    const std::string name = LowerAscii(actionId);
    if (category == "attack") tags.push_back("combat.attack");
    if (name.find("move") != std::string::npos) tags.push_back("movement.approach");
    if (name.find("retreat") != std::string::npos) tags.push_back("movement.retreat");
    if (name.find("dodge") != std::string::npos || name.find("retreat") != std::string::npos ||
        name.find("guard") != std::string::npos) tags.push_back("defense.evade");
    if (name.find("jump") != std::string::npos) tags.push_back("mobility.jump");
    return tags;
}

nlohmann::json EstimateActionRangeProperties(const std::string& actionId,
    const ProjectScanResult& scan) {
    if (GuessActionCategory(actionId) != "attack") return nlohmann::json::object();
    const std::string action = LowerAscii(actionId);
    const auto relevance = [&](const ProjectScanResult::AttackRangeCandidate& candidate) {
        const std::string evidence = LowerAscii(candidate.source + " " + candidate.symbol + " " + candidate.label);
        int score = 0;
        const auto matches = [&](const char* token) {
            return action.find(token) != std::string::npos && evidence.find(token) != std::string::npos;
        };
        for (const char* token : { "neutral", "side", "up", "down", "smash" })
            if (matches(token)) score += 5;
        if (action.find("weak") != std::string::npos &&
            (evidence.find("ground neutral") != std::string::npos ||
             evidence.find("air neutral") != std::string::npos)) score += 7;
        if (action.find("tilt") != std::string::npos && evidence.find("ground side") != std::string::npos)
            score += 7;
        if (action.find("special") != std::string::npos && evidence.find("playerattacki") != std::string::npos)
            score += 2;
        return score;
    };

    int bestScore = 0;
    double estimatedRange = 3.0; // Safe fallback for an unclassified close-range attack.
    float confidence = 0.25f;
    nlohmann::json evidence = nlohmann::json::array();
    for (const auto& candidate : scan.attackRangeCandidates) {
        const int score = relevance(candidate);
        if (score <= 0) continue;
        if (score > bestScore) {
            bestScore = score;
            estimatedRange = candidate.range;
            confidence = candidate.confidence;
            evidence = nlohmann::json::array();
        } else if (score == bestScore) {
            estimatedRange = (std::max)(estimatedRange, candidate.range);
            confidence = (std::max)(confidence, candidate.confidence);
        } else {
            continue;
        }
        if (evidence.size() < 5) {
            evidence.push_back({
                { "source", candidate.source }, { "symbol", candidate.symbol },
                { "label", candidate.label }, { "range", candidate.range },
                { "confidence", candidate.confidence },
            });
        }
    }
    // Source hitboxes normally describe the attack volume, while runtime
    // distance is center-to-center. Add a small generic target-body allowance.
    if (bestScore > 0) estimatedRange += 1.0;
    estimatedRange = std::clamp(estimatedRange, 0.5, 25.0);
    return {
        { "estimatedRangeMax", estimatedRange },
        { "rangeConfidence", confidence },
        { "rangeSource", bestScore > 0 ? "source_scan" : "conservative_default" },
        { "requiresFacing", true },
        { "rangeEvidence", std::move(evidence) },
    };
}

std::filesystem::path ActionProfilePath(const std::filesystem::path& projectRoot) {
    std::string gameId = WideToUtf8(projectRoot.filename().wstring());
    if (gameId.empty()) gameId = "game";
    return WorkspaceConfigPath().parent_path().parent_path() /
        "profiles" / gameId / "action_profile.json";
}

std::filesystem::path ConfiguredProjectRoot() {
    std::ifstream input(WorkspaceConfigPath());
    const auto config = nlohmann::json::parse(input, nullptr, false);
    if (config.is_discarded() || !config.is_object()) return {};
    return std::filesystem::path(Utf8ToWide(config.value("projectFolder", "")));
}

std::string GenerateActionProfile(const std::filesystem::path& root) {
    ProjectScanResult result;
    if (!AnalyzeProjectFolder(root, result)) return "Action Profile generation failed: folder does not exist.";
    std::map<std::string, nlohmann::json> existingActions;
    {
        std::ifstream existingInput(ActionProfilePath(root));
        const auto existingProfile = nlohmann::json::parse(existingInput, nullptr, false);
        if (!existingProfile.is_discarded() && existingProfile.is_object() &&
            existingProfile.contains("actions") && existingProfile["actions"].is_array()) {
            for (const auto& entry : existingProfile["actions"]) {
                if (entry.is_object() && !entry.value("actionId", "").empty())
                    existingActions[entry.value("actionId", "")] = entry;
            }
        }
    }
    nlohmann::json actions = nlohmann::json::array();
    for (const auto& [actionId, sources] : result.actionSources) {
        nlohmann::json entry = existingActions.contains(actionId)
            ? existingActions[actionId] : nlohmann::json::object();
        entry["actionId"] = actionId;
        entry["category"] = GuessActionCategory(actionId);
        entry["tags"] = GuessActionTags(actionId);
        entry["enabled"] = entry.value("enabled", true);
        entry["sourceDiscovered"] = true;
        entry["runtimeObserved"] = entry.value("runtimeObserved", false);
        entry["availableNow"] = entry.value("availableNow", false);
        entry["verified"] = entry.value("verified", false);
        entry["enabledForLocalAI"] = entry.value("enabledForLocalAI", false);
        entry["sources"] = sources;
        nlohmann::json properties = entry.value("properties", nlohmann::json::object());
        const auto estimated = EstimateActionRangeProperties(actionId, result);
        for (auto property = estimated.begin(); property != estimated.end(); ++property)
            properties[property.key()] = property.value();
        entry["properties"] = std::move(properties);
        actions.push_back(std::move(entry));
        existingActions.erase(actionId);
    }
    for (auto& [actionId, entry] : existingActions) actions.push_back(std::move(entry));
    std::string gameId = WideToUtf8(root.filename().wstring());
    if (gameId.empty()) gameId = "game";
    const auto outputPath = ActionProfilePath(root);
    std::error_code error;
    std::filesystem::create_directories(outputPath.parent_path(), error);
    nlohmann::json profile = {
        { "schemaVersion", 1 },
        { "gameId", gameId },
        { "generatedLocally", true },
        { "sourceFileCount", result.files },
        { "actions", std::move(actions) },
    };
    std::ofstream output(outputPath);
    if (!output) return "Action Profile generation failed: output file could not be opened.";
    output << profile.dump(2) << '\n';
    return "Action Profile generated locally.\r\nPath: " + WideToUtf8(outputPath.wstring()) +
        "\r\nActions discovered: " + std::to_string(result.actionSources.size()) +
        "\r\nAttack range candidates: " + std::to_string(result.attackRangeCandidates.size()) +
        "\r\nReview categories and properties before using the profile.\r\nNo source files were sent to an API.";
}

bool UpdateActionProfileFromRuntime(const DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return false;
    const auto path = ActionProfilePath(projectRoot);
    std::ifstream input(path);
    auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object()) return false;
    if (!profile.contains("actions") || !profile["actions"].is_array()) profile["actions"] = nlohmann::json::array();

    std::set<std::string> available;
    for (const auto& action : observation.availableActions) available.insert(action.actionId);
    std::set<std::string> matched;
    for (auto& action : profile["actions"]) {
        if (!action.is_object()) continue;
        const std::string actionId = action.value("actionId", "");
        const bool isAvailable = available.contains(actionId);
        const bool wasObserved = action.value("runtimeObserved", false);
        action["sourceDiscovered"] = action.value("sourceDiscovered", true);
        action["availableNow"] = isAvailable;
        action["runtimeObserved"] = wasObserved || isAvailable;
        action["verified"] = action.value("verified", false) || isAvailable;
        action["enabledForLocalAI"] = (wasObserved || isAvailable) && action.value("enabled", true);
        if (!action.contains("tags") || !action["tags"].is_array()) action["tags"] = GuessActionTags(actionId);
        if (isAvailable) matched.insert(actionId);
    }
    for (const auto& actionId : available) {
        if (matched.contains(actionId)) continue;
        profile["actions"].push_back({
            { "actionId", actionId }, { "category", GuessActionCategory(actionId) },
            { "tags", GuessActionTags(actionId) },
            { "enabled", true }, { "sourceDiscovered", false },
            { "runtimeObserved", true }, { "availableNow", true },
            { "verified", true }, { "enabledForLocalAI", true },
            { "sources", nlohmann::json::array() }, { "properties", nlohmann::json::object() },
        });
    }
    profile.erase("projectFolder");
    profile["lastRuntimeScene"] = observation.sceneId;
    profile["lastRuntimeFrame"] = observation.frameNumber;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << profile.dump(2) << '\n';
    return true;
}

bool ActionCanHitTarget(const DebugGenericAction& action) {
    if (GuessActionCategory(action.actionId) != "attack") return true;
    const auto found = action.parameters.find(DebugActionParameter::CanHitTarget);
    if (found == action.parameters.end()) return false;
    const auto* value = std::get_if<bool>(&found->second);
    return value && *value;
}

void EnrichObservationWithActionHitEstimates(DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    nlohmann::json profile;
    if (!projectRoot.empty()) {
        std::ifstream input(ActionProfilePath(projectRoot));
        profile = nlohmann::json::parse(input, nullptr, false);
    }
    std::map<std::string, nlohmann::json> propertiesByAction;
    if (!profile.is_discarded() && profile.is_object() &&
        profile.contains("actions") && profile["actions"].is_array()) {
        for (const auto& entry : profile["actions"]) {
            if (!entry.is_object() || !entry.contains("properties") || !entry["properties"].is_object()) continue;
            const std::string actionId = entry.value("actionId", "");
            if (!actionId.empty()) propertiesByAction[actionId] = entry["properties"];
        }
    }

    double targetDistance = -1.0;
    if (const auto found = observation.properties.find("enemy.distanceToPlayer");
        found != observation.properties.end()) {
        if (const auto* value = std::get_if<double>(&found->second)) targetDistance = *value;
        else if (const auto* value = std::get_if<std::int64_t>(&found->second)) targetDistance = static_cast<double>(*value);
    }
    DebugVec3 playerPosition{};
    DebugVec3 playerForward{};
    bool hasPlayerPosition = false;
    bool hasPlayerForward = false;
    if (const auto found = observation.properties.find("player.position"); found != observation.properties.end()) {
        if (const auto* value = std::get_if<DebugVec3>(&found->second)) {
            playerPosition = *value;
            hasPlayerPosition = true;
        }
    }
    if (const auto found = observation.properties.find("player.forward"); found != observation.properties.end()) {
        if (const auto* value = std::get_if<DebugVec3>(&found->second)) {
            playerForward = *value;
            hasPlayerForward = true;
        }
    }
    std::string nearestId;
    if (const auto found = observation.properties.find("enemy.nearestId"); found != observation.properties.end()) {
        if (const auto* value = std::get_if<std::string>(&found->second)) nearestId = *value;
    }
    const DebugEntity* target = nullptr;
    for (const auto& entity : observation.entities) {
        if ((!nearestId.empty() && entity.id == nearestId) ||
            (nearestId.empty() && entity.category == "Enemy")) {
            target = &entity;
            break;
        }
    }
    bool facingTarget = true;
    bool facingKnown = false;
    if (target && hasPlayerPosition && hasPlayerForward) {
        const double dx = target->position.x - playerPosition.x;
        const double dz = target->position.z - playerPosition.z;
        const double targetLength = std::sqrt(dx * dx + dz * dz);
        const double forwardLength = std::sqrt(playerForward.x * playerForward.x + playerForward.z * playerForward.z);
        if (targetLength > 0.0001 && forwardLength > 0.0001) {
            facingTarget = (dx * playerForward.x + dz * playerForward.z) /
                (targetLength * forwardLength) > 0.0;
            facingKnown = true;
        }
    }

    for (auto& action : observation.availableActions) {
        if (GuessActionCategory(action.actionId) != "attack") continue;
        const auto properties = propertiesByAction.find(action.actionId);
        const nlohmann::json* values = properties == propertiesByAction.end() ? nullptr : &properties->second;
        double estimatedRange = values ? values->value("estimatedRangeMax", 3.0) : 3.0;
        if (values) estimatedRange = (std::max)(estimatedRange, values->value("verifiedRangeMax", 0.0));
        estimatedRange = std::clamp(estimatedRange, 0.5, 100.0);
        const double confidence = values ? values->value("rangeConfidence", 0.25) : 0.25;
        const bool requiresFacing = values ? values->value("requiresFacing", true) : true;
        const bool inRange = targetDistance >= 0.0 && targetDistance <= estimatedRange;
        const bool canHit = inRange && (!requiresFacing || !facingKnown || facingTarget);
        action.parameters[DebugActionParameter::CanHitTarget] = canHit;
        action.parameters[DebugActionParameter::EstimatedRange] = estimatedRange;
        action.parameters[DebugActionParameter::TargetDistance] = targetDistance;
        action.parameters[DebugActionParameter::RangeConfidence] = confidence;
        action.parameters[DebugActionParameter::RequiresFacing] = requiresFacing;
        action.parameters["facingTarget"] = facingTarget;
        action.parameters["facingKnown"] = facingKnown;
    }
}

const DebugGenericAction* FindAvailableActionByProfileTag(
    const DebugObservation& observation, const std::string& requiredTag) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return nullptr;
    std::ifstream input(ActionProfilePath(projectRoot));
    const auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("actions") || !profile["actions"].is_array()) return nullptr;
    for (const auto& entry : profile["actions"]) {
        if (!entry.is_object() || !entry.contains("tags") || !entry["tags"].is_array()) continue;
        bool hasTag = false;
        for (const auto& tag : entry["tags"]) {
            if (tag.is_string() && tag.get<std::string>() == requiredTag) {
                hasTag = true;
                break;
            }
        }
        if (!hasTag) continue;
        const std::string actionId = entry.value("actionId", "");
        const auto found = std::find_if(observation.availableActions.begin(), observation.availableActions.end(),
            [&](const DebugGenericAction& action) { return action.actionId == actionId; });
        if (found != observation.availableActions.end()) return &*found;
    }
    return nullptr;
}

bool UpdateStateProfileFromRuntime(const DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return false;
    const auto path = StateProfilePath(projectRoot);
    std::ifstream input(path);
    auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("mappings") || !profile["mappings"].is_array()) return false;
    const auto hasProperty = [&](const std::string& name) {
        if (observation.properties.contains(name)) return true;
        return std::any_of(observation.entities.begin(), observation.entities.end(),
            [&](const DebugEntity& entity) { return entity.properties.contains(name); });
    };
    for (auto& mapping : profile["mappings"]) {
        if (!mapping.is_object()) continue;
        const std::string property = mapping.value("genericProperty", "");
        if (!property.empty() && hasProperty(property)) mapping["runtimeObserved"] = true;
    }
    profile["lastRuntimeScene"] = observation.sceneId;
    profile["lastRuntimeFrame"] = observation.frameNumber;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << profile.dump(2) << '\n';
    return true;
}

std::string DebugValueText(const DebugValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "";
        else if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::string>) return item;
        else if constexpr (std::is_same_v<T, DebugVec3>) {
            std::ostringstream text;
            text << std::fixed << std::setprecision(2) << "(" << item.x << ", " << item.y << ", " << item.z << ")";
            return text.str();
        } else return std::to_string(item);
    }, value);
}

std::string PropertyText(const DebugPropertyMap& properties, const char* name, const char* fallback = "-") {
    const auto found = properties.find(name);
    return found == properties.end() ? fallback : DebugValueText(found->second);
}

std::filesystem::path SaveLatestProtocolResponse(const std::string& response) {
    auto root = ConfiguredProjectRoot();
    if (root.empty()) root = std::filesystem::current_path();
    const auto path = root / "generated/debug_ai/viewer/latest_response.json";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    if (output) output << response << '\n';
    return path;
}

std::string FormatProtocolResponse(const std::string& response) {
    DebugProtocolMessage message;
    if (!DebugProtocolJson::TryParse(response, message)) return response;
    const auto rawPath = SaveLatestProtocolResponse(response);
    std::ostringstream output;
    output << "Game connection: connected\r\n"
        << "Result: " << PropertyText(message.properties, "ok", "true") << "\r\n"
        << "Message: " << PropertyText(message.properties, "message") << "\r\n";
    const std::string replayNote = PropertyText(message.properties, "lastError", "");
    if (!replayNote.empty()) output << "Replay note: " << replayNote << "\r\n";
    if (message.properties.contains("recording")) {
        output << "Recording: " << PropertyText(message.properties, "recording")
            << "  Replay: " << PropertyText(message.properties, "replaying") << "\r\n";
    }
    const std::string replayPath = PropertyText(message.properties, "path", "");
    if (!replayPath.empty()) output << "Replay file: " << replayPath << "\r\n";
    const std::string playerReplayPath = PropertyText(message.properties, "playerReplayPath", "");
    const std::string actorReplayPath = PropertyText(message.properties, "actorReplayPath", "");
    if (!playerReplayPath.empty()) output << "  Player input: " << playerReplayPath << "\r\n";
    if (!actorReplayPath.empty()) output << "  Actor actions: " << actorReplayPath << "\r\n";
    const std::string eventLogPath = PropertyText(message.properties, "eventLogPath", "");
    const std::string eventSummaryPath = PropertyText(message.properties, "eventSummaryPath", "");
    if (!eventLogPath.empty()) {
        output << "Event timeline (" << PropertyText(message.properties, "eventCount", "0")
            << "): " << eventLogPath << "\r\n";
    }
    if (!eventSummaryPath.empty()) output << "Event summary: " << eventSummaryPath << "\r\n";
    const std::string lastEvent = PropertyText(message.properties, "lastEvent", "");
    if (!lastEvent.empty()) output << "Latest event: " << lastEvent << "\r\n";
    if (message.observation) {
        const auto& observation = *message.observation;
        output << "\r\nGame State\r\n"
            << "  Scene: " << observation.sceneId
            << "  Phase: " << PropertyText(observation.properties, "game.phase")
            << "  Frame: " << observation.frameNumber
            << "  FPS: " << PropertyText(observation.properties, "fps") << "\r\n"
            << "  Player HP: " << PropertyText(observation.properties, "player.hp")
            << "  Position: " << PropertyText(observation.properties, "player.position") << "\r\n"
            << "  Player: action=" << PropertyText(observation.properties, "player.action")
            << "  attacking=" << PropertyText(observation.properties, "player.isAttacking")
            << "  canMove=" << PropertyText(observation.properties, "player.canMove")
            << "  canJump=" << PropertyText(observation.properties, "player.canJump")
            << "  canAttack=" << PropertyText(observation.properties, "player.canAttack") << "\r\n"
            << "  Enemy HP: " << PropertyText(observation.properties, "enemy.hp")
            << "  Count: " << PropertyText(observation.properties, "enemy.count")
            << "  Entities: " << observation.entities.size() << "\r\n"
            << "  Enemy: threat=" << PropertyText(observation.properties, "enemy.threat")
            << "  attackActive=" << PropertyText(observation.properties, "enemy.attackActive")
            << "  distance=" << PropertyText(observation.properties, "enemy.distanceToPlayer")
            << "  nearest=" << PropertyText(observation.properties, "enemy.nearestId") << "\r\n"
            << "\r\nAvailable Actions (" << observation.availableActions.size() << "):\r\n";
        const auto printActions = [&](bool player, const char* label) {
            output << "  " << label << ": ";
            bool first = true;
            for (const auto& action : observation.availableActions) {
                if (IsPlayerActorAction(action) != player) continue;
                if (!first) output << ", ";
                output << action.actionId;
                first = false;
            }
            if (first) output << "(none)";
            output << "\r\n";
        };
        printActions(true, "Player");
        printActions(false, "Boss/Other");
    }
    output << "\r\nDetailed JSON: " << WideToUtf8(rawPath.wstring());
    return output.str();
}

bool BrowseForProjectFolder(HWND owner, std::filesystem::path& selected) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)))) return false;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    const HRESULT shown = dialog->Show(owner);
    IShellItem* item = nullptr;
    PWSTR path = nullptr;
    const bool ok = SUCCEEDED(shown) && SUCCEEDED(dialog->GetResult(&item)) &&
        SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    if (ok) selected = path;
    if (path) CoTaskMemFree(path);
    if (item) item->Release();
    dialog->Release();
    return ok;
}

bool SendProtocolMessage(const DebugProtocolMessage& requestMessage, std::string& response) {
    std::lock_guard transportLock(gTransportMutex);
    response.clear();
    if (!WaitNamedPipeA(kPipeName, 250)) {
        response = "Game connection: disconnected\r\nStart CG2_Setup.exe to connect.";
        return false;
    }

    HANDLE pipe = CreateFileA(
        kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        response = "Game connection: disconnected";
        return false;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    const std::string request = DebugProtocolJson::Serialize(requestMessage);
    HANDLE writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED writeOverlapped{};
    writeOverlapped.hEvent = writeEvent;
    DWORD written = 0;
    bool writeOk = WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &written, &writeOverlapped) != FALSE;
    const bool writePending = !writeOk && GetLastError() == ERROR_IO_PENDING;
    if (writePending) {
        writeOk = WaitForSingleObject(writeEvent, kPipeIoTimeoutMilliseconds) == WAIT_OBJECT_0 &&
            GetOverlappedResult(pipe, &writeOverlapped, &written, FALSE) != FALSE;
    }
    if (!writeOk) {
        if (writePending) {
            CancelIoEx(pipe, &writeOverlapped);
            WaitForSingleObject(writeEvent, INFINITE);
        }
        CloseHandle(writeEvent);
        CloseHandle(pipe);
        response = "Game connection: send timeout or failure.";
        return false;
    }
    CloseHandle(writeEvent);

    std::vector<char> buffer(1024 * 1024);
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED readOverlapped{};
    readOverlapped.hEvent = readEvent;
    DWORD read = 0;
    bool success = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, &readOverlapped) != FALSE;
    const bool readPending = !success && GetLastError() == ERROR_IO_PENDING;
    DWORD readFailure = success ? ERROR_SUCCESS : GetLastError();
    if (readPending) {
        const DWORD waitResult = WaitForSingleObject(readEvent, kPipeIoTimeoutMilliseconds);
        success = waitResult == WAIT_OBJECT_0 &&
            GetOverlappedResult(pipe, &readOverlapped, &read, FALSE) != FALSE;
        readFailure = success ? ERROR_SUCCESS
            : (waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }
    if (!success && readPending) {
        CancelIoEx(pipe, &readOverlapped);
        WaitForSingleObject(readEvent, INFINITE);
    }
    CloseHandle(readEvent);
    CloseHandle(pipe);
    if (!success) {
        response = "Game connection: response failed (Windows error " +
            std::to_string(readFailure) + "). The game may be starting, stopped, or paused in the debugger.";
        return false;
    }
    response.assign(buffer.data(), read);
    return true;
}

DebugProtocolMessage MakeRequest(const char* command) {
    static std::atomic_uint64_t sequence = 0;
    DebugProtocolMessage request;
    request.gameId = "DebugAIViewer";
    request.gameVersion = "0.1.0";
    request.sessionId = "viewer";
    request.sequence = sequence.fetch_add(1) + 1;
    if (strcmp(command, "status") == 0) {
        request.messageType = DebugProtocolMessageType::StatusRequest;
    } else {
        request.messageType = DebugProtocolMessageType::ControlCommand;
        request.properties["command"] = std::string(command);
    }
    return request;
}

bool SendCommand(const char* command, std::string& response) {
    return SendProtocolMessage(MakeRequest(command), response);
}

bool SendCheckedCommand(const char* command, std::string& response) {
    if (!SendCommand(command, response)) return false;
    DebugProtocolMessage result;
    if (!DebugProtocolJson::TryParse(response, result)) return false;
    const auto found = result.properties.find("ok");
    return found != result.properties.end() &&
        std::get_if<bool>(&found->second) && *std::get_if<bool>(&found->second);
}

bool BuildOfflinePolicyObservation(DebugObservation& observation) {
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return false;
    std::ifstream input(ActionProfilePath(projectRoot));
    const auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("actions") || !profile["actions"].is_array()) return false;

    observation.sceneId = "OfflineProfile";
    observation.properties["game.phase"] = std::string("Battle");
    observation.properties["player.canMove"] = true;
    observation.properties["player.canJump"] = true;
    observation.properties["player.canAttack"] = true;
    observation.properties["player.isAttacking"] = false;
    observation.properties["enemy.threat"] = false;
    observation.properties["enemy.attackActive"] = false;
    observation.properties["enemy.distanceToPlayer"] = 6.0;
    observation.properties["observation.source"] = std::string("action_profile.json");
    for (const auto& entry : profile["actions"]) {
        if (!entry.is_object() || !entry.value("enabled", true)) continue;
        const bool usable = entry.value("enabledForLocalAI", false) ||
            entry.value("verified", false) || entry.value("runtimeObserved", false);
        const std::string actionId = entry.value("actionId", "");
        if (!usable || actionId.empty()) continue;
        DebugGenericAction action;
        action.actionId = actionId;
        action.parameters[DebugActionParameter::Direction] = DebugVec3{};
        action.parameters[DebugActionParameter::CoordinateSpace] = std::string(DebugCoordinateSpace::World);
        action.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(1);
        action.parameters[DebugActionParameter::TargetId] = std::string{};
        observation.availableActions.push_back(std::move(action));
    }
    return !observation.availableActions.empty();
}

std::string GenerateLocalPolicyCore() {
    std::string response;
    DebugObservation observation;
    bool usedRuntimeObservation = false;
    DebugProtocolMessage status;
    if (SendCommand("status", response) && DebugProtocolJson::TryParse(response, status) && status.observation) {
        observation = *status.observation;
        usedRuntimeObservation = true;
    } else if (!BuildOfflinePolicyObservation(observation)) {
        return "Local Policy generation failed: the game is disconnected and no verified Action Profile is available.\r\n"
            "Run Scan Project and Generate Action Profile once, or generate while GameScene is connected.";
    }
    EnrichObservationWithActionHitEstimates(observation);
    std::string policyJson;
    std::string reason;
    if (!gAIProvider.GenerateLocalPolicy(observation, policyJson, reason)) {
        return std::string("Local Policy generation failed (") + gAIProvider.Name() + "): " +
            gAIProvider.LastStatus();
    }
    auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return "Local Policy generation failed: select a Game Project Folder first.";
    const auto path = LocalPolicyPath(projectRoot);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    if (!output) return "Local Policy generation failed: output file could not be opened.";
    output << policyJson << '\n';
    return "Local Policy generated from AI Goal.\r\nPath: " + WideToUtf8(path.wstring()) +
        "\r\nObservation source: " +
            (usedRuntimeObservation ? std::string("connected game") : std::string("offline Action Profile")) +
        "\r\nProvider: " + gAIProvider.Name() + "\r\nReason: " + reason +
        "\r\nStart Local will use this policy without API calls.";
}

void ExecuteAndDisplay(const char* command) {
    std::string response;
    const bool connected = SendCommand(command, response);
    if (connected) {
        DebugProtocolMessage message;
        if (DebugProtocolJson::TryParse(response, message) && message.observation) {
            gLastObservation = *message.observation;
            gHasObservation = true;
        }
    }
    std::wstring display = connected
        ? L"Game connection: connected\r\n\r\n" + Utf8ToWide(response)
        : Utf8ToWide(response);
    SetWindowTextW(gStatusText, display.c_str());
}

std::string ExecuteCommandCore(const char* command) {
    std::string response;
    const bool connected = SendCommand(command, response);
    if (connected) {
        DebugProtocolMessage message;
        if (DebugProtocolJson::TryParse(response, message) && message.observation) {
            EnrichObservationWithActionHitEstimates(*message.observation);
            UpdateActionProfileFromRuntime(*message.observation);
            UpdateStateProfileFromRuntime(*message.observation);
        }
    }
    return connected ? FormatProtocolResponse(response) : response;
}

void ExecuteFirstAvailableAction() {
    std::string response;
    if (!SendCommand("status", response)) {
        ExecuteAndDisplay("status");
        return;
    }
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation ||
        status.observation->availableActions.empty()) {
        SetWindowTextW(gStatusText, L"No available generic action was received.");
        return;
    }
    DebugProtocolMessage request;
    request.gameId = "DebugAIViewer";
    request.gameVersion = "0.1.0";
    request.sessionId = "viewer";
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.sequence = status.sequence + 1;
    request.action = status.observation->availableActions.front();
    if (SendProtocolMessage(request, response)) {
        DebugProtocolMessage result;
        if (DebugProtocolJson::TryParse(response, result) && result.observation) {
            gLastObservation = *result.observation;
            gHasObservation = true;
        }
        SetWindowTextW(gStatusText, Utf8ToWide(response).c_str());
    }
}

std::string ExecuteFirstAvailableActionCore() {
    std::string response;
    if (!SendCommand("status", response)) return response;
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation ||
        status.observation->availableActions.empty()) {
        return "No available generic action was received.";
    }
    UpdateActionProfileFromRuntime(*status.observation);
    UpdateStateProfileFromRuntime(*status.observation);
    EnrichObservationWithActionHitEstimates(*status.observation);
    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = status.observation->availableActions.front();
    SendProtocolMessage(request, response);
    return FormatProtocolResponse(response);
}

std::string GenericActionActorId(const DebugGenericAction& action) {
    const auto found = action.parameters.find(DebugActionParameter::ActorId);
    if (found != action.parameters.end()) {
        if (const auto* value = std::get_if<std::string>(&found->second); value && !value->empty()) {
            return *value;
        }
    }
    return "player";
}

bool IsPlayerActorAction(const DebugGenericAction& action) {
    return GenericActionActorId(action) == "player";
}

DebugObservation ObservationForActor(const DebugObservation& source, ControlledActorMode mode) {
    DebugObservation result = source;
    if (mode == ControlledActorMode::Both) return result;
    const bool wantPlayer = mode == ControlledActorMode::Player;
    std::erase_if(result.availableActions, [&](const DebugGenericAction& action) {
        return IsPlayerActorAction(action) != wantPlayer;
    });
    return result;
}

const char* ControlledActorLabel(ControlledActorMode mode) {
    if (mode == ControlledActorMode::Boss) return "Boss";
    if (mode == ControlledActorMode::Both) return "Both";
    return "Player";
}

std::string ExecuteAIStepCore(
    HWND window,
    ControlledActorMode actorMode,
    bool* outExecuted = nullptr) {
    if (outExecuted) *outExecuted = false;
    const auto startedAt = std::chrono::steady_clock::now();
    PostAIStatus(window, "AI Step 1/5: requesting game state...", true);
    std::string response;
    if (!SendCommand("status", response)) {
        return response;
    }
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation) {
        return "AI Step: no generic observation was received.";
    }
    UpdateActionProfileFromRuntime(*status.observation);
    UpdateStateProfileFromRuntime(*status.observation);
    EnrichObservationWithActionHitEstimates(*status.observation);
    DebugObservation decisionObservation = ObservationForActor(*status.observation, actorMode);
    if (decisionObservation.availableActions.empty()) {
        return std::string("AI Step: no actions are available for actor ") +
            ControlledActorLabel(actorMode) + ".";
    }

    const bool pauseForInitialConnection = !gAIConnectionVerified.load();
    if (pauseForInitialConnection) {
        std::string pauseResponse;
        if (!SendCheckedCommand("pause_simulation", pauseResponse)) {
            return "AI Step failed: the game could not be paused.\r\n" + pauseResponse;
        }
    }
    PostAIStatus(window, std::string("AI Step 2/5: ") +
        (pauseForInitialConnection ? "game paused for initial connection; calling " : "calling ") +
        gAIProvider.Name() +
        " for " + ControlledActorLabel(actorMode) + " with " +
        std::to_string(decisionObservation.availableActions.size()) + " available actions...", true);
    DebugGenericAction selected;
    std::string reason;
    if (!gAIProvider.ChooseAction(decisionObservation, selected, reason)) {
        if (pauseForInitialConnection) {
            std::string ignoredResumeResponse;
            SendCheckedCommand("resume_simulation", ignoredResumeResponse);
        }
        const std::string message = std::string("AI Step failed (") + gAIProvider.Name() + "): " +
            gAIProvider.LastStatus();
        return message;
    }
    if (GuessActionCategory(selected.actionId) == "attack" && !ActionCanHitTarget(selected)) {
        if (const auto* approach = FindAvailableActionByProfileTag(
            decisionObservation, "movement.approach")) {
            selected = *approach;
            selected.parameters[DebugActionParameter::Direction] = DebugVec3{ 0.0, 0.0, 1.0 };
            selected.parameters[DebugActionParameter::CoordinateSpace] =
                std::string(DebugCoordinateSpace::TargetRelative);
            if (const auto target = status.observation->properties.find("enemy.nearestId");
                target != status.observation->properties.end()) {
                if (const auto* targetId = std::get_if<std::string>(&target->second))
                    selected.parameters[DebugActionParameter::TargetId] = *targetId;
            }
            selected.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(12);
            reason = "API attack rejected because canHitTarget=false; approaching target instead";
        }
    }
    selected.parameters[DebugActionParameter::Source] = std::string("API");

    if (pauseForInitialConnection) {
        PostAIStatus(window, "AI Step 3/5: selected " + selected.actionId + "; resuming game...", true);
        std::string resumeResponse;
        if (!SendCheckedCommand("resume_simulation", resumeResponse)) {
            return "AI Step failed: the game could not be resumed. It will auto-resume within 20 seconds.\r\n" +
                resumeResponse;
        }
    } else {
        PostAIStatus(window, "AI Step 3/5: selected " + selected.actionId + "; game remained running.", true);
    }
    PostAIStatus(window, "AI Step 4/5: sending " + selected.actionId + " to the game...", true);
    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = selected;
    if (!SendProtocolMessage(request, response)) {
        return response;
    }
    if (outExecuted) *outExecuted = true;
    gAIConnectionVerified = true;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    const auto parameter = [&](const char* name) {
        const auto found = selected.parameters.find(name);
        return found == selected.parameters.end() ? std::string("-") : DebugValueText(found->second);
    };
    return std::string("AI Step 5/5: completed\r\nProvider: ") + gAIProvider.Name() +
        "\r\nActor: " + GenericActionActorId(selected) +
        "\r\nAction: " + selected.actionId +
        "\r\nParameters: direction=" + parameter(DebugActionParameter::Direction) +
        ", space=" + parameter(DebugActionParameter::CoordinateSpace) +
        ", durationFrames=" + parameter(DebugActionParameter::DurationFrames) +
        "\r\nReason: " + reason + "\r\n\r\n" +
        FormatProtocolResponse(response) +
        "\r\nElapsed: " + std::to_string(elapsed) + " ms";
}

struct LocalPolicyConfig {
    struct Condition {
        std::string property;
        std::string operation;
        nlohmann::json value;
        double enter = 0.0;
        double exit = 0.0;
    };
    struct Rule {
        std::string id;
        int priority = 0;
        std::string conditionMode = "all";
        std::vector<Condition> conditions;
        std::vector<std::string> actionIds;
        std::vector<std::string> actionTags;
        std::string selection = "first";
        std::int64_t durationFrames = 1;
        bool interruptCurrent = false;
        bool repeatWhileMatched = false;
        int maxConsecutive = 0;
        std::vector<std::string> recoveryActionTags;
        std::int64_t recoveryDurationFrames = 1;
        bool hasDirection = false;
        DebugVec3 direction{};
        std::string coordinateSpace = DebugCoordinateSpace::World;
    };
    std::vector<Rule> rules;
    std::map<std::string, std::set<std::string>> actionTags;
    std::vector<std::string> threatActions{ "DodgeAway", "Jump", "Retreat", "Guard", "Wait" };
    std::string approachAction = "Move";
    std::vector<std::string> attackActions;
    std::string idleAction = "Wait";
    double attackDistance = 5.0;
    std::int64_t approachDurationFrames = 16;
    std::int64_t evadeDurationFrames = 10;
    std::int64_t attackDurationFrames = 8;
    bool preferLeastUsedAttack = true;
    bool loadedFromFile = false;
};

LocalPolicyConfig LoadLocalPolicyConfig() {
    LocalPolicyConfig config;
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return config;
    std::ifstream input(LocalPolicyPath(projectRoot));
    const auto policy = nlohmann::json::parse(input, nullptr, false);
    if (policy.is_discarded() || !policy.is_object()) return config;
    std::ifstream actionInput(ActionProfilePath(projectRoot));
    const auto actionProfile = nlohmann::json::parse(actionInput, nullptr, false);
    if (!actionProfile.is_discarded() && actionProfile.is_object() &&
        actionProfile.contains("actions") && actionProfile["actions"].is_array()) {
        for (const auto& action : actionProfile["actions"]) {
            if (!action.is_object()) continue;
            const std::string id = action.value("actionId", "");
            if (id.empty() || !action.contains("tags") || !action["tags"].is_array()) continue;
            for (const auto& tag : action["tags"]) if (tag.is_string()) config.actionTags[id].insert(tag.get<std::string>());
        }
    }
    const auto readActions = [&](const char* name, std::vector<std::string>& output) {
        const auto found = policy.find(name);
        if (found == policy.end() || !found->is_array()) return;
        std::vector<std::string> values;
        for (const auto& value : *found) if (value.is_string()) values.push_back(value.get<std::string>());
        if (!values.empty()) output = std::move(values);
    };
    readActions("threatActions", config.threatActions);
    readActions("attackActions", config.attackActions);
    config.approachAction = policy.value("approachAction", config.approachAction);
    config.idleAction = policy.value("idleAction", config.idleAction);
    config.attackDistance = std::clamp(policy.value("attackDistance", config.attackDistance), 0.0, 100.0);
    config.approachDurationFrames = std::clamp<std::int64_t>(
        policy.value("approachDurationFrames", config.approachDurationFrames), 1, 60);
    config.evadeDurationFrames = std::clamp<std::int64_t>(
        policy.value("evadeDurationFrames", config.evadeDurationFrames), 1, 60);
    config.attackDurationFrames = std::clamp<std::int64_t>(
        policy.value("attackDurationFrames", config.attackDurationFrames), 1, 60);
    config.preferLeastUsedAttack = policy.value("preferLeastUsedAttack", true);
    if (const auto rules = policy.find("rules"); rules != policy.end() && rules->is_array()) {
        for (const auto& sourceRule : *rules) {
            if (!sourceRule.is_object()) continue;
            LocalPolicyConfig::Rule rule;
            rule.id = sourceRule.value("id", "rule_" + std::to_string(config.rules.size()));
            rule.priority = sourceRule.value("priority", 0);
            rule.conditionMode = sourceRule.value("conditionMode", "all");
            rule.selection = sourceRule.value("selection", "first");
            rule.durationFrames = std::clamp<std::int64_t>(sourceRule.value("durationFrames", 1), 1, 600);
            rule.interruptCurrent = sourceRule.value("interruptCurrent", false);
            rule.repeatWhileMatched = sourceRule.value("repeatWhileMatched", false);
            rule.maxConsecutive = std::clamp(sourceRule.value("maxConsecutive", 0), 0, 100);
            rule.recoveryDurationFrames = std::clamp<std::int64_t>(
                sourceRule.value("recoveryDurationFrames", 1), 1, 600);
            const auto readStrings = [&](const char* name, std::vector<std::string>& destination) {
                const auto values = sourceRule.find(name);
                if (values == sourceRule.end() || !values->is_array()) return;
                for (const auto& value : *values) if (value.is_string()) destination.push_back(value.get<std::string>());
            };
            readStrings("actionIds", rule.actionIds);
            readStrings("actionTags", rule.actionTags);
            readStrings("recoveryActionTags", rule.recoveryActionTags);
            // Profiles generated before repeatWhileMatched existed already tag
            // sustained movement as movement.approach. Preserve smooth movement
            // without requiring users to regenerate those profiles.
            if (!sourceRule.contains("repeatWhileMatched")) {
                rule.repeatWhileMatched = std::find(rule.actionTags.begin(), rule.actionTags.end(),
                    "movement.approach") != rule.actionTags.end();
                for (const auto& actionId : rule.actionIds) {
                    const auto tags = config.actionTags.find(actionId);
                    if (tags != config.actionTags.end() && tags->second.contains("movement.approach")) {
                        rule.repeatWhileMatched = true;
                        break;
                    }
                }
            }
            // Backward compatibility: older generated attack rules had no
            // cadence, so they could monopolize every available decision.
            if (!sourceRule.contains("maxConsecutive")) {
                bool isAttackRule = std::find(rule.actionTags.begin(), rule.actionTags.end(),
                    "combat.attack") != rule.actionTags.end();
                for (const auto& actionId : rule.actionIds) {
                    const auto tags = config.actionTags.find(actionId);
                    if (tags != config.actionTags.end() && tags->second.contains("combat.attack")) {
                        isAttackRule = true;
                        break;
                    }
                }
                if (isAttackRule) {
                    rule.maxConsecutive = 2;
                    rule.recoveryActionTags = { "movement.retreat" };
                    rule.recoveryDurationFrames = (std::max<std::int64_t>)(12, rule.durationFrames);
                }
            }
            if (const auto conditions = sourceRule.find("conditions");
                conditions != sourceRule.end() && conditions->is_array()) {
                for (const auto& sourceCondition : *conditions) {
                    if (!sourceCondition.is_object()) continue;
                    LocalPolicyConfig::Condition condition;
                    condition.property = sourceCondition.value("property", "");
                    condition.operation = sourceCondition.value("operator", "equals");
                    if (sourceCondition.contains("value")) condition.value = sourceCondition["value"];
                    condition.enter = sourceCondition.value("enter", 0.0);
                    condition.exit = sourceCondition.value("exit", condition.enter);
                    if (!condition.property.empty()) rule.conditions.push_back(std::move(condition));
                }
            }
            if (const auto direction = sourceRule.find("direction");
                direction != sourceRule.end() && direction->is_object()) {
                rule.hasDirection = true;
                rule.direction = { direction->value("x", 0.0), direction->value("y", 0.0), direction->value("z", 0.0) };
            }
            rule.coordinateSpace = sourceRule.value("coordinateSpace", DebugCoordinateSpace::World);
            if (!rule.conditions.empty() && (!rule.actionIds.empty() || !rule.actionTags.empty()))
                config.rules.push_back(std::move(rule));
        }
        std::stable_sort(config.rules.begin(), config.rules.end(),
            [](const auto& left, const auto& right) { return left.priority > right.priority; });
    }
    config.loadedFromFile = true;
    return config;
}

struct LocalPolicyState {
    LocalPolicyConfig config;
    std::map<std::string, std::uint64_t> useCount;
    std::size_t fallbackIndex = 0;
    std::map<std::string, bool> ruleActive;
    std::map<std::string, int> consecutiveRuleExecutions;
    std::uint64_t actionLockUntilFrame = 0;
    int activePriority = -1;
    std::string activeRuleId;
    std::string activeActionId;
    struct PendingAttackSample {
        bool active = false;
        bool sawAttackActive = false;
        std::string actionId;
        double distance = -1.0;
        double enemyHpBefore = -1.0;
        std::uint64_t deadlineFrame = 0;
    } pendingAttack;
};

bool BoolProperty(const DebugPropertyMap& properties, const char* name, bool fallback) {
    const auto found = properties.find(name);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<bool>(&found->second)) return *value;
    return fallback;
}

double NumberProperty(const DebugPropertyMap& properties, const char* name, double fallback) {
    const auto found = properties.find(name);
    if (found == properties.end()) return fallback;
    if (const auto* value = std::get_if<double>(&found->second)) return *value;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) return static_cast<double>(*value);
    return fallback;
}

std::string StringProperty(const DebugPropertyMap& properties, const char* name) {
    const auto found = properties.find(name);
    if (found == properties.end()) return {};
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return {};
}

void RecordActionRangeSample(const std::string& actionId, double distance, bool hit) {
    if (actionId.empty() || distance < 0.0) return;
    const auto projectRoot = ConfiguredProjectRoot();
    if (projectRoot.empty()) return;
    const auto path = ActionProfilePath(projectRoot);
    std::ifstream input(path);
    auto profile = nlohmann::json::parse(input, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("actions") || !profile["actions"].is_array()) return;
    for (auto& action : profile["actions"]) {
        if (!action.is_object() || action.value("actionId", "") != actionId) continue;
        if (!action.contains("properties") || !action["properties"].is_object())
            action["properties"] = nlohmann::json::object();
        auto& properties = action["properties"];
        properties["runtimeSamples"] = properties.value("runtimeSamples", 0) + 1;
        if (hit) {
            properties["runtimeHitSamples"] = properties.value("runtimeHitSamples", 0) + 1;
            properties["verifiedRangeMax"] = (std::max)(
                properties.value("verifiedRangeMax", 0.0), distance);
            properties["rangeConfidence"] = (std::max)(
                properties.value("rangeConfidence", 0.25), 0.85);
            properties["runtimeRangeVerified"] = true;
        } else {
            properties["runtimeMissSamples"] = properties.value("runtimeMissSamples", 0) + 1;
            const double previous = properties.value("nearestMissRange", -1.0);
            properties["nearestMissRange"] = previous < 0.0 ? distance : (std::min)(previous, distance);
        }
        break;
    }
    std::ofstream output(path, std::ios::trunc);
    if (output) output << profile.dump(2) << '\n';
}

bool PolicyConditionMatches(const LocalPolicyConfig::Condition& condition,
    const DebugPropertyMap& properties, bool ruleWasActive) {
    const auto found = properties.find(condition.property);
    if (found == properties.end()) return false;
    if (condition.operation == "equals" || condition.operation == "notEquals") {
        bool equal = false;
        if (condition.value.is_boolean()) {
            if (const auto* value = std::get_if<bool>(&found->second)) equal = *value == condition.value.get<bool>();
        } else if (condition.value.is_string()) {
            if (const auto* value = std::get_if<std::string>(&found->second)) equal = *value == condition.value.get<std::string>();
        } else if (condition.value.is_number()) {
            equal = std::abs(NumberProperty(properties, condition.property.c_str(),
                std::numeric_limits<double>::quiet_NaN()) - condition.value.get<double>()) < 0.0001;
        }
        return condition.operation == "equals" ? equal : !equal;
    }
    const double number = NumberProperty(properties, condition.property.c_str(),
        std::numeric_limits<double>::quiet_NaN());
    if (std::isnan(number)) return false;
    const double expected = condition.value.is_number() ? condition.value.get<double>() : 0.0;
    if (condition.operation == "greaterThan") return number > expected;
    if (condition.operation == "greaterOrEqual") return number >= expected;
    if (condition.operation == "lessThan") return number < expected;
    if (condition.operation == "lessOrEqual") return number <= expected;
    if (condition.operation == "hysteresisAbove")
        return ruleWasActive ? number > condition.exit : number > condition.enter;
    return false;
}

std::string ExecuteLocalContinuousStep(LocalPolicyState& policy, unsigned int intervalMilliseconds) {
    std::string response;
    if (!SendCommand("status", response)) return response;
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation ||
        status.observation->availableActions.empty()) {
        return "Local continuous step: no available action.";
    }
    UpdateActionProfileFromRuntime(*status.observation);
    UpdateStateProfileFromRuntime(*status.observation);
    EnrichObservationWithActionHitEstimates(*status.observation);
    DebugObservation observation = ObservationForActor(
        *status.observation, ControlledActorMode::Player);
    if (observation.availableActions.empty()) {
        return "Local continuous step: no player action is available.";
    }
    const auto& actions = observation.availableActions;
    const auto findAction = [&](const std::string& id) -> const DebugGenericAction* {
        const auto found = std::find_if(actions.begin(), actions.end(),
            [&](const DebugGenericAction& action) { return action.actionId == id; });
        return found == actions.end() ? nullptr : &*found;
    };
    const auto firstAvailable = [&](std::initializer_list<const char*> ids) -> const DebugGenericAction* {
        for (const char* id : ids) if (const auto* action = findAction(id)) return action;
        return nullptr;
    };

    const bool threat = BoolProperty(observation.properties, "enemy.threat", false);
    const bool attackActive = BoolProperty(observation.properties, "enemy.attackActive", false);
    const bool canMove = BoolProperty(observation.properties, "player.canMove", true);
    const bool canJump = BoolProperty(observation.properties, "player.canJump", true);
    const bool canAttack = BoolProperty(observation.properties, "player.canAttack", true);
    const bool isAttacking = BoolProperty(observation.properties, "player.isAttacking", false);
    const double distance = NumberProperty(observation.properties, "enemy.distanceToPlayer", -1.0);
    const double enemyHp = NumberProperty(observation.properties, "enemy.hp", -1.0);
    const std::string nearestId = StringProperty(observation.properties, "enemy.nearestId");
    const auto& config = policy.config;

    if (policy.pendingAttack.active) {
        if (isAttacking) policy.pendingAttack.sawAttackActive = true;
        const bool hit = enemyHp >= 0.0 && policy.pendingAttack.enemyHpBefore >= 0.0 &&
            enemyHp < policy.pendingAttack.enemyHpBefore;
        const bool finishedWithoutHit = !isAttacking && policy.pendingAttack.sawAttackActive;
        const bool timedOut = observation.frameNumber >= policy.pendingAttack.deadlineFrame;
        if (hit || finishedWithoutHit || timedOut) {
            RecordActionRangeSample(policy.pendingAttack.actionId,
                policy.pendingAttack.distance, hit);
            policy.pendingAttack = {};
        }
    }

    const DebugGenericAction* choice = nullptr;
    const LocalPolicyConfig::Rule* selectedRule = nullptr;
    bool selectedRecovery = false;
    std::string reason;
    for (const auto& rule : config.rules) {
        const bool wasActive = policy.ruleActive[rule.id];
        std::size_t matchedConditions = 0;
        for (const auto& condition : rule.conditions) {
            if (PolicyConditionMatches(condition, observation.properties, wasActive)) ++matchedConditions;
        }
        const bool matched = rule.conditionMode == "any"
            ? matchedConditions > 0
            : matchedConditions == rule.conditions.size();
        policy.ruleActive[rule.id] = matched;
        if (!matched || selectedRule) continue;

        std::vector<const DebugGenericAction*> candidates;
        for (const auto& actionId : rule.actionIds) {
            if (const auto* action = findAction(actionId)) {
                if (GuessActionCategory(action->actionId) != "attack" || ActionCanHitTarget(*action))
                    candidates.push_back(action);
            }
        }
        if (candidates.empty() && !rule.actionTags.empty()) {
            for (const auto& action : actions) {
                const auto tags = config.actionTags.find(action.actionId);
                if (tags == config.actionTags.end()) continue;
                const bool hasTag = std::any_of(rule.actionTags.begin(), rule.actionTags.end(),
                    [&](const std::string& tag) { return tags->second.contains(tag); });
                if (hasTag && (GuessActionCategory(action.actionId) != "attack" || ActionCanHitTarget(action)))
                    candidates.push_back(&action);
            }
        }
        if (candidates.empty()) continue;

        if (rule.maxConsecutive > 0 &&
            policy.consecutiveRuleExecutions[rule.id] >= rule.maxConsecutive && canMove) {
            std::vector<const DebugGenericAction*> recoveryCandidates;
            for (const auto& action : actions) {
                const auto tags = config.actionTags.find(action.actionId);
                if (tags == config.actionTags.end()) continue;
                const bool hasRecoveryTag = std::any_of(rule.recoveryActionTags.begin(),
                    rule.recoveryActionTags.end(),
                    [&](const std::string& tag) { return tags->second.contains(tag); });
                if (hasRecoveryTag) recoveryCandidates.push_back(&action);
            }
            if (!recoveryCandidates.empty()) {
                choice = recoveryCandidates.front();
                selectedRule = &rule;
                selectedRecovery = true;
                reason = "maximum consecutive actions reached; adjusting spacing";
                break;
            }
        }
        choice = candidates.front();
        if (rule.selection == "leastUsed") {
            for (const auto* candidate : candidates) {
                if (policy.useCount[candidate->actionId] < policy.useCount[choice->actionId]) choice = candidate;
            }
        }
        selectedRule = &rule;
        reason = "generic rule " + rule.id + " matched";
    }

    if (!choice && !canMove && !canJump && !canAttack) {
        choice = findAction(config.idleAction);
        if (!choice) choice = firstAvailable({ "Wait", "Guard" });
        reason = "player cannot currently act";
    } else if (!choice && (threat || attackActive)) {
        for (const auto& actionId : config.threatActions) {
            if (!canJump && (actionId == "DodgeAway" || actionId == "Jump")) continue;
            if (!canMove && (actionId == "Retreat" || actionId == "Move")) continue;
            if (const auto* action = findAction(actionId)) { choice = action; break; }
        }
        if (!choice) choice = firstAvailable({ "Guard", "Wait" });
        reason = attackActive ? "enemy attack is active" : "incoming enemy threat";
    } else if (!choice && canAttack && !isAttacking && (distance < 0.0 || distance <= config.attackDistance)) {
        for (const auto& action : actions) {
            const bool listed = config.attackActions.empty()
                ? GuessActionCategory(action.actionId) == "attack"
                : std::find(config.attackActions.begin(), config.attackActions.end(), action.actionId) != config.attackActions.end();
            if (!listed || !ActionCanHitTarget(action)) continue;
            if (!choice || !config.preferLeastUsedAttack ||
                policy.useCount[action.actionId] < policy.useCount[choice->actionId]) choice = &action;
            if (choice && !config.preferLeastUsedAttack) break;
        }
        reason = "safe attack range; selecting least-used attack";
    } else if (!choice && canMove && distance > config.attackDistance) {
        choice = findAction(config.approachAction);
        if (!choice) choice = firstAvailable({ "Move" });
        reason = "enemy is outside attack range; approaching target";
    }
    if (!choice && canMove) {
        choice = findAction(config.approachAction);
        if (!choice) choice = FindAvailableActionByProfileTag(observation, "movement.approach");
        if (choice) reason = "no attack can currently hit; adjusting range or facing";
    }
    if (!choice && canAttack && !isAttacking) {
        for (const auto& action : actions) {
            if (GuessActionCategory(action.actionId) == "attack" && ActionCanHitTarget(action)) {
                choice = &action;
                break;
            }
        }
        reason = "using an available attack";
    }
    if (!choice) {
        choice = firstAvailable({ "Wait", "Guard" });
        if (!choice) choice = &actions[policy.fallbackIndex++ % actions.size()];
        reason = "safe fallback";
    }

    const bool repeatWhileMatched = selectedRule
        ? selectedRule->repeatWhileMatched
        : choice->actionId == config.approachAction;
    if (observation.frameNumber < policy.actionLockUntilFrame) {
        const bool mayInterrupt = selectedRule && selectedRule->interruptCurrent &&
            selectedRule->priority > policy.activePriority;
        const bool mayRefresh = repeatWhileMatched &&
            choice->actionId == policy.activeActionId &&
            (!selectedRule || selectedRule->id == policy.activeRuleId);
        if (!mayInterrupt && !mayRefresh) {
            return "Generic Local Policy\r\nAction lock: " + policy.activeRuleId +
                " continues until frame " + std::to_string(policy.actionLockUntilFrame) +
                "\r\nCurrent frame: " + std::to_string(observation.frameNumber) +
                "\r\nAPI call: skipped\r\n\r\n" + FormatProtocolResponse(response);
        }
    }

    DebugGenericAction selected = *choice;
    selected.parameters[DebugActionParameter::ActorId] = std::string("player");
    selected.parameters[DebugActionParameter::Source] = std::string("Local");
    ++policy.useCount[selected.actionId];
    std::int64_t selectedDuration = 1;
    if (selectedRule) {
        selectedDuration = selectedRecovery
            ? selectedRule->recoveryDurationFrames
            : selectedRule->durationFrames;
        selected.parameters[DebugActionParameter::DurationFrames] = selectedDuration;
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        if (selectedRule->hasDirection) {
            selected.parameters[DebugActionParameter::Direction] = selectedRule->direction;
            selected.parameters[DebugActionParameter::CoordinateSpace] = selectedRule->coordinateSpace;
        }
    } else if (selected.actionId == config.approachAction ||
        (config.actionTags.contains(selected.actionId) &&
         config.actionTags.at(selected.actionId).contains("movement.approach"))) {
        selected.parameters[DebugActionParameter::Direction] = DebugVec3{ 0.0, 0.0, 1.0 };
        selected.parameters[DebugActionParameter::CoordinateSpace] =
            std::string(DebugCoordinateSpace::TargetRelative);
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        selected.parameters[DebugActionParameter::DurationFrames] = config.approachDurationFrames;
        selectedDuration = config.approachDurationFrames;
    } else if (std::find(config.threatActions.begin(), config.threatActions.end(), selected.actionId) !=
        config.threatActions.end()) {
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        selected.parameters[DebugActionParameter::DurationFrames] = config.evadeDurationFrames;
        selectedDuration = config.evadeDurationFrames;
    } else if (GuessActionCategory(selected.actionId) == "attack") {
        selected.parameters[DebugActionParameter::TargetId] = nearestId;
        selected.parameters[DebugActionParameter::DurationFrames] = config.attackDurationFrames;
        selectedDuration = config.attackDurationFrames;
    }
    if (repeatWhileMatched) {
        // Keep a continuous control active until the next decision can renew it.
        // The margin also covers pipe/status latency and variable frame times.
        const double observedFps = std::clamp(NumberProperty(observation.properties, "fps", 60.0), 10.0, 360.0);
        const auto decisionFrames = static_cast<std::int64_t>(
            std::ceil(observedFps * static_cast<double>(intervalMilliseconds) / 1000.0)) + 4;
        selectedDuration = std::clamp<std::int64_t>((std::max)(selectedDuration, decisionFrames), 1, 600);
        selected.parameters[DebugActionParameter::DurationFrames] = selectedDuration;
    }
    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = selected;
    if (!SendProtocolMessage(request, response)) return response;
    if (GuessActionCategory(selected.actionId) == "attack" && !policy.pendingAttack.active) {
        policy.pendingAttack.active = true;
        policy.pendingAttack.actionId = selected.actionId;
        policy.pendingAttack.distance = distance;
        policy.pendingAttack.enemyHpBefore = enemyHp;
        policy.pendingAttack.deadlineFrame = observation.frameNumber + 120;
    }
    if (selectedRule && selectedRule->maxConsecutive > 0) {
        if (selectedRecovery) {
            policy.consecutiveRuleExecutions[selectedRule->id] = 0;
        } else {
            ++policy.consecutiveRuleExecutions[selectedRule->id];
        }
    }
    policy.actionLockUntilFrame = observation.frameNumber + static_cast<std::uint64_t>(selectedDuration);
    policy.activePriority = selectedRule ? selectedRule->priority : 0;
    policy.activeRuleId = selectedRule ? selectedRule->id : "compatibility_fallback";
    policy.activeActionId = selected.actionId;
    return "Generic Local Policy\r\nAction: " + selected.actionId +
        "\r\nReason: " + reason +
        "\r\nThreat: " + (threat ? "true" : "false") +
        "  Attack active: " + (attackActive ? "true" : "false") +
        "  Distance: " + std::to_string(distance) +
        "\r\nPolicy source: " + (config.loadedFromFile ? "AI-generated local_policy.json" : "built-in default") +
        "\r\nAPI call: skipped\r\n\r\n" + FormatProtocolResponse(response);
}

struct BossLocalPolicyState {
    std::map<std::string, std::size_t> useCount;
    std::uint64_t nextDecisionFrame = 0;
    std::string activeAction;
};

std::string ExecuteBossLocalContinuousStep(BossLocalPolicyState& policy) {
    std::string response;
    if (!SendCommand("status", response)) return response;
    DebugProtocolMessage status;
    if (!DebugProtocolJson::TryParse(response, status) || !status.observation) {
        return "Boss Local Policy: no game observation was received.";
    }
    DebugObservation observation = ObservationForActor(
        *status.observation, ControlledActorMode::Boss);
    if (observation.availableActions.empty()) {
        return "Boss Local Policy: no boss action is available in the current scene or phase.";
    }
    if (observation.frameNumber < policy.nextDecisionFrame) {
        return "Boss Local Policy\r\nAction: " + policy.activeAction +
            " (native sequence continues)\r\nNext decision frame: " +
            std::to_string(policy.nextDecisionFrame) + "\r\nAPI call: skipped";
    }

    const DebugGenericAction* choice = &observation.availableActions.front();
    for (const auto& candidate : observation.availableActions) {
        if (policy.useCount[candidate.actionId] < policy.useCount[choice->actionId]) {
            choice = &candidate;
        }
    }
    DebugGenericAction selected = *choice;
    selected.parameters[DebugActionParameter::Source] = std::string("Local");
    selected.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(1);

    DebugProtocolMessage request = MakeRequest("execute_action");
    request.messageType = DebugProtocolMessageType::ExecuteAction;
    request.action = selected;
    if (!SendProtocolMessage(request, response)) return response;

    ++policy.useCount[selected.actionId];
    policy.activeAction = selected.actionId;
    // Let the boss's native state sequence play out before selecting a new
    // high-level move. This is based on observation frames, not engine time.
    policy.nextDecisionFrame = observation.frameNumber + 90;
    return "Boss Local Policy\r\nActor: " + GenericActionActorId(selected) +
        "\r\nAction: " + selected.actionId +
        "\r\nReason: least-used boss action; native sequence runs for 90 frames" +
        "\r\nAPI call: skipped\r\n\r\n" + FormatProtocolResponse(response);
}

std::string ExecuteLocalActors(
    LocalPolicyState& playerPolicy,
    BossLocalPolicyState& bossPolicy,
    ControlledActorMode mode,
    unsigned int intervalMilliseconds) {
    if (mode == ControlledActorMode::Boss) return ExecuteBossLocalContinuousStep(bossPolicy);
    if (mode == ControlledActorMode::Both) {
        return ExecuteLocalContinuousStep(playerPolicy, intervalMilliseconds) +
            "\r\n\r\n---------------- Actor: Boss ----------------\r\n" +
            ExecuteBossLocalContinuousStep(bossPolicy);
    }
    return ExecuteLocalContinuousStep(playerPolicy, intervalMilliseconds);
}

void PostAIStatus(HWND window, std::string text, bool preserveResult = false) {
    {
        std::lock_guard lock(gAIStatusMutex);
        gPendingAIStatus = std::move(text);
    }
    PostMessageW(window, kAIStatusMessage, preserveResult ? 1 : 0, 0);
}

unsigned int ReadAIIntervalMilliseconds() {
    wchar_t text[32]{};
    GetWindowTextW(gAIInterval, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(text, &end, 10);
    return end == text ? 250u : static_cast<unsigned int>(std::clamp(value, 60ul, 5000ul));
}

ControlledActorMode ReadControlledActorMode() {
    if (!gAIActorMode) return ControlledActorMode::Player;
    const LRESULT selection = SendMessageW(gAIActorMode, CB_GETCURSEL, 0, 0);
    if (selection == 1) return ControlledActorMode::Boss;
    if (selection == 2) return ControlledActorMode::Both;
    return ControlledActorMode::Player;
}

void ApplyViewerGoal() {
    const int length = GetWindowTextLengthW(gAIGoal);
    if (length <= 0) return;
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(gAIGoal, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0u);
    gAIProvider.SetGoal(WideToUtf8(text));
}

enum class AIWorkerMode { ApiStep, ApiContinuous, LocalContinuous };

void StartAIWorker(HWND window, AIWorkerMode mode) {
    if (gAIWorkerRunning.exchange(true)) {
        SetWindowTextW(gStatusText, L"AI is already running.");
        return;
    }
    // Ignore a status request that may already be in flight. Its stale
    // disconnected/connected text must not overwrite AI progress.
    gAutoRefreshResumeTick = GetTickCount64() + 60000;
    if (gAIWorker.joinable()) gAIWorker.join();
    const bool continuous = mode != AIWorkerMode::ApiStep;
    const bool useApi = mode != AIWorkerMode::LocalContinuous;
    if (useApi) ApplyViewerGoal();
    gAIStopRequested = false;
    const unsigned int requestedInterval = ReadAIIntervalMilliseconds();
    const unsigned int interval = mode == AIWorkerMode::LocalContinuous
        ? (requestedInterval > 250 ? 250 : requestedInterval)
        : requestedInterval;
    const ControlledActorMode actorMode = ReadControlledActorMode();
    SetWindowTextW(gStatusText, mode == AIWorkerMode::ApiStep ? L"API Step started..."
        : (mode == AIWorkerMode::ApiContinuous ? L"API Continuous started..." : L"Local AI started (basic policy)..."));
    gAIWorker = std::thread([window, mode, continuous, useApi, interval, actorMode] {
        constexpr auto kRateLimitCooldown = std::chrono::seconds(60);
        LocalPolicyState localPolicy;
        localPolicy.config = LoadLocalPolicyConfig();
        BossLocalPolicyState bossLocalPolicy;
        bool bothBossTurn = false;
        auto apiCooldownUntil = std::chrono::steady_clock::time_point{};
        do {
            const auto now = std::chrono::steady_clock::now();
            if (useApi && continuous && now < apiCooldownUntil) {
                const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                    apiCooldownUntil - now).count() + 1;
                PostAIStatus(window, "API: rate-limit cooldown (" +
                    std::to_string(remaining) + "s remaining).\r\nAPI call: skipped\r\n\r\n" +
                    ExecuteLocalActors(localPolicy, bossLocalPolicy, actorMode, interval), true);
            } else if (useApi) {
                bool apiExecuted = false;
                ControlledActorMode decisionActor = actorMode;
                if (actorMode == ControlledActorMode::Both) {
                    decisionActor = bothBossTurn
                        ? ControlledActorMode::Boss : ControlledActorMode::Player;
                    bothBossTurn = !bothBossTurn;
                }
                const std::string apiResult = ExecuteAIStepCore(window, decisionActor, &apiExecuted);
                if (continuous && !apiExecuted && !gAIStopRequested) {
                    const bool rateLimited = apiResult.find("HTTP error 429") != std::string::npos ||
                        apiResult.find("RESOURCE_EXHAUSTED") != std::string::npos;
                    if (rateLimited) apiCooldownUntil = std::chrono::steady_clock::now() + kRateLimitCooldown;
                    PostAIStatus(window, "API decision failed; using one local fallback.\r\n\r\n" +
                        (rateLimited ? std::string("Rate limit detected. API calls paused for 60 seconds.\r\n") : std::string{}) +
                        apiResult + "\r\n\r\n" + ExecuteLocalActors(
                            localPolicy, bossLocalPolicy, decisionActor, interval), true);
                } else {
                    PostAIStatus(window, apiResult, true);
                }
            } else {
                PostAIStatus(window, ExecuteLocalActors(
                    localPolicy, bossLocalPolicy, actorMode, interval), true);
            }
            if (!continuous || gAIStopRequested) break;
            std::unique_lock lock(gAIWaitMutex);
            gAIWaitCondition.wait_for(lock, std::chrono::milliseconds(interval),
                [] { return gAIStopRequested.load(); });
        } while (!gAIStopRequested);
        gAIWorkerRunning = false;
        if (continuous && gAIStopRequested) PostAIStatus(window,
            mode == AIWorkerMode::LocalContinuous ? "Local AI stopped." : "API Continuous stopped.", true);
    });
}

void StopAIWorker() {
    gAIStopRequested = true;
    gAIWaitCondition.notify_all();
}

void StartOneShotWorker(HWND window, std::function<std::string()> task,
    const wchar_t* startingText = nullptr, bool preserveResult = true) {
    if (gAIWorkerRunning.exchange(true)) return;
    if (gAIWorker.joinable()) gAIWorker.join();
    gAIStopRequested = false;
    if (startingText) SetWindowTextW(gStatusText, startingText);
    gAIWorker = std::thread([window, task = std::move(task), preserveResult] {
        std::string result;
        try {
            result = task();
        } catch (const std::exception& exception) {
            result = std::string("Operation failed safely. Viewer remains open.\r\nError: ") +
                exception.what();
        } catch (...) {
            result = "Operation failed safely. Viewer remains open.\r\nError: unknown exception";
        }
        PostAIStatus(window, std::move(result), preserveResult);
        gAIWorkerRunning = false;
    });
}

void StartCommandWorker(HWND window, const char* command,
    const wchar_t* startingText = nullptr, bool preserveResult = true) {
    const std::string commandCopy = command;
    StartOneShotWorker(window, [commandCopy] { return ExecuteCommandCore(commandCopy.c_str()); },
        startingText, preserveResult);
}

HWND AddButton(HWND parent, const wchar_t* text, int id, int x, int y, int width) {
    return CreateWindowW(
        L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, width, 34,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gAIProvider.Configure();
        const std::wstring configuredInterval = std::to_wstring(gAIProvider.SuggestedIntervalMilliseconds());
        CreateWindowW(L"STATIC", L"DebugAI Viewer", WS_CHILD | WS_VISIBLE,
            20, 16, 300, 28, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"Start Recording", StartRecordingId, 20, 55, 130);
        AddButton(window, L"Stop Recording", StopRecordingId, 160, 55, 130);
        AddButton(window, L"Play Latest", PlayLatestId, 300, 55, 120);
        AddButton(window, L"Stop Replay", StopReplayId, 430, 55, 120);
        AddButton(window, L"Refresh", RefreshId, 560, 55, 90);
        CreateWindowW(L"STATIC", L"API:", WS_CHILD | WS_VISIBLE,
            20, 104, 45, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"API Step", AIStepId, 65, 96, 110);
        AddButton(window, L"Start API", AIStartId, 185, 96, 115);
        CreateWindowW(L"STATIC", L"LOCAL:", WS_CHILD | WS_VISIBLE,
            320, 104, 60, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"Start Local", LocalStartId, 380, 96, 120);
        AddButton(window, L"Stop", AIStopId, 510, 96, 80);
        CreateWindowW(L"STATIC", L"Interval ms:", WS_CHILD | WS_VISIBLE,
            20, 137, 75, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gAIInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", configuredInterval.c_str(),
            WS_CHILD | WS_VISIBLE | ES_NUMBER, 95, 133, 85, 26, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(AIIntervalId)), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Actor:", WS_CHILD | WS_VISIBLE,
            210, 137, 50, 24, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gAIActorMode = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            260, 133, 150, 180, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(AIActorModeId)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(gAIActorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Player"));
        SendMessageW(gAIActorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Boss"));
        SendMessageW(gAIActorMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Both"));
        SendMessageW(gAIActorMode, CB_SETCURSEL, 0, 0);
        CreateWindowW(L"STATIC", L"Game Project Folder:", WS_CHILD | WS_VISIBLE,
            20, 168, 160, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        const std::wstring savedProjectFolder = LoadProjectFolder();
        gProjectFolder = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", savedProjectFolder.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 190, 450, 27, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProjectFolderId)), GetModuleHandleW(nullptr), nullptr);
        AddButton(window, L"Browse...", BrowseProjectId, 480, 187, 80);
        AddButton(window, L"Scan Project", ScanProjectId, 570, 187, 80);
        AddButton(window, L"Generate Action Profile", GenerateProfileId, 20, 225, 190);
        AddButton(window, L"Generate State Mapping", GenerateStateProfileId, 220, 225, 190);
        AddButton(window, L"Generate Local Policy", GenerateLocalPolicyId, 420, 225, 190);
        CreateWindowW(L"STATIC", L"Scan Targets (comma separated; blank = automatic):", WS_CHILD | WS_VISIBLE,
            20, 266, 390, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        const std::wstring savedScanTargets = LoadScanTargets();
        gScanTargets = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", savedScanTargets.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 288, 630, 27, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ScanTargetsId)), GetModuleHandleW(nullptr), nullptr);
        const std::wstring configuredGoal = Utf8ToWide(gAIProvider.Goal());
        CreateWindowW(L"STATIC", L"AI Goal / Instruction:", WS_CHILD | WS_VISIBLE,
            20, 325, 170, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        gAIGoal = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", configuredGoal.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            20, 347, 630, 58, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(AIGoalId)), GetModuleHandleW(nullptr), nullptr);
        gStatusText = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"Game connection: checking...",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            20, 415, 630, 205,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusTextId)),
            GetModuleHandleW(nullptr),
            nullptr);
        StartCommandWorker(window, "status", L"Game connection: checking...", false);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case StartRecordingId: StartCommandWorker(window, "start_recording", L"Starting recording..."); return 0;
        case StopRecordingId: StartCommandWorker(window, "stop_recording", L"Stopping recording..."); return 0;
        case PlayLatestId: StartCommandWorker(window, "play_latest", L"Starting replay..."); return 0;
        case StopReplayId: StartCommandWorker(window, "stop_replay", L"Stopping replay..."); return 0;
        case RefreshId: StartCommandWorker(window, "status", L"Refreshing..."); return 0;
        case ExecuteFirstActionId:
            StartOneShotWorker(window, ExecuteFirstAvailableActionCore, L"Executing first action..."); return 0;
        case AIStepId: StartAIWorker(window, AIWorkerMode::ApiStep); return 0;
        case AIStartId: StartAIWorker(window, AIWorkerMode::ApiContinuous); return 0;
        case LocalStartId: StartAIWorker(window, AIWorkerMode::LocalContinuous); return 0;
        case AIStopId: StopAIWorker(); return 0;
        case BrowseProjectId: {
            std::filesystem::path selected;
            if (BrowseForProjectFolder(window, selected)) {
                SetWindowTextW(gProjectFolder, selected.c_str());
                SaveProjectFolder(selected);
                SetWindowTextW(gStatusText, L"Project folder saved locally. It is excluded from Git.");
            }
            return 0;
        }
        case ScanProjectId: {
            const std::filesystem::path folder(ReadWindowText(gProjectFolder));
            SaveProjectFolder(folder);
            StartOneShotWorker(window, [folder] { return ScanProjectFolder(folder); },
                L"Scanning project files...");
            return 0;
        }
        case GenerateProfileId: {
            const std::filesystem::path folder(ReadWindowText(gProjectFolder));
            SaveProjectFolder(folder);
            StartOneShotWorker(window, [folder] { return GenerateActionProfile(folder); },
                L"Generating Action Profile locally...");
            return 0;
        }
        case GenerateStateProfileId: {
            const std::filesystem::path folder(ReadWindowText(gProjectFolder));
            const std::wstring targets = ReadWindowText(gScanTargets);
            SaveWorkspaceSettings(folder, targets);
            const std::string targetText = WideToUtf8(targets);
            StartOneShotWorker(window, [folder, targetText] { return GenerateStateMappingProfile(folder, targetText); },
                L"Generating State Mapping Profile locally...");
            return 0;
        }
        case GenerateLocalPolicyId:
            ApplyViewerGoal();
            StartOneShotWorker(window, GenerateLocalPolicyCore,
                L"Generating Local Policy from AI Goal...");
            return 0;
        default: break;
        }
        break;

    case WM_TIMER:
        return 0;

    case kAIStatusMessage: {
        const ULONGLONG now = GetTickCount64();
        if (wParam == 0 && now < gAutoRefreshResumeTick) {
            return 0;
        }
        if (wParam != 0) gAutoRefreshResumeTick = now + 15000;
        std::string status;
        {
            std::lock_guard lock(gAIStatusMutex);
            status = gPendingAIStatus;
        }
        SetWindowTextW(gStatusText, Utf8ToWide(status).c_str());
        return 0;
    }

    case WM_DESTROY:
        StopAIWorker();
        if (gAIWorker.joinable()) gAIWorker.join();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    std::wstring arguments = commandLine ? commandLine : L"";
    constexpr std::wstring_view diagnosticPrefix = L"--scan-diagnostic";
    if (arguments.starts_with(diagnosticPrefix)) {
        arguments.erase(0, diagnosticPrefix.size());
        const auto first = arguments.find_first_not_of(L" \t");
        if (first != std::wstring::npos) arguments.erase(0, first);
        const auto last = arguments.find_last_not_of(L" \t");
        if (last != std::wstring::npos) arguments.erase(last + 1);
        if (arguments.size() >= 2 && arguments.front() == L'"' && arguments.back() == L'"') {
            arguments = arguments.substr(1, arguments.size() - 2);
        }
        const std::filesystem::path root(arguments);
        std::string diagnostic;
        int resultCode = 0;
        try {
            diagnostic = "SCAN\r\n" + ScanProjectFolder(root) +
                "\r\n\r\nSTATE MAPPING\r\n" + GenerateStateMappingProfile(root, "");
        } catch (const std::exception& exception) {
            diagnostic = std::string("Scan diagnostic failed safely: ") + exception.what();
            resultCode = 2;
        } catch (...) {
            diagnostic = "Scan diagnostic failed safely: unknown exception";
            resultCode = 3;
        }
        const auto outputPath = std::filesystem::current_path() /
            "generated/debug_ai/viewer/scan_diagnostic.txt";
        std::error_code error;
        std::filesystem::create_directories(outputPath.parent_path(), error);
        std::ofstream output(outputPath, std::ios::trunc);
        if (output) {
            output << "Root: " << WideToUtf8(root.wstring()) << "\n" << diagnostic << '\n';
        }
        return resultCode;
    }
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&windowClass)) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        L"DebugAI Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 710,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
