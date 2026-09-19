#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <filesystem>
#include <system_error>

namespace patronus::profiling 
{

class FrameTimingWriter
{
public:
  // Metadata printed above the data in the csv file,
  // e.g. "# resolution=1280x720" or "# config=RelWithDebInfo".
  using Metadata = std::vector<std::pair<std::string, std::string>>;

  FrameTimingWriter() = default;
  ~FrameTimingWriter();
  FrameTimingWriter(const FrameTimingWriter&) = delete;
  FrameTimingWriter& operator=(const FrameTimingWriter&) = delete;

  // Creates or truncates the file and creates the parent directories if necessary.
  bool Open(const std::filesystem::path& path, const Metadata& metadata);

  // Adds on sample: "frame,zone,ms".
  // Requirement: zone cannot contain a comma.
  void Add(uint64_t frame, const char* zone, double milliseconds);
  void Close();

  bool IsOpen() const { return m_file != nullptr; }

private:
  std::FILE* m_file{nullptr};
};

}  // namespace patronus::profiling