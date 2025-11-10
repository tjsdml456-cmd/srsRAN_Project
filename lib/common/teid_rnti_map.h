#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace srsran {

class teid_rnti_map {
public:
  void set(uint32_t teid, uint16_t rnti) {
    std::lock_guard<std::mutex> lock(mtx);
    db[teid] = rnti;
  }

  uint16_t get(uint32_t teid) const {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = db.find(teid);
    return (it != db.end()) ? it->second : 0;
  }

private:
  mutable std::mutex mtx;
  std::unordered_map<uint32_t, uint16_t> db;
};

inline teid_rnti_map global_teid_rnti;

} // namespace srsran
