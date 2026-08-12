#include "markdownmay/export/export_task.hpp"

#include "markdownmay/fileio/path_utils.hpp"

#include <windows.h>

#include <algorithm>

namespace markdownmay::exporting {
namespace {

std::atomic_uint64_t g_export_temporary_sequence{1};

class TemporaryExport final {
public:
    explicit TemporaryExport(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryExport() {
        if (!committed_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }
    TemporaryExport(const TemporaryExport&) = delete;
    TemporaryExport& operator=(const TemporaryExport&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    void release() noexcept { committed_ = true; }

private:
    std::filesystem::path path_;
    bool committed_{};
};

Result<std::filesystem::path> ReserveTemporaryBeside(
    const std::filesystem::path& target) {
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto suffix = L".markdownmay.export-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(g_export_temporary_sequence.fetch_add(1));
        const auto temporary = target.parent_path() /
            (target.filename().wstring() + suffix);
        const HANDLE file = CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            return Result<std::filesystem::path>::success(temporary);
        }
        if (GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            return Result<std::filesystem::path>::failure(
                ErrorCode::export_target_failed);
    }
    return Result<std::filesystem::path>::failure(ErrorCode::export_target_failed);
}

ErrorCode CommitTemporary(
    const std::filesystem::path& temporary,
    const std::filesystem::path& target) noexcept {
    const bool exists = std::filesystem::exists(target);
    const BOOL committed = exists
        ? ReplaceFileW(target.c_str(), temporary.c_str(), nullptr,
            REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
        : MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    return committed ? ErrorCode::ok : ErrorCode::export_target_failed;
}

ErrorCode FlushTemporary(const std::filesystem::path& temporary) noexcept {
    const HANDLE file = CreateFileW(
        temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return ErrorCode::export_target_failed;
    const BOOL flushed = FlushFileBuffers(file);
    CloseHandle(file);
    return flushed ? ErrorCode::ok : ErrorCode::export_target_failed;
}

class ProgressReporter final {
public:
    explicit ProgressReporter(ExportProgressSink sink) : sink_(std::move(sink)) {}
    void report(ExportStage stage, std::uint32_t completed) noexcept {
        const auto value = (std::max)(last_, (std::min)(completed, 100U));
        last_ = value;
        if (!sink_) return;
        try { sink_({stage, value, 100}); } catch (...) {}
    }
    [[nodiscard]] ExportProgressSink writer_sink() {
        return [this](const ExportProgress& value) {
            report(ExportStage::writing, (std::min)(value.completed, 79U));
        };
    }

private:
    ExportProgressSink sink_;
    std::uint32_t last_{};
};

}  // namespace

CancellationToken::CancellationToken(
    std::shared_ptr<std::atomic_bool> state) noexcept : state_(std::move(state)) {}

bool CancellationToken::is_cancelled() const noexcept {
    return state_ && state_->load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : state_(std::make_shared<std::atomic_bool>(false)) {}

CancellationToken CancellationSource::token() const noexcept {
    return CancellationToken(state_);
}

void CancellationSource::cancel() noexcept {
    state_->store(true, std::memory_order_release);
}

ErrorCode RunExportTask(
    const ExportDocument& document,
    const std::filesystem::path& target,
    const ExportWriter& writer,
    const ExportValidator& validator,
    const CancellationToken& cancellation,
    const ExportProgressSink& progress) {
    ProgressReporter reporter(progress);
    reporter.report(ExportStage::preparing, 0);
    if (cancellation.is_cancelled()) return ErrorCode::export_cancelled;
    if (!writer || !validator || target.empty()) return ErrorCode::export_invalid_options;
    auto normalized = fileio::NormalizeAbsolutePath(target);
    if (!normalized.is_ok() || normalized.value().filename().empty())
        return ErrorCode::export_target_failed;
    auto reserved = ReserveTemporaryBeside(normalized.value());
    if (!reserved.is_ok()) return reserved.error();
    TemporaryExport temporary(std::move(reserved).value());

    reporter.report(ExportStage::writing, 5);
    ErrorCode result = ErrorCode::export_target_failed;
    try {
        result = writer(document, temporary.path(), cancellation, reporter.writer_sink());
    } catch (...) {
        result = ErrorCode::export_target_failed;
    }
    if (result != ErrorCode::ok) return result;
    if (cancellation.is_cancelled()) return ErrorCode::export_cancelled;
    result = FlushTemporary(temporary.path());
    if (result != ErrorCode::ok) return result;

    reporter.report(ExportStage::validating, 80);
    try { result = validator(temporary.path()); }
    catch (...) { result = ErrorCode::export_validation_failed; }
    if (result != ErrorCode::ok) return result;
    if (cancellation.is_cancelled()) return ErrorCode::export_cancelled;

    reporter.report(ExportStage::committing, 95);
    result = CommitTemporary(temporary.path(), normalized.value());
    if (result != ErrorCode::ok) return result;
    temporary.release();
    reporter.report(ExportStage::completed, 100);
    return ErrorCode::ok;
}

}  // namespace markdownmay::exporting
