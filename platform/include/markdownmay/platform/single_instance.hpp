#pragma once

#include "markdownmay/core/result.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <thread>
#include <vector>

namespace markdownmay::platform {

inline constexpr std::size_t kMaximumOpenPaths = 128;
inline constexpr std::size_t kMaximumIpcPayload = 1024 * 1024;

[[nodiscard]] Result<std::vector<std::byte>> EncodeOpenRequest(
    std::span<const std::filesystem::path> paths);
[[nodiscard]] Result<std::vector<std::filesystem::path>> DecodeOpenRequest(
    std::span<const std::byte> message);

class SingleInstance final {
public:
    using Receiver = std::function<void(std::vector<std::filesystem::path>)>;

    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    [[nodiscard]] ErrorCode acquire();
    [[nodiscard]] bool is_primary() const noexcept;
    [[nodiscard]] ErrorCode start_receiver(Receiver receiver);
    [[nodiscard]] ErrorCode send(
        std::span<const std::filesystem::path> paths) const;
    void stop() noexcept;

private:
    void ReceiveLoop();

    HANDLE mutex_{};
    std::wstring pipe_name_;
    bool primary_{};
    std::atomic_bool stopping_{};
    Receiver receiver_;
    std::thread receiver_thread_;
};

}  // namespace markdownmay::platform
