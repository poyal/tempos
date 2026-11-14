#pragma once
#include "updates.h"
namespace tempos {
std::filesystem::path runningExecutable();
bool installedUpdateTarget(const std::filesystem::path &);
bool beginUpdateInstall(const UpdateState &, const std::filesystem::path &root, std::wstring &error);
int runUpdateHelper(const std::filesystem::path &job);
#ifdef TEMPOS_TEST_HOST
int runMockUpdateInstaller(const std::filesystem::path &job);
#endif
} // namespace tempos
