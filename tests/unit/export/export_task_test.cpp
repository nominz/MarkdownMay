#include "markdownmay/export/export_task.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct TemporaryDirectory final {
    std::filesystem::path path;
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

std::size_t ExportTemporaryCount(const std::filesystem::path& directory) {
    std::size_t count{};
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().filename().wstring().find(L".markdownmay.export-") !=
            std::wstring::npos) ++count;
    return count;
}

}  // namespace

int RunExportTaskTests() {
    using namespace markdownmay;
    using namespace markdownmay::exporting;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory{std::filesystem::temp_directory_path() /
        ("markdownmay-export-task-" + std::to_string(nonce))};
    std::filesystem::create_directories(directory.path);
    const auto target = directory.path / "result.bin";
    { std::ofstream existing(target, std::ios::binary); existing << "old"; }

    ExportDocument document{7, ExportScope::full, {}};
    std::vector<ExportProgress> progress;
    const ExportWriter success = [](const ExportDocument& value,
        const std::filesystem::path& temporary, const CancellationToken& token,
        const ExportProgressSink& report) {
        if (value.revision != 7 || token.is_cancelled()) return ErrorCode::export_target_failed;
        report({ExportStage::writing, 40, 100});
        report({ExportStage::writing, 20, 100});
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "new-content";
        return output.good() ? ErrorCode::ok : ErrorCode::export_target_failed;
    };
    const ExportValidator valid = [](const std::filesystem::path& temporary) {
        return ReadAll(temporary) == "new-content" ?
            ErrorCode::ok : ErrorCode::export_validation_failed;
    };
    if (RunExportTask(document, target, success, valid, {},
            [&](const ExportProgress& value) { progress.push_back(value); }) != ErrorCode::ok ||
        ReadAll(target) != "new-content" || ExportTemporaryCount(directory.path) != 0 ||
        progress.empty() || progress.back().stage != ExportStage::completed ||
        progress.back().completed != 100) return 20;
    for (std::size_t index = 1; index < progress.size(); ++index)
        if (progress[index].completed < progress[index - 1].completed) return 21;

    { std::ofstream existing(target, std::ios::binary | std::ios::trunc); existing << "safe"; }
    const ExportWriter failure = [](const ExportDocument&,
        const std::filesystem::path& temporary, const CancellationToken&,
        const ExportProgressSink&) {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "broken";
        return ErrorCode::export_target_failed;
    };
    if (RunExportTask(document, target, failure, valid) != ErrorCode::export_target_failed ||
        ReadAll(target) != "safe" || ExportTemporaryCount(directory.path) != 0) return 22;

    const ExportWriter removes_temporary = [](const ExportDocument&,
        const std::filesystem::path& temporary, const CancellationToken&,
        const ExportProgressSink&) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return ErrorCode::ok;
    };
    if (RunExportTask(document, target, removes_temporary, valid) !=
            ErrorCode::export_target_failed ||
        ReadAll(target) != "safe" || ExportTemporaryCount(directory.path) != 0) return 27;

    const ExportValidator invalid = [](const std::filesystem::path&) {
        return ErrorCode::export_validation_failed;
    };
    if (RunExportTask(document, target, success, invalid) !=
            ErrorCode::export_validation_failed ||
        ReadAll(target) != "safe" || ExportTemporaryCount(directory.path) != 0) return 23;

    CancellationSource before;
    before.cancel();
    if (RunExportTask(document, target, success, valid, before.token()) !=
            ErrorCode::export_cancelled || ReadAll(target) != "safe" ||
        ExportTemporaryCount(directory.path) != 0) return 24;

    CancellationSource during;
    const ExportWriter cancel_after_write = [&](const ExportDocument&,
        const std::filesystem::path& temporary, const CancellationToken&,
        const ExportProgressSink&) {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "cancelled";
        output.close();
        during.cancel();
        return ErrorCode::ok;
    };
    if (RunExportTask(document, target, cancel_after_write, valid, during.token()) !=
            ErrorCode::export_cancelled || ReadAll(target) != "safe" ||
        ExportTemporaryCount(directory.path) != 0) return 25;

    if (RunExportTask(document, {}, success, valid) != ErrorCode::export_invalid_options ||
        RunExportTask(document, target, {}, valid) != ErrorCode::export_invalid_options ||
        RunExportTask(document, target, success, {}) != ErrorCode::export_invalid_options)
        return 26;
    return 0;
}
