#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "core/Utils.h"
#include "profiling/FrameTimingWriter.h"

namespace 
{

int g_failures = 0;

void Check(bool condition, const char* what) 
{
  if (condition)
    return;

  std::fprintf(stderr, "FAIL: %s\n", what);
  ++g_failures;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  using patronus::profiling::FrameTimingWriter;
  using patronus::utils::TicksToMilliseconds;

  const fs::path root = fs::temp_directory_path() / "patronus_frame_timing_writer_test";
  const fs::path file = root / "nested" / "run.csv";
  std::error_code ignored;
  fs::remove_all(root, ignored);

  {
    FrameTimingWriter log;
    Check(!log.IsOpen(), "log starts closed");
    log.Add(7, "sim", 1.0);

    const bool opened = log.Open(file, {{"gpu", "test device"}, {"note", "self-test"}});
    Check(opened, "Open creates missing parent directories and succeeds");
    Check(log.IsOpen(), "IsOpen after Open");

    log.Add(0, "sim", 0.4125);
    log.Add(0, "draw", 1.9);
    log.Add(1, "sim", 0.40001);
    // Destructor closes and flushes; nothing else is written.
  }

  {
    std::ifstream in(file);
    std::stringstream contents;
    contents << in.rdbuf();

    const std::string expected =
      "# gpu=test device\n"
      "# note=self-test\n"
      "frame,zone,ms\n"
      "0,sim,0.4125\n"
      "0,draw,1.9000\n"
      "1,sim,0.4000\n";
    
    if (contents.str() != expected) 
    {
      std::fprintf(stderr, "FAIL: file contents\n--- expected ---\n%s--- got ---\n%s---\n", expected.c_str(),
                   contents.str().c_str());
      ++g_failures;
    }
  }

  Check(TicksToMilliseconds(1000, 501000, 1'000'000'000) == 0.5, "1 GHz conversion");
  Check(TicksToMilliseconds(0, 10, 10'000'000) == 0.001, "10 MHz conversion");
  Check(TicksToMilliseconds(5, 1, 100) == 0.0, "reversed pair is 0, not a wraparound");
  Check(TicksToMilliseconds(0, 1, 0) == 0.0, "zero frequency is 0, not a division by zero");

  {
    // An unopenable path: a file where a directory is needed.
    FrameTimingWriter log;
    const bool opened = log.Open(file / "cannot_be_a_directory" / "x.csv", {});
    Check(!opened, "Open fails cleanly when the path is unusable");
    Check(!log.IsOpen(), "log stays closed after a failed Open");
    log.Add(0, "sim", 1.0);
  }

  fs::remove_all(root, ignored);

  if (g_failures != 0) 
  {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }

  std::puts("FrameTimingWriter test passed.");
  return 0;
}
