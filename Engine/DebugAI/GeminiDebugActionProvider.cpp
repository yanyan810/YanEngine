#include "GeminiDebugActionProvider.h"

#include "DebugJson.h"

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace {

using json = nlohmann::json;

std::string GetEnvironmentString(const char* name) {
    const DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
    if (size == 0) {
        return {};
    }

    std::string value(size, '\0');
    const DWORD written = GetEnvironmentVariableA(name, value.data(), size);
    if (written == 0) {
        return {};
    }
    value.resize(written);
    return value;
}

unsigned long long GetEnvironmentUInt64(const char* name, unsigned long long fallbackValue) {
    const std::string value = GetEnvironmentString(name);
    if (value.empty()) {
        return fallbackValue;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    return (end != value.c_str()) ? parsed : fallbackValue;
}

std::wstring ToWideString(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

bool IsTruthy(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

}

bool GeminiDebugActionProvider::ConfigureFromEnvironment() {
    const std::string enabled = GetEnvironmentString("DEBUGAI_GEMINI_ENABLED");
    if (!IsTruthy(enabled)) {
        lastStatus_ = "Gemini disabled. Set DEBUGAI_GEMINI_ENABLED=1 to enable.";
        return false;
    }

    apiKey_ = GetEnvironmentString("GEMINI_API_KEY");
    if (apiKey_.empty()) {
        lastStatus_ = "GEMINI_API_KEY is not set.";
        return false;
    }

    if (const std::string model = GetEnvironmentString("DEBUGAI_GEMINI_MODEL"); !model.empty()) {
        model_ = model;
    }
    if (const std::string goal = GetEnvironmentString("DEBUGAI_GEMINI_GOAL"); !goal.empty()) {
        goal_ = goal;
    }
    requestIntervalFrames_ = GetEnvironmentUInt64("DEBUGAI_GEMINI_INTERVAL_FRAMES", requestIntervalFrames_);
    timeoutMilliseconds_ = static_cast<unsigned int>(GetEnvironmentUInt64("DEBUGAI_GEMINI_TIMEOUT_MS", timeoutMilliseconds_));
    lastStatus_ = "Gemini provider configured.";
    return true;
}

bool GeminiDebugActionProvider::RequestActionJson(const DebugGameState& state, std::string& outJsonResponse) {
    outJsonResponse.clear();

    if (!IsConfigured()) {
        lastStatus_ = "Gemini provider is not configured.";
        return false;
    }

    if (requestPending_) {
        if (pendingResponseJson_.valid() &&
            pendingResponseJson_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const std::string responseJson = pendingResponseJson_.get();
            requestPending_ = false;
            if (!responseJson.empty()) {
                cachedResponseJson_ = responseJson;
                hasCachedResponse_ = true;
                outJsonResponse = cachedResponseJson_;
                lastStatus_ = "Gemini async action received.";
                return true;
            }
            lastStatus_ = "Gemini async request failed.";
        } else {
            lastStatus_ = "Gemini async request pending.";
            return false;
        }
    }

    if (hasCachedResponse_ &&
        requestIntervalFrames_ > 0 &&
        state.frameNumber < lastRequestFrame_ + requestIntervalFrames_) {
        outJsonResponse = cachedResponseJson_;
        lastStatus_ = "Gemini cached action.";
        return true;
    }

    const std::string requestBody = BuildRequestBody_(state);
    lastRequestFrame_ = state.frameNumber;
    requestPending_ = true;
    pendingResponseJson_ = std::async(std::launch::async, [this, requestBody]() {
        std::string responseBody;
        std::string status;
        if (!PostJson_(requestBody, responseBody, &status)) {
            return std::string{};
        }

        std::string outputText;
        if (!ExtractOutputText_(responseBody, outputText)) {
            return std::string{};
        }

        return outputText;
    });

    lastStatus_ = "Gemini async request started.";
    return false;
}

std::string GeminiDebugActionProvider::BuildRequestBody_(const DebugGameState& state) const {
    std::vector<std::string> actionNames;
    actionNames.reserve(state.availableActions.size());
    for (const DebugAction& action : state.availableActions) {
        if (!action.name.empty()) {
            actionNames.push_back(action.name);
        }
    }
    if (actionNames.empty()) {
        actionNames.push_back("Wait");
    }

    json schema = {
        { "type", "object" },
        { "properties", {
            { "reason", {
                { "type", "string" },
                { "description", "Short reason for choosing this action." },
            }},
            { "action", {
                { "type", "object" },
                { "properties", {
                    { "name", {
                        { "type", "string" },
                        { "enum", actionNames },
                    }},
                    { "targetId", { { "type", "string" } } },
                    { "intParam", { { "type", "integer" } } },
                    { "floatParam", { { "type", "number" } } },
                    { "stringParam", { { "type", "string" } } },
                    { "holdFrames", {
                        { "type", "integer" },
                        { "minimum", 1 },
                        { "maximum", 60 },
                    }},
                }},
                { "required", { "name", "targetId", "intParam", "floatParam", "stringParam", "holdFrames" } },
            }},
        }},
        { "required", { "reason", "action" } },
    };

    std::ostringstream prompt;
    prompt
        << "You are controlling a game through semantic debug actions.\n"
        << "Choose exactly one action from availableActions.\n"
        << "Do not invent action names. Prefer varied exploration and progress.\n"
        << "Use targetId only when it appears in entities. Otherwise use an empty string.\n"
        << "Return only JSON that matches the schema.\n"
        << "Current state JSON:\n"
        << DebugJson::ToAiStateJsonString(state, goal_);

    json request = {
        { "system_instruction", {
            { "parts", json::array({
                {
                    { "text", "Return a single valid DebugAI action for the current game state." },
                },
            }) },
        }},
        { "contents", json::array({
            {
                { "parts", json::array({
                    {
                        { "text", prompt.str() },
                    },
                }) },
            },
        }) },
        { "generationConfig", {
            { "responseFormat", {
                { "text", {
                    { "mimeType", "application/json" },
                    { "schema", schema },
                }},
            }},
            { "maxOutputTokens", 200 },
        }},
    };

    return request.dump();
}

bool GeminiDebugActionProvider::PostJson_(const std::string& requestBody, std::string& outResponseBody, std::string* outStatus) const {
    outResponseBody.clear();

    HINTERNET session = WinHttpOpen(
        L"DebugAI Gemini Provider/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        if (outStatus != nullptr) {
            *outStatus = "WinHttpOpen failed.";
        }
        return false;
    }

    WinHttpSetTimeouts(
        session,
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_));

    HINTERNET connection = WinHttpConnect(session, L"generativelanguage.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        if (outStatus != nullptr) {
            *outStatus = "WinHttpConnect failed.";
        }
        return false;
    }

    const std::wstring path = L"/v1beta/models/" + ToWideString(model_) + L":generateContent";
    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (outStatus != nullptr) {
            *outStatus = "WinHttpOpenRequest failed.";
        }
        return false;
    }

    const std::wstring headers =
        L"Content-Type: application/json\r\n"
        L"x-goog-api-key: " + ToWideString(apiKey_) + L"\r\n";

    const BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(requestBody.data()),
        static_cast<DWORD>(requestBody.size()),
        static_cast<DWORD>(requestBody.size()),
        0);

    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (outStatus != nullptr) {
            *outStatus = "Gemini request failed.";
        }
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);

    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            break;
        }
        if (available == 0) {
            break;
        }

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
            break;
        }
        chunk.resize(read);
        outResponseBody += chunk;
    } while (available > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (statusCode < 200 || statusCode >= 300) {
        std::ostringstream message;
        message << "Gemini HTTP " << statusCode;
        if (!outResponseBody.empty()) {
            message << ": " << outResponseBody.substr(0, 256);
        }
        if (outStatus != nullptr) {
            *outStatus = message.str();
        }
        return false;
    }

    return true;
}

bool GeminiDebugActionProvider::ExtractOutputText_(const std::string& responseBody, std::string& outText) const {
    outText.clear();

    try {
        const json response = json::parse(responseBody);
        const auto candidatesIt = response.find("candidates");
        if (candidatesIt == response.end() || !candidatesIt->is_array()) {
            return false;
        }

        for (const json& candidate : *candidatesIt) {
            const auto contentIt = candidate.find("content");
            if (contentIt == candidate.end() || !contentIt->is_object()) {
                continue;
            }

            const auto partsIt = contentIt->find("parts");
            if (partsIt == contentIt->end() || !partsIt->is_array()) {
                continue;
            }

            for (const json& part : *partsIt) {
                if (const auto textIt = part.find("text"); textIt != part.end() && textIt->is_string()) {
                    outText = textIt->get<std::string>();
                    return !outText.empty();
                }
            }
        }
    } catch (...) {
        return false;
    }

    return false;
}
