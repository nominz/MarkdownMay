#include "markdownmay/platform/single_instance.hpp"

#include <sddl.h>

#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace markdownmay::platform {
namespace {
constexpr std::uint32_t kMagic = 0x594D444D;
constexpr std::uint16_t kVersion = 1;

#pragma pack(push, 1)
struct Header final {
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t path_count{};
    std::uint32_t payload_bytes{};
};
#pragma pack(pop)

template <typename T>
void Append(std::vector<std::byte>& output, const T& value) {
    const auto* begin = reinterpret_cast<const std::byte*>(&value);
    output.insert(output.end(), begin, begin + sizeof(value));
}

std::wstring UserSuffix() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return L"fallback";
    DWORD needed{};
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    std::vector<std::byte> buffer(needed);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed)) {
        CloseHandle(token); return L"fallback";
    }
    CloseHandle(token);
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR sid_text{};
    if (!ConvertSidToStringSidW(user->User.Sid, &sid_text)) return L"fallback";
    std::uint64_t hash = 1469598103934665603ULL;
    for (const wchar_t* cursor = sid_text; *cursor; ++cursor) {
        hash ^= static_cast<std::uint16_t>(*cursor);
        hash *= 1099511628211ULL;
    }
    LocalFree(sid_text);
    std::wostringstream text;
    text << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return text.str();
}

bool ReadExact(HANDLE pipe, void* data, DWORD size) {
    auto* cursor = static_cast<std::byte*>(data);
    DWORD total{};
    while (total < size) {
        DWORD read{};
        if (!ReadFile(pipe, cursor + total, size - total, &read, nullptr) || !read)
            return false;
        total += read;
    }
    return true;
}
}

Result<std::vector<std::byte>> EncodeOpenRequest(
    std::span<const std::filesystem::path> paths) {
    if (paths.size() > kMaximumOpenPaths)
        return Result<std::vector<std::byte>>::failure(ErrorCode::platform_ipc_invalid_message);
    std::vector<std::byte> payload;
    for (const auto& path : paths) {
        const auto value = path.wstring();
        const auto bytes = value.size() * sizeof(wchar_t);
        if (bytes > UINT32_MAX || payload.size() + sizeof(std::uint32_t) + bytes > kMaximumIpcPayload)
            return Result<std::vector<std::byte>>::failure(ErrorCode::platform_ipc_invalid_message);
        const auto length = static_cast<std::uint32_t>(bytes);
        Append(payload, length);
        const auto* begin = reinterpret_cast<const std::byte*>(value.data());
        payload.insert(payload.end(), begin, begin + bytes);
    }
    Header header{kMagic, kVersion, static_cast<std::uint16_t>(paths.size()),
                  static_cast<std::uint32_t>(payload.size())};
    std::vector<std::byte> result;
    result.reserve(sizeof(header) + payload.size());
    Append(result, header);
    result.insert(result.end(), payload.begin(), payload.end());
    return Result<std::vector<std::byte>>::success(std::move(result));
}

Result<std::vector<std::filesystem::path>> DecodeOpenRequest(
    std::span<const std::byte> message) {
    if (message.size() < sizeof(Header))
        return Result<std::vector<std::filesystem::path>>::failure(ErrorCode::platform_ipc_invalid_message);
    Header header{};
    std::memcpy(&header, message.data(), sizeof(header));
    if (header.magic != kMagic || header.version != kVersion ||
        header.path_count > kMaximumOpenPaths || header.payload_bytes > kMaximumIpcPayload ||
        message.size() != sizeof(Header) + header.payload_bytes)
        return Result<std::vector<std::filesystem::path>>::failure(ErrorCode::platform_ipc_invalid_message);
    std::vector<std::filesystem::path> paths;
    std::size_t offset = sizeof(Header);
    for (std::uint16_t index = 0; index < header.path_count; ++index) {
        if (offset + sizeof(std::uint32_t) > message.size())
            return Result<std::vector<std::filesystem::path>>::failure(ErrorCode::platform_ipc_invalid_message);
        std::uint32_t bytes{};
        std::memcpy(&bytes, message.data() + offset, sizeof(bytes));
        offset += sizeof(bytes);
        if ((bytes % sizeof(wchar_t)) != 0 || offset + bytes > message.size())
            return Result<std::vector<std::filesystem::path>>::failure(ErrorCode::platform_ipc_invalid_message);
        const auto* text = reinterpret_cast<const wchar_t*>(message.data() + offset);
        paths.emplace_back(std::wstring(text, bytes / sizeof(wchar_t)));
        offset += bytes;
    }
    if (offset != message.size())
        return Result<std::vector<std::filesystem::path>>::failure(ErrorCode::platform_ipc_invalid_message);
    return Result<std::vector<std::filesystem::path>>::success(std::move(paths));
}

SingleInstance::~SingleInstance() { stop(); }

ErrorCode SingleInstance::acquire() {
    if (mutex_) return ErrorCode::ok;
    const auto suffix = UserSuffix();
    pipe_name_ = L"\\\\.\\pipe\\MarkdownMay." + suffix + L".Open";
    const auto mutex_name = L"Local\\MarkdownMay." + suffix + L".Mutex";
    mutex_ = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (!mutex_) return ErrorCode::platform_single_instance_failed;
    primary_ = GetLastError() != ERROR_ALREADY_EXISTS;
    return ErrorCode::ok;
}

bool SingleInstance::is_primary() const noexcept { return primary_; }

ErrorCode SingleInstance::start_receiver(Receiver receiver) {
    if (!primary_ || receiver_thread_.joinable())
        return ErrorCode::platform_single_instance_failed;
    receiver_ = std::move(receiver);
    stopping_ = false;
    receiver_thread_ = std::thread([this] { ReceiveLoop(); });
    return ErrorCode::ok;
}

ErrorCode SingleInstance::send(std::span<const std::filesystem::path> paths) const {
    const auto encoded = EncodeOpenRequest(paths);
    if (!encoded.is_ok()) return encoded.error();
    HANDLE pipe = INVALID_HANDLE_VALUE;
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        pipe = CreateFileW(pipe_name_.c_str(), GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY)
            return ErrorCode::platform_ipc_send_failed;
        (void)WaitNamedPipeW(pipe_name_.c_str(), 50);
        Sleep(10);
    }
    if (pipe == INVALID_HANDLE_VALUE) return ErrorCode::platform_ipc_send_failed;
    DWORD written{};
    const bool ok = WriteFile(pipe, encoded.value().data(),
        static_cast<DWORD>(encoded.value().size()), &written, nullptr) &&
        written == encoded.value().size();
    CloseHandle(pipe);
    return ok ? ErrorCode::ok : ErrorCode::platform_ipc_send_failed;
}

void SingleInstance::stop() noexcept {
    stopping_ = true;
    if (receiver_thread_.joinable()) {
        (void)WaitNamedPipeW(pipe_name_.c_str(), 1000);
        const HANDLE wake = CreateFileW(pipe_name_.c_str(), GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
        receiver_thread_.join();
    }
    if (mutex_) { CloseHandle(mutex_); mutex_ = nullptr; }
}

void SingleInstance::ReceiveLoop() {
    while (!stopping_) {
        const HANDLE pipe = CreateNamedPipeW(pipe_name_.c_str(), PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
            4096, static_cast<DWORD>(kMaximumIpcPayload + sizeof(Header)), 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return;
        const bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected && !stopping_) {
            Header header{};
            if (ReadExact(pipe, &header, sizeof(header)) &&
                header.payload_bytes <= kMaximumIpcPayload) {
                std::vector<std::byte> message(sizeof(header) + header.payload_bytes);
                std::memcpy(message.data(), &header, sizeof(header));
                if (ReadExact(pipe, message.data() + sizeof(header), header.payload_bytes)) {
                    auto decoded = DecodeOpenRequest(message);
                    if (decoded.is_ok() && receiver_) receiver_(std::move(decoded).value());
                }
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

}  // namespace markdownmay::platform
