#ifndef VAULT_CLI_CLI_APPLICATION_HPP
#define VAULT_CLI_CLI_APPLICATION_HPP

#include "../core/Action.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace vault::cli {

struct CliOptions {
    std::filesystem::path targetPath;
    Action action = Action::Encrypt;
    std::size_t workerCount = 4;
};

class CliApplication {
public:
    int run(int argc, char* argv[]);

private:
    bool parseOptions(int argc, char* argv[], CliOptions& options) const;
    bool readInteractiveInput(CliOptions& options) const;
    void printUsage(const char* programName) const;
};

} // namespace vault::cli

#endif
