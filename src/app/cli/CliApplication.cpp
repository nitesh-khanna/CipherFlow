#include "CliApplication.hpp"

#include "../chunker/FileChunker.hpp"
#include "../crypto/Aes256GcmEncryptionEngine.hpp"
#include "../fileHandling/ReadEnv.hpp"
#include "../jobs/JobQueue.hpp"
#include "../metadata/InMemoryMetadataStore.hpp"
#include "../storage/LocalFileStorageAdapter.hpp"

#include <iostream>
#include <exception>
#include <string>
#include <chrono>
#include <iomanip>
#include <cstdint>

namespace vault::cli {
namespace fs = std::filesystem;

int CliApplication::run(int argc, char* argv[]) {
    CliOptions options;
    if (!parseOptions(argc, argv, options)) {
        return argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") ? 0 : 1;
    }

    chunking::FileChunker chunker;
    manifest::VaultManifest manifest;
    std::string error;
    if (!chunker.buildManifestForDirectory(options.targetPath, options.action, manifest, error)) {
        std::cerr << "Manifest error: " << error << std::endl;
        return 1;
    }

    crypto::Aes256GcmEncryptionEngine encryptionEngine;
    manifest.setAlgorithmId(encryptionEngine.algorithmId());

    if (manifest.empty()) {
        std::cout << "No regular files found. Nothing to do." << std::endl;
        return 0;
    }

    std::string keyError;
    std::optional<std::string> password = ReadEnv::readPassword(keyError);
    if (!password.has_value()) {
        std::cerr << "Password error: " << keyError << std::endl;
        return 1;
    }

    storage::LocalFileStorageAdapter storageAdapter;
    if (!storageAdapter.validateWritable(manifest, error)) {
        std::cerr << "Storage error: " << error << std::endl;
        std::cerr << "Aborting before modifying any files." << std::endl;
        return 1;
    }

    metadata::InMemoryMetadataStore metadataStore;
    jobs::JobQueue jobQueue;
    jobQueue.enqueueManifest(options.action, manifest);

    const crypto::CryptoKey key = crypto::CryptoKey::fromPassword(password.value());
    std::uint64_t totalBytes = 0;

    for (const auto& file : manifest.files()) {
        totalBytes += file.plainSize;
    }

    const auto startTime = std::chrono::steady_clock::now();
    if (!jobQueue.executeAll(encryptionEngine, key, storageAdapter, metadataStore, manifest, error, options.workerCount)) {
        std::cerr << "Job error: " << error << std::endl;
        return 1;
    }

    const auto endTime = std::chrono::steady_clock::now();

    const double elapsedSeconds =
        std::chrono::duration<double>(endTime - startTime).count();

    const double throughputMBps = elapsedSeconds > 0.0
        ? (static_cast<double>(totalBytes) / 1'000'000.0)
            / elapsedSeconds
        : 0.0;

    std::cout << "Worker threads: " << options.workerCount << '\n'
            << std::fixed << std::setprecision(3)
            << "Processing time: " << elapsedSeconds << " seconds\n"
            << "Throughput: " << throughputMBps << " MB/s\n";

    std::cout << "Processed " << manifest.fileCount()
              << " file(s) across " << manifest.chunkCount()
              << " chunk(s) using " << encryptionEngine.algorithmId() << "."
              << std::endl;

    return 0;
}


bool CliApplication::parseOptions(
    int argc,
    char* argv[],
    CliOptions& options
) const {
    if (argc == 2 &&
        (std::string(argv[1]) == "-h" ||
         std::string(argv[1]) == "--help")) {
        printUsage(argv[0]);
        return false;
    }

    if (argc != 3 && argc != 5) {
        printUsage(argv[0]);
        return false;
    }

    options.targetPath = argv[1];

    const std::string actionInput = argv[2];
    const std::optional<Action> action = parseAction(actionInput);

    if (!action.has_value()) {
        std::cerr << "Invalid action: " << actionInput << '\n';
        std::cerr << "Expected 'encrypt' or 'decrypt'.\n";
        return false;
    }

    options.action = action.value();

    if (argc == 5) {
        if (std::string(argv[3]) != "--workers") {
            std::cerr << "Expected --workers followed by a number.\n";
            return false;
        }

        try {
            std::size_t consumed = 0;
            const unsigned long count = std::stoul(argv[4], &consumed);

            if (consumed != std::string(argv[4]).size() ||
                count < 1 || count > 64) {
                std::cerr << "Worker count must be between 1 and 64.\n";
                return false;
            }

            options.workerCount = static_cast<std::size_t>(count);
        } catch (const std::exception&) {
            std::cerr << "Invalid worker count: " << argv[4] << '\n';
            return false;
        }
    }

    return true;
}


bool CliApplication::readInteractiveInput(CliOptions& options) const {
    std::string directory;
    std::string actionInput;

    std::cout << "Enter the directory path: ";
    if (!std::getline(std::cin, directory)) {
        std::cerr << "Failed to read directory path." << std::endl;
        return false;
    }

    std::cout << "Enter the action (encrypt/decrypt): ";
    if (!std::getline(std::cin, actionInput)) {
        std::cerr << "Failed to read action." << std::endl;
        return false;
    }

    std::optional<Action> action = parseAction(actionInput);
    if (!action.has_value()) {
        std::cerr << "Invalid action: " << actionInput << std::endl;
        std::cerr << "Expected 'encrypt' or 'decrypt'." << std::endl;
        return false;
    }

    options.targetPath = fs::path(directory);
    options.action = action.value();
    return true;
}


void CliApplication::printUsage(const char* programName) const {
    std::cerr
        << "Usage: " << programName
        << " <directory> <encrypt|decrypt> [--workers N]\n"
        << "N must be between 1 and 64.\n"
        << "Run without arguments for interactive prompts.\n";
}


} // namespace vault::cli
