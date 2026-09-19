#include "profiling/FrameTimingWriter.h"

namespace patronus::profiling 
{

FrameTimingWriter::~FrameTimingWriter() 
{ 
  Close(); 
}

bool FrameTimingWriter::Open(const std::filesystem::path& path, const Metadata& metadata) 
{
  Close();

  if (path.has_parent_path()) 
  {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
  }

  // Use wide on Windows.
  if (_wfopen_s(&m_file, path.c_str(), L"w") || m_file == nullptr) 
  {
    m_file = nullptr;
    return false;
  }

  for (const auto& [key, value] : metadata) 
  {
    std::fprintf(m_file, "# %s=%s\n", key.c_str(), value.c_str());
  }

  std::fputs("frame,zone,ms\n", m_file);
  return true;
}

void FrameTimingWriter::Add(uint64_t frame, const char* zone, double milliseconds) 
{
  if (m_file == nullptr)
    return;

  std::fprintf(m_file, "%llu,%s,%.4f\n", static_cast<unsigned long long>(frame), zone, milliseconds);
}

void FrameTimingWriter::Close() 
{
  if (m_file == nullptr)
    return;
  
  std::fclose(m_file);
  m_file = nullptr;
}

}  // namespace patronus::profiling