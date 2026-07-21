#include "plugin_api_v1.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

const host_api_v1_t* g_host = nullptr;

struct ProcEntry {
  int pid = 0;
  std::string name;
  float cpu = 0.0f;
  float mem = 0.0f;
  long rss_kb = 0;
};

struct WifiNetwork {
  std::string ssid;
  int signal_dbm = -100;
  int freq_mhz = 0;
};

struct Metrics {
  float cpu_pct = 0.0f;
  float cpu_temp_c = 0.0f;
  bool cpu_temp_available = false;
  float load1 = 0.0f;
  float load5 = 0.0f;
  float load15 = 0.0f;
  long mem_total_mb = 0;
  long mem_used_mb = 0;
  long mem_free_mb = 0;
  long mem_available_mb = 0;
  float data_used_pct = 0.0f;
  float data_used_gb = 0.0f;
  float data_free_gb = 0.0f;
  float root_used_pct = 0.0f;
  float root_free_gb = 0.0f;
  long uptime_seconds = 0;
  std::string wifi_ssid = "offline";
  int wifi_signal_dbm = -100;
  long long wifi_rx_bytes = 0;
  long long wifi_tx_bytes = 0;
  long long wifi_rx_packets = 0;
  long long wifi_tx_packets = 0;
  long long wifi_rx_drop = 0;
  long long wifi_tx_drop = 0;
  float wifi_rx_mbps = 0.0f;
  float wifi_tx_mbps = 0.0f;
  std::vector<ProcEntry> top_cpu;
  std::vector<ProcEntry> top_mem;
  std::vector<WifiNetwork> wifi_scan;
};

struct VoiceState {
  float phase = 0.0f;
  float env = 0.0f;
};

struct SonifyVoice {
  std::string label;
  float strength = 0.0f;
};

struct SlottedVoice {
  bool active = false;
  SonifyVoice voice;
};

static std::string trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r'))
    ++start;
  size_t end = s.size();
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r'))
    --end;
  return s.substr(start, end - start);
}

static std::string shell_read(const char* cmd) {
  std::string out;
  FILE* fp = popen(cmd, "r");
  if (!fp)
    return out;
  char buf[512];
  while (fgets(buf, sizeof(buf), fp))
    out += buf;
  pclose(fp);
  return out;
}

static bool read_file(const char* path, std::string& out) {
  FILE* fp = fopen(path, "rb");
  if (!fp)
    return false;
  char buf[1024];
  while (fgets(buf, sizeof(buf), fp))
    out += buf;
  fclose(fp);
  return true;
}

static std::string json_escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) >= 32)
          out += c;
        break;
    }
  }
  return out;
}

class SignalScope {
public:
  SignalScope() {
    refresh_metrics(true);
    refresh_network_list();
  }

  void set_param(const char* key, const char* val) {
    if (strcmp(key, "refresh") == 0) {
      refresh_metrics(true);
      return;
    }
    if (strcmp(key, "refresh_light") == 0) {
      refresh_metrics(false);
      return;
    }
    if (strcmp(key, "scan_wifi") == 0) {
      refresh_metrics(false);
      refresh_network_list();
      return;
    }
    if (strcmp(key, "auto_refresh") == 0) {
      auto_refresh_ = atoi(val) != 0;
      return;
    }
    if (strcmp(key, "sonify_enable") == 0) {
      sonify_enable_ = atoi(val) != 0;
      return;
    }
    if (strcmp(key, "sonify_gain") == 0) {
      sonify_gain_ = std::max(0.0f, std::min(2.0f, static_cast<float>(atof(val))));
      return;
    }
    if (strcmp(key, "sonify_root") == 0) {
      sonify_root_ = std::max(0, std::min(11, atoi(val)));
      return;
    }
    if (strcmp(key, "sonify_rate") == 0) {
      sonify_rate_index_ = std::max(0, std::min(5, atoi(val)));
      return;
    }
    if (strcmp(key, "sonify_decay") == 0) {
      sonify_decay_ms_ = std::max(50.0f, std::min(4000.0f, static_cast<float>(atof(val))));
      return;
    }
    if (strcmp(key, "sonify_waveform") == 0) {
      sonify_waveform_ = std::max(0, std::min(3, atoi(val)));
      return;
    }
    if (strcmp(key, "sonify_scale") == 0) {
      sonify_scale_ = std::max(0, std::min(3, atoi(val)));
      return;
    }
    if (strcmp(key, "sonify_source") == 0) {
      sonify_source_ = std::max(0, std::min(1, atoi(val)));
      return;
    }
    if (strcmp(key, "sonify_pan_1") == 0) { voice_pan_[0] = clamp_pan(val); return; }
    if (strcmp(key, "sonify_pan_2") == 0) { voice_pan_[1] = clamp_pan(val); return; }
    if (strcmp(key, "sonify_pan_3") == 0) { voice_pan_[2] = clamp_pan(val); return; }
    if (strcmp(key, "sonify_pan_4") == 0) { voice_pan_[3] = clamp_pan(val); return; }
  }

  int get_param(const char* key, char* buf, int buf_len) {
    maybe_auto_refresh();
    if (strcmp(key, "auto_refresh") == 0)
      return snprintf(buf, buf_len, "%d", auto_refresh_ ? 1 : 0);
    if (strcmp(key, "sonify_enable") == 0)
      return snprintf(buf, buf_len, "%d", sonify_enable_ ? 1 : 0);
    if (strcmp(key, "sonify_gain") == 0)
      return snprintf(buf, buf_len, "%.3f", sonify_gain_);
    if (strcmp(key, "sonify_root") == 0)
      return snprintf(buf, buf_len, "%d", sonify_root_);
    if (strcmp(key, "sonify_rate") == 0)
      return snprintf(buf, buf_len, "%d", sonify_rate_index_);
    if (strcmp(key, "sonify_decay") == 0)
      return snprintf(buf, buf_len, "%.1f", sonify_decay_ms_);
    if (strcmp(key, "sonify_waveform") == 0)
      return snprintf(buf, buf_len, "%d", sonify_waveform_);
    if (strcmp(key, "sonify_scale") == 0)
      return snprintf(buf, buf_len, "%d", sonify_scale_);
    if (strcmp(key, "sonify_source") == 0)
      return snprintf(buf, buf_len, "%d", sonify_source_);
    if (strcmp(key, "sonify_pan_1") == 0)
      return snprintf(buf, buf_len, "%.3f", voice_pan_[0]);
    if (strcmp(key, "sonify_pan_2") == 0)
      return snprintf(buf, buf_len, "%.3f", voice_pan_[1]);
    if (strcmp(key, "sonify_pan_3") == 0)
      return snprintf(buf, buf_len, "%.3f", voice_pan_[2]);
    if (strcmp(key, "sonify_pan_4") == 0)
      return snprintf(buf, buf_len, "%.3f", voice_pan_[3]);
    if (strcmp(key, "overview_json") == 0)
      return write_string(buf, buf_len, overview_json());
    if (strcmp(key, "top_cpu_json") == 0)
      return write_string(buf, buf_len, procs_json(metrics_.top_cpu, false));
    if (strcmp(key, "top_mem_json") == 0)
      return write_string(buf, buf_len, procs_json(metrics_.top_mem, true));
    if (strcmp(key, "wifi_scan_json") == 0)
      return write_string(buf, buf_len, wifi_scan_json());
    if (strcmp(key, "sonify_status") == 0)
      return write_string(buf, buf_len, sonify_status_json());
    return -1;
  }

  void render_block(int16_t* out, int frames) {
    if (!out || frames <= 0)
      return;
    const auto slotted = get_slotted_voices();
    int voices = 0;
    for (int slot = 0; slot < 4; ++slot) {
      if (slotted[slot].active)
        ++voices;
    }
    if (!sonify_enable_ || voices == 0 || sonify_gain_ <= 0.0001f) {
      std::memset(out, 0, sizeof(int16_t) * frames * 2);
      return;
    }

    update_tempo();
    const float samples_per_trigger = trigger_samples_for_rate();
    for (int i = 0; i < frames; ++i) {
      if (++samples_until_trigger_ >= samples_per_trigger) {
        samples_until_trigger_ = 0.0f;
        for (int slot = 0; slot < 4; ++slot) {
          if (!slotted[slot].active)
            continue;
          voice_states_[slot].env = 1.0f;
          voice_states_[slot].phase = 0.0f;
        }
      }
      float mix_l = 0.0f;
      float mix_r = 0.0f;
      for (int slot = 0; slot < 4; ++slot) {
        if (!slotted[slot].active)
          continue;
        const SonifyVoice& voice = slotted[slot].voice;
        const float norm = std::max(0.0f, std::min(1.0f, voice.strength));
        const int octave = static_cast<int>(std::round(norm * 2.0f));
        const auto& tones = current_scale_tones();
        const int tone = tones[slot % tones.size()];
        const int midi = 48 + octave * 12 + ((sonify_root_ + tone) % 12);
        const float freq = 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
        VoiceState& state = voice_states_[slot];
        const float amp = (0.16f + 0.12f * norm) * sonify_gain_ * state.env;
        state.phase += (2.0f * 3.1415926535f * freq) / MOVE_SAMPLE_RATE;
        if (state.phase > 2.0f * 3.1415926535f)
          state.phase -= 2.0f * 3.1415926535f;
        const float s = render_wave(state.phase) * amp;
        const float pan = voice_pan_[slot];
        const float left_gain = std::sqrt(0.5f * (1.0f - pan));
        const float right_gain = std::sqrt(0.5f * (1.0f + pan));
        mix_l += s * left_gain;
        mix_r += s * right_gain;
        state.env *= decay_coefficient();
      }
      mix_l /= std::max(1, voices);
      mix_r /= std::max(1, voices);
      mix_l = std::max(-0.95f, std::min(0.95f, mix_l));
      mix_r = std::max(-0.95f, std::min(0.95f, mix_r));
      out[i * 2] = static_cast<int16_t>(std::lround(mix_l * 32767.0f));
      out[i * 2 + 1] = static_cast<int16_t>(std::lround(mix_r * 32767.0f));
    }
  }

private:
  int write_string(char* buf, int buf_len, const std::string& s) {
    if (static_cast<int>(s.size()) >= buf_len)
      return -1;
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return static_cast<int>(s.size());
  }

  void maybe_auto_refresh() {
    if (!auto_refresh_)
      return;
    auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh_).count();
    if (elapsed >= 1000)
      refresh_metrics(false);
  }

  void refresh_metrics(bool full) {
    read_cpu();
    read_cpu_temperature();
    read_loadavg();
    read_mem();
    read_uptime();
    read_netdev();
    read_wifi_link();
    if (full || top_cpu_refresh_due()) {
      read_top_processes();
      last_top_refresh_ = std::chrono::steady_clock::now();
    }
    if (full || disk_refresh_due()) {
      read_df();
      last_disk_refresh_ = std::chrono::steady_clock::now();
    }
    last_refresh_ = std::chrono::steady_clock::now();
  }

  bool top_cpu_refresh_due() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - last_top_refresh_).count() >= 4000;
  }

  bool disk_refresh_due() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - last_disk_refresh_).count() >= 15000;
  }

  void read_cpu() {
    std::string data;
    if (!read_file("/proc/stat", data))
      return;
    std::istringstream iss(data);
    std::string cpu_label;
    long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    const long long idle_all = idle + iowait;
    const long long non_idle = user + nice + system + irq + softirq + steal;
    const long long total = idle_all + non_idle;
    if (last_cpu_total_ > 0 && total > last_cpu_total_) {
      const long long totald = total - last_cpu_total_;
      const long long idled = idle_all - last_cpu_idle_;
      metrics_.cpu_pct = totald > 0 ? (100.0f * static_cast<float>(totald - idled) / static_cast<float>(totald)) : 0.0f;
    }
    last_cpu_total_ = total;
    last_cpu_idle_ = idle_all;
  }

  void read_cpu_temperature() {
    std::string data;
    metrics_.cpu_temp_available = false;
    if (!read_file("/run/signalscope-cpu-temp", data))
      return;

    const size_t equals = data.find('=');
    const char* value = data.c_str() + (equals == std::string::npos ? 0 : equals + 1);
    char* end = nullptr;
    const float temperature = std::strtof(value, &end);
    if (end != value && std::isfinite(temperature)) {
      metrics_.cpu_temp_c = temperature;
      metrics_.cpu_temp_available = true;
    }
  }

  void read_loadavg() {
    std::string data;
    if (!read_file("/proc/loadavg", data))
      return;
    std::istringstream iss(data);
    iss >> metrics_.load1 >> metrics_.load5 >> metrics_.load15;
  }

  void read_mem() {
    std::string data;
    if (!read_file("/proc/meminfo", data))
      return;
    long total_kb = 0, free_kb = 0, available_kb = 0, buffers_kb = 0, cached_kb = 0;
    std::istringstream iss(data);
    std::string key;
    long value;
    std::string unit;
    while (iss >> key >> value >> unit) {
      if (key == "MemTotal:")
        total_kb = value;
      else if (key == "MemFree:")
        free_kb = value;
      else if (key == "MemAvailable:")
        available_kb = value;
      else if (key == "Buffers:")
        buffers_kb = value;
      else if (key == "Cached:")
        cached_kb = value;
    }
    const long used_kb = total_kb - free_kb - buffers_kb - cached_kb;
    metrics_.mem_total_mb = total_kb / 1024;
    metrics_.mem_used_mb = std::max(0L, used_kb / 1024);
    metrics_.mem_free_mb = free_kb / 1024;
    metrics_.mem_available_mb = available_kb / 1024;
  }

  void read_uptime() {
    std::string data;
    if (!read_file("/proc/uptime", data))
      return;
    metrics_.uptime_seconds = static_cast<long>(std::atof(data.c_str()));
  }

  void read_df() {
    const std::string data = shell_read("df -k /data / 2>/dev/null");
    std::istringstream iss(data);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
      line = trim(line);
      if (line.empty())
        continue;
      if (row++ == 0)
        continue;
      std::istringstream ls(line);
      std::string fs, mount;
      long blocks = 0, used = 0, avail = 0;
      std::string usepct;
      ls >> fs >> blocks >> used >> avail >> usepct >> mount;
      if (mount == "/data") {
        metrics_.data_used_pct = blocks > 0 ? (100.0f * used / blocks) : 0.0f;
        metrics_.data_used_gb = used / (1024.0f * 1024.0f);
        metrics_.data_free_gb = avail / (1024.0f * 1024.0f);
      } else if (mount == "/") {
        metrics_.root_used_pct = blocks > 0 ? (100.0f * used / blocks) : 0.0f;
        metrics_.root_free_gb = avail / (1024.0f * 1024.0f);
      }
    }
  }

  void read_netdev() {
    std::string data;
    if (!read_file("/proc/net/dev", data))
      return;
    std::istringstream iss(data);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
      if (row++ < 2)
        continue;
      if (line.find("wlan0:") == std::string::npos)
        continue;
      const size_t colon = line.find(':');
      if (colon == std::string::npos)
        continue;
      std::istringstream ls(line.substr(colon + 1));
      long long rx_bytes = 0, rx_packets = 0, rx_errs = 0, rx_drop = 0;
      long long tx_bytes = 0, tx_packets = 0, tx_errs = 0, tx_drop = 0;
      long long skip = 0;
      ls >> rx_bytes >> rx_packets >> rx_errs >> rx_drop;
      for (int i = 0; i < 4; ++i) ls >> skip;
      ls >> tx_bytes >> tx_packets >> tx_errs >> tx_drop;
      metrics_.wifi_rx_bytes = rx_bytes;
      metrics_.wifi_tx_bytes = tx_bytes;
      metrics_.wifi_rx_packets = rx_packets;
      metrics_.wifi_tx_packets = tx_packets;
      metrics_.wifi_rx_drop = rx_drop;
      metrics_.wifi_tx_drop = tx_drop;
      return;
    }
  }

  void read_wifi_link() {
    metrics_.wifi_ssid = "offline";
    metrics_.wifi_signal_dbm = -100;
    metrics_.wifi_rx_mbps = 0.0f;
    metrics_.wifi_tx_mbps = 0.0f;
    const std::string data = shell_read("iw wlan0 link 2>/dev/null");
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
      const std::string t = trim(line);
      if (t.find("SSID:") == 0)
        metrics_.wifi_ssid = trim(t.substr(5));
      else if (t.find("signal:") == 0)
        metrics_.wifi_signal_dbm = std::atoi(trim(t.substr(7)).c_str());
      else if (t.find("rx bitrate:") == 0)
        metrics_.wifi_rx_mbps = static_cast<float>(std::atof(trim(t.substr(11)).c_str()));
      else if (t.find("tx bitrate:") == 0)
        metrics_.wifi_tx_mbps = static_cast<float>(std::atof(trim(t.substr(11)).c_str()));
    }
  }

  void read_top_processes() {
    metrics_.top_cpu = parse_proc_lines(shell_read("ps -eo pid,comm,%cpu,%mem --sort=-%cpu | head -n 6 2>/dev/null"), false);
    metrics_.top_mem = parse_proc_lines(shell_read("ps -eo pid,comm,%cpu,rss --sort=-rss | head -n 6 2>/dev/null"), true);
  }

  std::vector<ProcEntry> parse_proc_lines(const std::string& data, bool mem_rss) {
    std::vector<ProcEntry> out;
    std::istringstream iss(data);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
      line = trim(line);
      if (line.empty())
        continue;
      if (row++ == 0)
        continue;
      ProcEntry p;
      std::istringstream ls(line);
      if (mem_rss) {
        ls >> p.pid >> p.name >> p.cpu >> p.rss_kb;
        p.mem = p.rss_kb / 1024.0f;
      } else {
        ls >> p.pid >> p.name >> p.cpu >> p.mem;
      }
      if (p.pid > 0)
        out.push_back(p);
    }
    return out;
  }

  void parse_wifi_scan_text(const std::string& data) {
    std::vector<WifiNetwork> parsed;
    std::istringstream iss(data);
    std::string line;
    WifiNetwork current;
    bool in_block = false;
    while (std::getline(iss, line)) {
      const std::string t = trim(line);
      if (t.find("BSS ") == 0) {
        if (in_block && !current.ssid.empty())
          parsed.push_back(current);
        current = WifiNetwork();
        in_block = true;
      } else if (t.find("SSID:") == 0) {
        current.ssid = trim(t.substr(5));
      } else if (t.find("signal:") == 0) {
        current.signal_dbm = static_cast<int>(std::round(std::atof(trim(t.substr(7)).c_str())));
      } else if (t.find("freq:") == 0) {
        current.freq_mhz = std::atoi(trim(t.substr(5)).c_str());
      }
    }
    if (in_block && !current.ssid.empty())
      parsed.push_back(current);

    std::sort(parsed.begin(), parsed.end(),
              [](const WifiNetwork& a, const WifiNetwork& b) { return a.signal_dbm > b.signal_dbm; });
    if (parsed.size() > 8)
      parsed.resize(8);
    metrics_.wifi_scan = std::move(parsed);
  }

  void refresh_network_list() {
    const std::string connman = shell_read("connmanctl services 2>/dev/null");
    std::vector<WifiNetwork> parsed;
    if (!connman.empty()) {
      std::istringstream iss(connman);
      std::string line;
      int idx = 0;
      while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty())
          continue;
        const size_t wifi_pos = line.find(" wifi_");
        if (wifi_pos == std::string::npos)
          continue;
        std::string name = trim(line.substr(0, wifi_pos));
        while (!name.empty() && (name[0] == '*' || name[0] == 'A' || name[0] == 'R'))
          name = trim(name.substr(1));
        if (name.empty())
          continue;
        WifiNetwork net;
        net.ssid = name;
        net.signal_dbm = -38 - idx * 9;
        net.freq_mhz = 0;
        parsed.push_back(net);
        ++idx;
      }
    }
    if (parsed.empty()) {
      const std::string data = shell_read("iw dev wlan0 scan 2>/dev/null");
      if (!data.empty())
        parse_wifi_scan_text(data);
      return;
    }
    if (parsed.size() > 8)
      parsed.resize(8);
    metrics_.wifi_scan = std::move(parsed);
  }

  float strength_to_norm(int dbm) const {
    const float clamped = std::max(-95.0f, std::min(-35.0f, static_cast<float>(dbm)));
    return (clamped + 95.0f) / 60.0f;
  }

  float clamp_pan(const char* val) const {
    return std::max(-1.0f, std::min(1.0f, static_cast<float>(atof(val))));
  }

  void update_tempo() {
    if (!g_host || !g_host->get_bpm)
      return;
    const float bpm = g_host->get_bpm();
    if (bpm > 20.0f && bpm < 400.0f)
      bpm_ = bpm;
  }

  float trigger_samples_for_rate() const {
    const float quarter = (60.0f / std::max(1.0f, bpm_)) * MOVE_SAMPLE_RATE;
    switch (sonify_rate_index_) {
      case 0: return quarter * 4.0f;   // whole
      case 1: return quarter * 2.0f;   // half
      case 2: return quarter;          // quarter
      case 3: return quarter * 0.5f;   // eighth
      case 4: return quarter * 0.25f;  // sixteenth
      case 5: return quarter * 0.125f; // thirty-second
      default: return quarter;
    }
  }

  float render_wave(float phase) const {
    switch (sonify_waveform_) {
      case 1: {
        const float wrapped = phase / (2.0f * 3.1415926535f);
        const float tri = 2.0f * std::fabs(2.0f * (wrapped - std::floor(wrapped + 0.5f))) - 1.0f;
        return tri;
      }
      case 2:
        return std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
      case 3: {
        const float wrapped = phase / (2.0f * 3.1415926535f);
        return 2.0f * (wrapped - std::floor(wrapped + 0.5f));
      }
      default:
        return std::sin(phase);
    }
  }

  const std::array<int, 4>& current_scale_tones() const {
    switch (sonify_scale_) {
      case 1: return minor7_tones_;
      case 2: return suspended_tones_;
      case 3: return pentatonic_tones_;
      default: return major7_tones_;
    }
  }

  float decay_coefficient() const {
    const float decay_seconds = std::max(0.05f, sonify_decay_ms_ / 1000.0f);
    return std::exp(std::log(0.001f) / (MOVE_SAMPLE_RATE * decay_seconds));
  }

  std::vector<SonifyVoice> get_sonify_voices() {
    std::vector<SonifyVoice> out;
    if (sonify_source_ == 1) {
      const int limit = std::min<int>(4, metrics_.top_cpu.size());
      for (int i = 0; i < limit; ++i) {
        SonifyVoice v;
        v.label = metrics_.top_cpu[i].name;
        v.strength = std::max(0.05f, std::min(1.0f, metrics_.top_cpu[i].cpu / 100.0f));
        out.push_back(v);
      }
      std::stable_sort(out.begin(), out.end(),
                       [](const SonifyVoice& a, const SonifyVoice& b) { return a.strength < b.strength; });
      return out;
    }

    if (!metrics_.wifi_scan.empty()) {
      const int limit = std::min<int>(4, metrics_.wifi_scan.size());
      for (int i = 0; i < limit; ++i) {
        SonifyVoice v;
        v.label = metrics_.wifi_scan[i].ssid;
        v.strength = strength_to_norm(metrics_.wifi_scan[i].signal_dbm);
        out.push_back(v);
      }
      std::stable_sort(out.begin(), out.end(),
                       [](const SonifyVoice& a, const SonifyVoice& b) { return a.strength < b.strength; });
      return out;
    }

    if (metrics_.wifi_ssid != "offline" && !metrics_.wifi_ssid.empty()) {
      SonifyVoice v;
      v.label = metrics_.wifi_ssid;
      v.strength = strength_to_norm(metrics_.wifi_signal_dbm);
      out.push_back(v);
    }
    return out;
  }

  std::array<SlottedVoice, 4> get_slotted_voices() {
    std::array<SlottedVoice, 4> slots{};
    auto voices = get_sonify_voices();
    auto& slot_labels = current_slot_labels();
    std::array<bool, 4> used_voice{};
    std::array<bool, 4> used_slot{};

    for (int slot = 0; slot < 4; ++slot) {
      if (slot_labels[slot].empty())
        continue;
      for (size_t i = 0; i < voices.size(); ++i) {
        if (used_voice[i])
          continue;
        if (voices[i].label == slot_labels[slot]) {
          slots[slot].active = true;
          slots[slot].voice = voices[i];
          used_voice[i] = true;
          used_slot[slot] = true;
          break;
        }
      }
    }

    std::vector<int> free_slots;
    std::vector<SonifyVoice> remaining;
    for (int slot = 0; slot < 4; ++slot) {
      if (!used_slot[slot])
        free_slots.push_back(slot);
    }
    for (size_t i = 0; i < voices.size(); ++i) {
      if (!used_voice[i])
        remaining.push_back(voices[i]);
    }

    const int assign_count = std::min<int>(static_cast<int>(remaining.size()), static_cast<int>(free_slots.size()));
    const int slot_start = static_cast<int>(free_slots.size()) - assign_count;
    for (int i = 0; i < assign_count; ++i) {
      const int slot = free_slots[slot_start + i];
      slots[slot].active = true;
      slots[slot].voice = remaining[i];
      used_slot[slot] = true;
    }

    for (int slot = 0; slot < 4; ++slot) {
      slot_labels[slot] = slots[slot].active ? slots[slot].voice.label : std::string();
    }
    return slots;
  }

  std::array<std::string, 4>& current_slot_labels() {
    return sonify_source_ == 1 ? proc_slot_labels_ : wifi_slot_labels_;
  }

  std::string procs_json(const std::vector<ProcEntry>& items, bool mem_rss) const {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < items.size(); ++i) {
      if (i) os << ",";
      os << "{\"pid\":" << items[i].pid
         << ",\"name\":\"" << json_escape(items[i].name) << "\""
         << ",\"cpu\":" << items[i].cpu;
      if (mem_rss)
        os << ",\"mem_mb\":" << items[i].mem;
      else
        os << ",\"mem_pct\":" << items[i].mem;
      os << "}";
    }
    os << "]";
    return os.str();
  }

  std::string wifi_scan_json() const {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < metrics_.wifi_scan.size(); ++i) {
      if (i) os << ",";
      os << "{\"ssid\":\"" << json_escape(metrics_.wifi_scan[i].ssid) << "\""
         << ",\"signal_dbm\":" << metrics_.wifi_scan[i].signal_dbm
         << ",\"freq_mhz\":" << metrics_.wifi_scan[i].freq_mhz
         << "}";
    }
    os << "]";
    return os.str();
  }

  std::string overview_json() const {
    std::ostringstream os;
    os << "{"
       << "\"cpu_pct\":" << metrics_.cpu_pct
       << ",\"cpu_temp_c\":" << metrics_.cpu_temp_c
       << ",\"cpu_temp_available\":" << (metrics_.cpu_temp_available ? "true" : "false")
       << ",\"load1\":" << metrics_.load1
       << ",\"load5\":" << metrics_.load5
       << ",\"load15\":" << metrics_.load15
       << ",\"mem_total_mb\":" << metrics_.mem_total_mb
       << ",\"mem_used_mb\":" << metrics_.mem_used_mb
       << ",\"mem_free_mb\":" << metrics_.mem_free_mb
       << ",\"mem_available_mb\":" << metrics_.mem_available_mb
       << ",\"data_used_pct\":" << metrics_.data_used_pct
       << ",\"data_used_gb\":" << metrics_.data_used_gb
       << ",\"data_free_gb\":" << metrics_.data_free_gb
       << ",\"root_used_pct\":" << metrics_.root_used_pct
       << ",\"root_free_gb\":" << metrics_.root_free_gb
       << ",\"uptime_seconds\":" << metrics_.uptime_seconds
       << ",\"wifi_ssid\":\"" << json_escape(metrics_.wifi_ssid) << "\""
       << ",\"wifi_signal_dbm\":" << metrics_.wifi_signal_dbm
       << ",\"wifi_rx_bytes\":" << metrics_.wifi_rx_bytes
       << ",\"wifi_tx_bytes\":" << metrics_.wifi_tx_bytes
       << ",\"wifi_rx_packets\":" << metrics_.wifi_rx_packets
       << ",\"wifi_tx_packets\":" << metrics_.wifi_tx_packets
       << ",\"wifi_rx_drop\":" << metrics_.wifi_rx_drop
       << ",\"wifi_tx_drop\":" << metrics_.wifi_tx_drop
       << ",\"wifi_rx_mbps\":" << metrics_.wifi_rx_mbps
       << ",\"wifi_tx_mbps\":" << metrics_.wifi_tx_mbps
       << "}";
    return os.str();
  }

  std::string sonify_status_json() const {
    size_t voice_count = 0;
    if (sonify_source_ == 1) {
      voice_count = std::min<size_t>(4, metrics_.top_cpu.size());
    } else {
      voice_count = !metrics_.wifi_scan.empty()
        ? std::min<size_t>(4, metrics_.wifi_scan.size())
        : ((metrics_.wifi_ssid != "offline" && !metrics_.wifi_ssid.empty()) ? 1 : 0);
    }
    std::ostringstream os;
    os << "{"
       << "\"enabled\":" << (sonify_enable_ ? 1 : 0)
       << ",\"gain\":" << sonify_gain_
       << ",\"root\":" << sonify_root_
       << ",\"rate\":" << sonify_rate_index_
       << ",\"decay\":" << sonify_decay_ms_
       << ",\"waveform\":" << sonify_waveform_
       << ",\"scale\":" << sonify_scale_
       << ",\"source\":" << sonify_source_
        << ",\"auto_refresh\":" << (auto_refresh_ ? 1 : 0)
        << ",\"voices\":" << voice_count
        << "}";
    return os.str();
  }

  Metrics metrics_;
  bool auto_refresh_ = false;
  bool sonify_enable_ = false;
  float sonify_gain_ = 0.90f;
  int sonify_root_ = 0;
  int sonify_rate_index_ = 0;
  float sonify_decay_ms_ = 900.0f;
  int sonify_waveform_ = 0;
  int sonify_scale_ = 0;
  int sonify_source_ = 0;
  float bpm_ = 120.0f;
  float samples_until_trigger_ = 0.0f;
  long long last_cpu_total_ = 0;
  long long last_cpu_idle_ = 0;
  std::chrono::steady_clock::time_point last_refresh_{};
  std::chrono::steady_clock::time_point last_top_refresh_{};
  std::chrono::steady_clock::time_point last_disk_refresh_{};
  std::array<VoiceState, 8> voice_states_{};
  std::array<float, 4> voice_pan_{{-0.75f, -0.25f, 0.25f, 0.75f}};
  std::array<std::string, 4> wifi_slot_labels_{};
  std::array<std::string, 4> proc_slot_labels_{};
  const std::array<int, 4> major7_tones_{{0, 4, 7, 11}};
  const std::array<int, 4> minor7_tones_{{0, 3, 7, 10}};
  const std::array<int, 4> suspended_tones_{{0, 5, 7, 10}};
  const std::array<int, 4> pentatonic_tones_{{0, 2, 7, 9}};
};

struct instance_t {
  SignalScope scope;
  std::string error;
};

void* create_instance(const char* module_dir, const char* json_defaults) {
  (void)module_dir;
  (void)json_defaults;
  return new instance_t();
}

void destroy_instance(void* opaque) {
  delete static_cast<instance_t*>(opaque);
}

void on_midi(void* opaque, const uint8_t* msg, int len, int source) {
  (void)opaque;
  (void)msg;
  (void)len;
  (void)source;
}

void set_param(void* opaque, const char* key, const char* val) {
  instance_t* inst = static_cast<instance_t*>(opaque);
  if (!inst || !key || !val)
    return;
  inst->scope.set_param(key, val);
}

int get_param(void* opaque, const char* key, char* buf, int buf_len) {
  instance_t* inst = static_cast<instance_t*>(opaque);
  if (!inst || !key || !buf || buf_len <= 0)
    return -1;
  return inst->scope.get_param(key, buf, buf_len);
}

int get_error(void* opaque, char* buf, int buf_len) {
  instance_t* inst = static_cast<instance_t*>(opaque);
  if (!inst || !buf || buf_len <= 0 || inst->error.empty())
    return 0;
  std::snprintf(buf, buf_len, "%s", inst->error.c_str());
  return static_cast<int>(inst->error.size());
}

void render_block(void* opaque, int16_t* out_interleaved_lr, int frames) {
  instance_t* inst = static_cast<instance_t*>(opaque);
  if (!inst || !out_interleaved_lr || frames <= 0) {
    if (out_interleaved_lr && frames > 0)
      std::memset(out_interleaved_lr, 0, sizeof(int16_t) * frames * 2);
    return;
  }
  inst->scope.render_block(out_interleaved_lr, frames);
}

plugin_api_v2_t g_plugin_api = {
  MOVE_PLUGIN_API_VERSION_2,
  create_instance,
  destroy_instance,
  on_midi,
  set_param,
  get_param,
  get_error,
  render_block,
};

}  // namespace

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t* host) {
  g_host = host;
  return &g_plugin_api;
}
