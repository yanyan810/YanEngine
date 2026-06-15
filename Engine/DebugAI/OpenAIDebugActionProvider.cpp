#include "OpenAIDebugActionProvider.h"

#include "DebugJson.h"

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <winhttp.h>

#include <algorithm>
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

bool OpenAIDebugActionProvider::ConfigureFromEnvironment() {
    const std::string enabled = GetEnvironmentString("DEBUGAI_OPENAI_ENABLED");
    if (!IsTruthy(enabled)) {
        lastStatus_ = "OpenAI disabled. Set DEBUGAI_OPENAI_ENABLED=1 to enable.";
        return false;
    }

    apiKey_ = GetEnvironmentString("OPENAI_API_KEY");
    if (apiKey_.empty()) {
        lastStatus_ = "OPENAI_API_KEY is not set.";
        return false;
    }

    if (const std::string model = GetEnvironmentString("DEBUGAI_OPENAI_MODEL"); !model.empty()) {
        model_ = model;
    }
    if (const std::string goal = GetEnvironmentString("DEBUGAI_OPENAI_GOAL"); !goal.empty()) {
        goal_ = goal;
    }
    requestIntervalFrames_ = GetEnvironmentUInt64("DEBUGAI_OPENAI_INTERVAL_FRAMES", requestIntervalFrames_);
    timeoutMilliseconds_ = static_cast<unsigned int>(GetEnvironmentUInt64("DEBUGAI_OPENAI_TIMEOUT_MS", timeoutMilliseconds_));
    lastStatus_ = "OpenAI provider configured.";
    return true;
}

bool OpenAIDebugActionProvider::RequestActionJson(const DebugGameState& state, std::string& outJsonResponse) {
    outJsonResponse.clear();

    if (!IsConfigured()) {
        lastStatus_ = "OpenAI provider is not configured.";
        return false;
    }

    if (hasCachedResponse_ &&
        requestIntervalFrames_ > 0 &&
        state.frameNumber < lastRequestFrame_ + requestIntervalFrames_) {
        outJsonResponse = cachedResponseJson_;
        lastStatus_ = "OpenAI cached action.";
        return true;
    }

    std::string responseBody;
    if (!PostJson_(BuildRequestBody_(state), responseBody)) {
        return false;
    }

    std::string outputText;
    if (!ExtractOutputText_(responseBody, outputText)) {
        lastStatus_ = "OpenAI response did not contain output text.";
        return false;
    }

    cachedResponseJson_ = outputText;
    hasCachedResponse_ = true;
    lastRequestFrame_ = state.frameNumber;
    outJsonResponse = outputText;
    lastStatus_ = "OpenAI action received.";
    return true;
}

std::string OpenAIDebugActionProvider::BuildRequestBody_(const DebugGameState& state) const {
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
                { "additionalProperties", false },
            }},
        }},
        { "required", { "reason", "action" } },
        { "additionalProperties", false },
    };

    std::ostringstream prompt;
    prompt
        << "You are controlling a game through semantic debug actions.\n"
        << "Choose exactly one action from availableActions.\n"
        << "Do not invent action names. Prefer varied exploration and progress.\n"
        << "Use targetId only when it appears in entities. Otherwise use an empty string.\n"
        << "Current state JSON:\n"
        << DebugJson::ToAiStateJsonString(state, goal_);

    json request = {
        { "model", model_ },
        { "instructions", "Return a single valid DebugAI action for the current game state." },
        { "input", json::array({
            {
                { "role", "user" },
                { "content", json::array({
                    {
                        { "type", "input_text" },
                        { "text", prompt.str() },
                    },
                }) },
            },
        }) },
        { "text", {
            { "format", {
                { "type", "json_schema" },
                { "name", "debug_action_response" },
                { "schema", schema },
                { "strict", true },
            }},
        }},
        { "max_output_tokens", 200 },
    };

    return request.dump();
}

bool OpenAIDebugActionProvider::PostJson_(const std::string& requestBody, std::string& outResponseBody) {
    outResponseBody.clear();

    HINTERNET session = WinHttpOpen(
        L"DebugAI OpenAI Provider/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        lastStatus_ = "WinHttpOpen failed.";
        return false;
    }

    WinHttpSetTimeouts(
        session,
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_),
        static_cast<int>(timeoutMilliseconds_));

    HINTERNET connection = WinHttpConnect(session, L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        lastStatus_ = "WinHttpConnect failed.";
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        L"/v1/responses",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        lastStatus_ = "WinHttpOpenRequest failed.";
        return false;
    }

    const std::wstring headers =
        L"Content-Type: application/json\r\n"
        L"Authorization: Bearer " + ToWideString(apiKey_) + L"\r\n";

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
        lastStatus_ = "OpenAI request failed.";
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
        message << "OpenAI HTTP " << statusCode;
        if (!outResponseBody.empty()) {
            message << ": " << outResponseBody.substr(0, 256);
        }
        lastStatus_ = message.str();
        return false;
    }

    return true;
}

bool OpenAIDebugActionProvider::ExtractOutputText_(const std::string& responseBody, std::string& outText) const {
    outText.clear();

    try {
        const json response = json::parse(responseBody);
        if (const auto it = response.find("output_text"); it != response.end() && it->is_string()) {
            outText = it->get<std::string>();
            return !outText.empty();
        }

        if (const auto outputIt = response.find("output"); outputIt != response.end() && outputIt->is_array()) {
            for (const json& outputItem : *outputIt) {
                const auto contentIt = outputItem.find("content");
                if (contentIt == outputItem.end() || !contentIt->is_array()) {
                    continue;
                }

                for (const json& contentItem : *contentIt) {
                    const auto typeIt = contentItem.find("type");
                    if (typeIt == contentItem.end() || !typeIt->is_string() || typeIt->get<std::string>() != "output_text") {
                        continue;
                    }

                    if (const auto textIt = contentItem.find("text"); textIt != contentItem.end() && textIt->is_string()) {
                        outText = textIt->get<std::string>();
                        return !outText.empty();
                    }
                }
            }
        }
    } catch (...) {
        return false;
    }

    return false;
}
