#ifndef SIGNALSCOPE_DISK_ACTIVITY_H
#define SIGNALSCOPE_DISK_ACTIVITY_H

#include <cstdint>
#include <sstream>
#include <string>

struct DiskCounters {
  uint64_t sectors_read = 0;
  uint64_t sectors_written = 0;
};

inline bool find_disk_counters(const std::string& text, unsigned wanted_major,
                               unsigned wanted_minor, DiskCounters& counters) {
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    unsigned major_id, minor_id;
    std::string name;
    if (!(fields >> major_id >> minor_id >> name) ||
        major_id != wanted_major || minor_id != wanted_minor) continue;
    uint64_t unused;
    return static_cast<bool>(fields >> unused >> unused >> counters.sectors_read
                             >> unused >> unused >> unused >> counters.sectors_written);
  }
  return false;
}

struct DiskActivity {
  double read_bps = 0;
  double write_bps = 0;
  bool available = false;

  void clear() { *this = DiskActivity{}; }

  void update(DiskCounters counters, double now, uint64_t device) {
    const double elapsed = now - baseline_at_;
    // Linux diskstats always counts 512-byte sectors, independent of filesystem block size.
    constexpr double bytes_per_sector = 512;
    if (has_baseline_ && device == baseline_device_ && elapsed > 0 &&
        counters.sectors_read >= baseline_.sectors_read &&
        counters.sectors_written >= baseline_.sectors_written) {
      read_bps = (counters.sectors_read - baseline_.sectors_read) * bytes_per_sector / elapsed;
      write_bps = (counters.sectors_written - baseline_.sectors_written) * bytes_per_sector / elapsed;
      available = true;
    } else {
      available = false;
      read_bps = write_bps = 0;
    }
    baseline_ = counters;
    baseline_at_ = now;
    baseline_device_ = device;
    has_baseline_ = true;
  }

private:
  DiskCounters baseline_{};
  double baseline_at_ = 0;
  uint64_t baseline_device_ = 0;
  bool has_baseline_ = false;
};

#endif
