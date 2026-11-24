#pragma once
#include "updates.h"
namespace tempos {
std::filesystem::path runningExecutable();
std::string updatePublicKey();
bool installedUpdateTarget(const std::filesystem::path &);
bool beginUpdateInstall(const UpdateState &, const std::filesystem::path &root, std::wstring &error);
int runUpdateHelper(const std::filesystem::path &job, bool recover);
bool resumeUpdate(const std::filesystem::path &root, std::wstring_view token);
bool acknowledgeUpdate(const std::filesystem::path &root, bool healthy);
} // namespace tempos
