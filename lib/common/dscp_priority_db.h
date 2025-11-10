

#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace srsran {

class dscp_priority_db {
public:
  void set(uint16_t rnti, uint8_t dscp) {
    std::lock_guard<std::mutex> lock(mtx);
    db[rnti] = dscp;
  }

  uint8_t get(uint16_t rnti) const {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = db.find(rnti);
    return (it != db.end()) ? it->second : 0;
  }

private:
  mutable std::mutex mtx;
  std::unordered_map<uint16_t, uint8_t> db;
};

// 전역 인스턴스
inline dscp_priority_db global_dscp_db;

} // namespace srsran

