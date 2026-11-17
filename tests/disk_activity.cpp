#include "../src/dsp/disk_activity.h"
#include <cassert>

int main() {
  const std::string diskstats = "179 0 mmcblk0 20 0 800 0 14 0 1060 0 0\n"
                                "179 4 mmcblk0p4 10 0 200 0 4 0 412 0 0\n";
  DiskCounters counts;
  assert(find_disk_counters(diskstats, 179, 4, counts));
  assert(counts.sectors_read == 200 && counts.sectors_written == 412);
  assert(!find_disk_counters(diskstats, 179, 8, counts));
  assert(!find_disk_counters("179 4 mmcblk0p4 1 0 2\n", 179, 4, counts));
  DiskActivity activity;
  activity.update({200, 412}, 100, 4);
  assert(!activity.available);
  activity.update({204, 420}, 102, 4);
  assert(activity.available && activity.read_bps == 1024 && activity.write_bps == 2048);
  activity.update({204, 420}, 103, 4);
  assert(activity.available && activity.read_bps == 0 && activity.write_bps == 0);
  activity.update({203, 420}, 104, 4);
  assert(!activity.available);
  activity.update({204, 421}, 105, 5);
  assert(!activity.available);
  activity.clear();
  activity.update({205, 430}, 106, 5);
  assert(!activity.available);
}
