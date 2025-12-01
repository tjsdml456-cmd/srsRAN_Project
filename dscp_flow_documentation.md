# DSCP 추출 및 MAC 스케줄러 전달 과정

이 문서는 GTP-U에서 DSCP를 추출하여 MAC 스케줄러까지 전달되는 전체 과정을 코드와 함께 설명합니다.

## 개요

DSCP (Differentiated Services Code Point)는 IP 패킷의 QoS를 나타내는 6비트 값입니다. srsRAN에서는 GTP-U에서 수신한 IP 패킷에서 DSCP를 추출하여 각 레이어를 거쳐 MAC 스케줄러까지 전달하여 우선순위 기반 스케줄링에 활용합니다.

## 전체 데이터 흐름

```
GTP-U → SDAP → PDCP → RLC → MAC → Scheduler
  ↓       ↓      ↓      ↓     ↓        ↓
DSCP    DSCP   DSCP   DSCP  DSCP    Priority
추출    전달   전달   전달  전달    계산
```

---

## 1. GTP-U 레이어: DSCP 추출

### 1.1 DSCP 추출 함수

**파일**: `lib/ran/qos/dscp_qos_mapping.cpp`

```cpp
std::optional<dscp_value_t> srsran::extract_dscp_from_ip_packet(const byte_buffer& pdu)
{
  static auto& logger = srslog::fetch_basic_logger("DSCP-QOS");
  
  if (pdu.empty()) {
    return std::nullopt;
  }

  // Get first byte to determine IP version
  uint8_t first_byte = *pdu.begin();
  uint8_t version    = (first_byte >> 4) & 0x0F;

  if (version == IPV4_VERSION) {
    // IPv4: DSCP is in bits 2-7 of the second byte (TOS field)
    if (pdu.length() < IPV4_MIN_HEADER_LEN) {
      return std::nullopt;
    }
    auto it = pdu.begin();
    ++it; // Skip version/IHL byte
    uint8_t tos = *it;
    // DSCP is the upper 6 bits of TOS
    uint8_t dscp_val = tos >> 2;
    logger.debug("DSCP extracted from IPv4 packet: DSCP={} TOS={:#x} pdu_len={}", 
                 dscp_val, tos, pdu.length());
    return dscp_value_t{dscp_val};
  } else if (version == IPV6_VERSION) {
    // IPv6: DSCP is in bits 4-9 of the first 2 bytes (Traffic Class field)
    if (pdu.length() < IPV6_HEADER_LEN) {
      return std::nullopt;
    }
    auto    it         = pdu.begin();
    uint8_t first      = *it;
    ++it;
    uint8_t second     = *it;
    // Traffic class = bits 4-11 of the first 32 bits
    uint8_t traffic_class = ((first & 0x0F) << 4) | ((second >> 4) & 0x0F);
    // DSCP is the upper 6 bits of traffic class
    uint8_t dscp_val = traffic_class >> 2;
    logger.debug("DSCP extracted from IPv6 packet: DSCP={} TC={:#x} pdu_len={}", 
                 dscp_val, traffic_class, pdu.length());
    return dscp_value_t{dscp_val};
  }

  logger.debug("Unknown IP version: {} pdu_len={}", version, pdu.length());
  return std::nullopt;
}
```

### 1.2 GTP-U RX에서 DSCP 추출 및 전달

**파일**: `lib/gtpu/gtpu_tunnel_ngu_rx_impl.h`

```cpp
void deliver_sdu(gtpu_rx_sdu_info& sdu_info)
{
  // DSCP가 아직 추출되지 않은 경우 IP 패킷에서 추출
  if (!sdu_info.dscp.has_value()) {
    sdu_info.dscp = extract_dscp_from_ip_packet(sdu_info.sdu);
  }
  if (sdu_info.dscp.has_value()) {
    const uint8_t dscp = sdu_info.dscp->to_uint();
    logger.log_info("DSCP={} detected, QFI={} (unchanged)", dscp, sdu_info.qos_flow_id);    
  }
        
  logger.log_info(sdu_info.sdu.begin(),
                  sdu_info.sdu.end(),
                  "RX SDU. sdu_len={} qos_flow={} sn={}",
                  sdu_info.sdu.length(),
                  sdu_info.qos_flow_id,
                  sdu_info.sn);
  // SDAP 레이어로 DSCP와 함께 SDU 전달
  lower_dn.on_new_sdu(std::move(sdu_info.sdu), sdu_info.qos_flow_id, sdu_info.dscp);    
}
```

**주요 포인트**:
- `gtpu_rx_sdu_info` 구조체에 `std::optional<dscp_value_t> dscp` 필드 포함
- IP 패킷에서 DSCP를 추출하여 SDAP로 전달

---

## 2. SDAP 레이어: DSCP 전달

**파일**: `lib/sdap/sdap_entity_tx_impl.h`

```cpp
void handle_sdu(byte_buffer sdu, std::optional<dscp_value_t> dscp)  
{
  if (dscp.has_value()) {
    logger.log_debug("TX PDU. {} pdu_len={} dscp={}", qfi, sdu.length(), dscp->to_uint());
  } else {
    logger.log_debug("TX PDU. {} pdu_len={} dscp=none", qfi, sdu.length());
  }
  // PDCP 레이어로 DSCP와 함께 PDU 전달
  pdu_notifier.on_new_pdu(std::move(sdu), dscp);  
}
```

**주요 포인트**:
- SDAP는 DSCP를 변경하지 않고 그대로 PDCP로 전달

---

## 3. PDCP 레이어: DSCP 전달

**파일**: `lib/pdcp/pdcp_entity_tx.cpp`

### 3.1 SDU 수신 및 처리

```cpp
void pdcp_entity_tx::handle_sdu(byte_buffer buf, std::optional<dscp_value_t> dscp)
{
  metrics.add_sdus(1, buf.length());
  if (dscp.has_value()) {
    logger.log_debug(buf.begin(), buf.end(), "TX SDU. sdu_len={} dscp={}", 
                    buf.length(), dscp->to_uint());
  } else {
    logger.log_debug(buf.begin(), buf.end(), "TX SDU. sdu_len={} dscp=none", 
                     buf.length());
  }

  // ... (보안 처리 등)

  /// Prepare buffer info struct to pass to crypto executor.
  pdcp_tx_buffer_info buf_info{.is_retx = false,
                               .retx_id = retransmit_id,
                               .count   = st.tx_next,
                               .buf     = std::move(buf),
                               .dscp    = dscp,  // DSCP를 버퍼 정보에 포함
                               .token   = pdcp_crypto_token(token_mngr)};

  // ... (암호화 처리 후 RLC로 전달)
}
```

### 3.2 RLC로 PDU 전달

```cpp
void pdcp_entity_tx::write_data_pdu_to_lower_layers(pdcp_tx_pdu_info&& pdu_info, bool is_retx)
{
  logger.log_info(pdu_info.pdu.begin(),
                  pdu_info.pdu.end(),
                  "TX PDU. type=data pdu_len={} sn={} count={} is_retx={}",
                  pdu_info.pdu.length(),
                  SN(pdu_info.count),
                  pdu_info.count,
                  is_retx);
  metrics.add_pdus(1, pdu_info.pdu.length());
  auto sdu_latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::high_resolution_clock::now() - pdu_info.sdu_toa);
  metrics.add_pdu_latency_ns(sdu_latency_ns.count());
  // RLC 레이어로 DSCP와 함께 PDU 전달
  lower_dn.on_new_pdu(std::move(pdu_info.pdu), is_retx, pdu_info.dscp);  
}
```

**주요 포인트**:
- PDCP는 DSCP를 `pdcp_tx_buffer_info`와 `pdcp_tx_pdu_info` 구조체에 포함하여 전달

---

## 4. RLC 레이어: DSCP를 버퍼 상태에 포함

**파일**: `include/srsran/rlc/rlc_buffer_state.h`

```cpp
/// Structure used to represent RLC buffer state.
struct rlc_buffer_state {
  /// Amount of bytes pending for transmission.
  unsigned pending_bytes = 0;
  /// Head of line (HOL) time of arrival (TOA)
  std::optional<std::chrono::time_point<std::chrono::steady_clock>> hol_toa;
  /// Head of line DSCP marking associated with the queued SDU, if available.
  std::optional<dscp_value_t> hol_dscp;  // DSCP를 버퍼 상태에 포함
};
```

### 4.1 RLC AM 엔티티에서 버퍼 상태 계산

**파일**: `lib/rlc/rlc_tx_am_entity.cpp`

```cpp
rlc_buffer_state rlc_tx_am_entity::get_buffer_state()
{
  rlc_buffer_state bs = {};

  // ... (버퍼 크기 계산)

  // SDU under segmentation이 있는 경우
  if (sn_under_segmentation != INVALID_RLC_SN) {
    if (tx_window.has_sn(sn_under_segmentation)) {
      rlc_tx_am_sdu_info& sdu_info = tx_window[sn_under_segmentation];
      bs.hol_toa  = sdu_info.time_of_arrival;
      bs.hol_dscp = sdu_info.dscp;  // DSCP 포함
    }
  } else {
    // 큐의 첫 번째 SDU에서 DSCP 추출
    const rlc_sdu* next_sdu = sdu_queue.front();
    if (next_sdu != nullptr) {
      bs.hol_toa  = next_sdu->time_of_arrival;
      bs.hol_dscp = next_sdu->dscp;  // DSCP 포함
    }
  }

  // ReTx 큐가 있는 경우
  if (!retx_queue.empty()) {
    const rlc_tx_am_sdu_info& front_sdu = tx_window[retx_queue.front().sn];
    bs.hol_toa  = front_sdu.time_of_arrival;
    bs.hol_dscp = front_sdu.dscp;  // DSCP 포함
  }

  // ... (나머지 버퍼 상태 계산)

  return bs;
}
```

**주요 포인트**:
- RLC는 Head-of-Line (HOL) SDU의 DSCP를 버퍼 상태에 포함
- MAC으로 버퍼 상태 업데이트 시 DSCP도 함께 전달

---

## 5. MAC 레이어: 버퍼 상태에서 DSCP 추출 및 스케줄러로 전달

**파일**: `lib/mac/mac_dl/mac_cell_processor.cpp`

```cpp
void mac_cell_processor::update_logical_channel_dl_buffer_states(const dl_sched_result& dl_res)
{
  if (dl_res.nof_dl_symbols == 0) {
    return;
  }

  for (const dl_msg_alloc& grant : dl_res.ue_grants) {
    for (const dl_msg_tb_info& tb_info : grant.tb_list) {
      for (const dl_msg_lc_info& lc_info : tb_info.lc_chs_to_sched) {
        if (not lc_info.lcid.is_sdu()) {
          continue;
        }

        // RLC Bearer에서 버퍼 상태 가져오기
        mac_sdu_tx_builder* bearer = ue_mng.get_lc_sdu_builder(
            grant.pdsch_cfg.rnti, lc_info.lcid.to_lcid());
        srsran_sanity_check(bearer != nullptr, 
                           "Scheduler is allocating inexistent bearers");

        // RLC 버퍼 상태 업데이트
        rlc_buffer_state rlc_bs = bearer->on_buffer_state_update();
        
        // MAC 버퍼 상태 메시지 생성
        mac_dl_buffer_state_indication_message bs{
            ue_mng.get_ue_index(grant.pdsch_cfg.rnti), 
            lc_info.lcid.to_lcid(), 
            rlc_bs.pending_bytes};
        bs.hol_dscp = rlc_bs.hol_dscp;  // DSCP 포함
        
        // hol_toa 변환 (steady_clock → system_clock)
        if (rlc_bs.hol_toa.has_value()) {
          auto steady_now = std::chrono::steady_clock::now();
          auto system_now = std::chrono::system_clock::now();
          auto elapsed = steady_now - rlc_bs.hol_toa.value();
          bs.hol_toa = system_now - elapsed;
        }        
        
        logger.debug(
            "MAC DL buffer update: ue_index={} rnti=0x{:04x} lcid={} bytes={} dscp={}",
            bs.ue_index,
            to_value(grant.pdsch_cfg.rnti),
            static_cast<unsigned>(bs.lcid),
            bs.bs,
            bs.hol_dscp.has_value() ? static_cast<int>(bs.hol_dscp->to_uint()) : -1);
        
        // 스케줄러로 버퍼 상태 전달
        sched.handle_dl_buffer_state_update(bs);
      }
    }
  }
}
```

**주요 포인트**:
- RLC 버퍼 상태에서 DSCP를 추출하여 MAC 버퍼 상태 메시지에 포함
- 스케줄러로 버퍼 상태 업데이트 전달

---

## 6. 스케줄러: UE 컨텍스트에 DSCP 저장

**파일**: `lib/scheduler/ue_context/ue.cpp`

```cpp
void ue::handle_dl_buffer_state_indication(lcid_t                    lcid,
                                           unsigned                  pending_bytes,
                                           slot_point                hol_toa,
                                           std::optional<dscp_value_t> hol_dscp)
{
  // ... (버퍼 상태 처리)

  sched_logger.debug("UE DL BS indication: ue={} lcid={} pending={} hol_dscp={}",
                     ue_index,
                     static_cast<unsigned>(lcid),
                     pending_bytes,
                     hol_dscp.has_value() ? static_cast<int>(hol_dscp->to_uint()) : -1);
 
  // DL Logical Channel Manager에 버퍼 상태 전달 (DSCP 포함)
  dl_lc_ch_mgr.handle_dl_buffer_status_indication(lcid, pending_bytes, hol_toa, hol_dscp);  
}
```

### 6.1 DL Logical Channel Manager에 DSCP 저장

**파일**: `lib/scheduler/ue_context/dl_logical_channel_manager.h`

```cpp
class dl_logical_channel_manager
{
  // ...

  /// \brief Update DL buffer status for a given LCID.
  void handle_dl_buffer_status_indication(lcid_t                    lcid,
                                          unsigned                  buffer_status,
                                          slot_point                hol_toa     = {},
                                          std::optional<dscp_value_t> hol_dscp = std::nullopt)
  {
    // We apply this limit to avoid potential overflows.
    static constexpr unsigned max_buffer_status = 1U << 24U;
    srsran_sanity_check(lcid < MAX_NOF_RB_LCIDS, "Max LCID value 32 exceeded");
    channels[lcid].buf_st  = std::min(buffer_status, max_buffer_status);
    channels[lcid].hol_toa = hol_toa;
    channels[lcid].hol_dscp = hol_dscp;  // DSCP 저장
    
    // Log T_in: Packet arrival time (HOL time-of-arrival)
    if (hol_toa.valid() and buffer_status > 0) {
      auto now = std::chrono::steady_clock::now();
      auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();
      int dscp_value = hol_dscp.has_value() ? 
          static_cast<int>(hol_dscp->to_uint()) : -1;
      srslog::fetch_basic_logger("SCHED").info(
          "[RAN_DELAY] T_in: ue_lcid={} hol_toa_slot={} hol_toa_count={} "
          "buffer_status={} dscp={} time_ms={}",
          static_cast<unsigned>(lcid), 
          hol_toa.slot_index(), 
          hol_toa.count(), 
          buffer_status, 
          dscp_value, 
          time_ms);
    }
  }

  /// Get HOL DSCP for a given LCID
  std::optional<dscp_value_t> hol_dscp(lcid_t lcid) const
  {
    return is_active(lcid) ? channels[lcid].hol_dscp : std::nullopt;
  }

private:
  struct channel_context {
    bool active = false;
    unsigned buf_st = 0;
    slot_point hol_toa;
    std::optional<dscp_value_t> hol_dscp;  // DSCP 저장
    // ...
  };
  
  std::array<channel_context, MAX_NOF_RB_LCIDS> channels;
};
```

**주요 포인트**:
- 각 logical channel의 컨텍스트에 DSCP 저장
- `hol_dscp()` 메서드로 DSCP 조회 가능

---

## 7. 스케줄러: DSCP 기반 우선순위 계산

### 7.1 Logical Channel 우선순위 계산

**파일**: `lib/scheduler/ue_context/dl_logical_channel_manager.cpp`

```cpp
uint64_t dl_logical_channel_manager::compute_priority_metric(lcid_t lcid) const
{
  uint64_t base_prio = std::numeric_limits<uint16_t>::max();

  // 기본 우선순위 가져오기
  if (channel_configs.has_value() && channel_configs->contains(lcid)) {
    base_prio = get_lc_prio(*channel_configs.value()[lcid]);
  }

  // DSCP 기반 bias 계산
  uint64_t dscp_bias = DSCP_DEFAULT_BIAS;
  if (channels[lcid].hol_dscp.has_value()) {
    unsigned raw_dscp = channels[lcid].hol_dscp->to_uint();
    if (raw_dscp >= DSCP_PRIORITY_SPAN) {
      raw_dscp = DSCP_PRIORITY_SPAN - 1U;
    }
    // 높은 DSCP 값 = 낮은 bias = 높은 우선순위
    dscp_bias = (DSCP_PRIORITY_SPAN - 1U) - raw_dscp;
  }

  // base_prio를 6비트 왼쪽으로 시프트하고 DSCP bias 추가
  return (base_prio << 6U) + dscp_bias;
}

lcid_t dl_logical_channel_manager::get_max_prio_lcid() const
{
  lcid_t  best_lcid   = INVALID_LCID;
  uint64_t best_metric = std::numeric_limits<uint64_t>::max();

  for (const auto lcid : sorted_channels) {
    if (not has_pending_bytes(lcid)) {
      continue;
    }

    const uint64_t metric = compute_priority_metric(lcid);
    if (metric < best_metric) {
      best_metric = metric;
      best_lcid   = lcid;
      continue;
    }

    // 동일한 우선순위인 경우 DSCP가 있는 채널 우선
    if (metric == best_metric && best_lcid != INVALID_LCID) {
      const channel_context& current_best = channels[best_lcid];
      const channel_context& candidate    = channels[lcid];

      const bool candidate_has_dscp = candidate.hol_dscp.has_value();
      const bool best_has_dscp      = current_best.hol_dscp.has_value();

      if (candidate_has_dscp && not best_has_dscp) {
        best_lcid = lcid;
        continue;
      }

      // HOL TOA 비교
      if (candidate.hol_toa < current_best.hol_toa) {
        best_lcid = lcid;
      }
    }
  }

  return best_lcid;
}
```

**주요 포인트**:
- DSCP 값이 높을수록 우선순위가 높아짐
- 동일한 우선순위인 경우 DSCP가 있는 채널이 우선

### 7.2 UE 레벨 DSCP 기반 우선순위 가중치 계산

**파일**: `lib/scheduler/policy/scheduler_time_qos.cpp`

```cpp
static double compute_dl_qos_weights(const slice_ue&                         u,
                                     double                                  estim_dl_rate,
                                     double                                  avg_dl_rate,
                                     slot_point                              slot_tx,
                                     const time_qos_scheduler_expert_config& policy_params)
{
  static auto& sched_logger = srslog::fetch_basic_logger("SCHED");
  sched_logger.info("ue={} entered compute_dl_qos_weights avg_dl_rate={:.3f}", 
                    u.ue_index(), avg_dl_rate);  
  std::optional<dscp_value_t> max_dscp = u.max_dl_dscp();  // UE의 최대 DSCP 가져오기
  
  if (max_dscp.has_value()) {
    sched_logger.debug("DL DSCP candidate: ue={} dscp={} avg_dl_rate={:.3f}",
                       u.ue_index(),
                       max_dscp->to_uint(),
                       avg_dl_rate);
  }
  
  // 평균 전송률이 0인 경우 (아직 할당받지 않은 UE)
  if (avg_dl_rate == 0) {
    if (max_dscp.has_value()) {
      // DSCP 기반 우선순위 가중치 계산
      double prio_weight = static_cast<double>(max_dscp->to_uint() + 1U);
      sched_logger.debug(
          "DL DSCP override: ue={} dscp={} prio_weight={:.3f}", 
          u.ue_index(), max_dscp->to_uint(), prio_weight);
      return prio_weight;
    }  
    // Highest priority to UEs that have not yet received any allocation.
    return std::numeric_limits<double>::max();
  }

  // ... (QoS 기반 가중치 계산)

  double prio_weight = default_prio_weight;
  if (max_dscp.has_value()) {
    // DSCP 기반 우선순위 가중치로 오버라이드
    prio_weight = static_cast<double>(max_dscp->to_uint() + 1U);
    sched_logger.debug(
        "DL DSCP override: ue={} dscp={} prio_weight={:.3f}", 
        u.ue_index(), max_dscp->to_uint(), prio_weight);  
  }

  // ... (최종 가중치 계산)

  // The return is a combination of ARP and QoS priorities, GBR and PF weight functions.
  double final_weight = combine_qos_metrics(pf_weight, gbr_weight, prio_weight, delay_weight, policy_params);
  
  return final_weight;
}
```

### 7.3 UE의 최대 DSCP 조회

**파일**: `lib/scheduler/slicing/slice_ue_repository.h`

```cpp
std::optional<dscp_value_t> max_dl_dscp() const
{
  std::optional<dscp_value_t> max_dscp;
  auto                        lc_list = u.ue_cfg_dedicated()->logical_channels();
  auto&                       logger  = srslog::fetch_basic_logger("SCHED");    
    
  if (not lc_list.has_value()) {
    logger.debug("max_dl_dscp: ue={} no logical channel config", 
                 fmt::underlying(ue_index()));      
    return max_dscp;
  }

  // 모든 logical channel 중 최대 DSCP 값 찾기
  for (logical_channel_config_ptr lc : *lc_list) {
    bool                       lc_in_slice = contains(lc->lcid);
    unsigned                   pending     = pending_dl_newtx_bytes(lc->lcid);      
    std::optional<dscp_value_t> lc_dscp    = u.dl_logical_channels().hol_dscp(lc->lcid);

    logger.debug("max_dl_dscp: ue={} lcid={} in_slice={} pending={} dscp={}",
                 fmt::underlying(ue_index()),
                 static_cast<unsigned>(lc->lcid),                   
                 lc_in_slice,
                 pending,
                 lc_dscp.has_value() ? static_cast<int>(lc_dscp->to_uint()) : -1);

    if (lc_in_slice and pending > 0 and lc_dscp.has_value()) {
      if (not max_dscp.has_value() or lc_dscp->to_uint() > max_dscp->to_uint()) {
        max_dscp = lc_dscp;
      }
    }
  }

  return max_dscp;
}
```

**주요 포인트**:
- UE의 모든 logical channel 중 최대 DSCP 값을 찾아 우선순위 계산에 사용
- DSCP 값이 높을수록 우선순위 가중치가 높아짐

---

## 8. 데이터 구조체 요약

### 8.1 DSCP 값 타입

**파일**: `include/srsran/ran/qos/dscp_qos_mapping.h`

```cpp
/// \brief DSCP (Differentiated Services Code Point) value extracted from IP header.
/// DSCP occupies 6 bits in the IPv4 TOS field or IPv6 Traffic Class field.
/// Values range from 0 to 63.
struct dscp_value_t {
  uint8_t value = 0;

  constexpr dscp_value_t() = default;
  constexpr explicit dscp_value_t(uint8_t val) : value(val & 0x3F) {} // Mask to 6 bits

  constexpr bool operator==(const dscp_value_t& other) const { return value == other.value; }
  constexpr bool operator!=(const dscp_value_t& other) const { return value != other.value; }
  constexpr bool operator<(const dscp_value_t& other) const { return value < other.value; }

  constexpr uint8_t to_uint() const { return value; }
};
```

### 8.2 주요 구조체에서의 DSCP 사용

1. **GTP-U**: `gtpu_rx_sdu_info.dscp`
2. **RLC**: `rlc_buffer_state.hol_dscp`
3. **MAC**: `mac_dl_buffer_state_indication_message.hol_dscp`
4. **Scheduler**: `channel_context.hol_dscp` (dl_logical_channel_manager)

---

## 9. 주요 상수 및 설정

**파일**: `lib/scheduler/ue_context/dl_logical_channel_manager.cpp`

```cpp
// DSCP prioritization scaling factor.
static constexpr uint64_t DSCP_PRIORITY_SPAN = 64U;
static constexpr uint64_t DSCP_DEFAULT_BIAS  = DSCP_PRIORITY_SPAN / 2U;
```

- `DSCP_PRIORITY_SPAN`: DSCP 값의 범위 (0-63)
- `DSCP_DEFAULT_BIAS`: DSCP가 없는 경우 기본 bias 값 (32)

---

## 10. 로깅 및 디버깅

DSCP 전달 과정에서 다음과 같은 로그가 출력됩니다:

1. **GTP-U**: `"DSCP={} detected, QFI={} (unchanged)"`
2. **SDAP**: `"TX PDU. {} pdu_len={} dscp={}"`
3. **PDCP**: `"TX SDU. sdu_len={} dscp={}"`
4. **MAC**: `"MAC DL buffer update: ue_index={} rnti=0x{:04x} lcid={} bytes={} dscp={}"`
5. **Scheduler**: 
   - `"[RAN_DELAY] T_in: ue_lcid={} ... dscp={} ..."`
   - `"DL DSCP override: ue={} dscp={} prio_weight={:.3f}"`
   - `"max_dl_dscp: ue={} lcid={} ... dscp={}"`

---

## 11. 요약

DSCP는 다음과 같은 경로로 전달됩니다:

1. **GTP-U**: IP 패킷에서 DSCP 추출 (`extract_dscp_from_ip_packet`)
2. **SDAP**: DSCP를 그대로 전달
3. **PDCP**: DSCP를 포함하여 RLC로 전달
4. **RLC**: HOL SDU의 DSCP를 버퍼 상태에 포함
5. **MAC**: RLC 버퍼 상태에서 DSCP 추출하여 스케줄러로 전달
6. **Scheduler**: 
   - Logical Channel 레벨: DSCP 기반 우선순위 계산
   - UE 레벨: 최대 DSCP 기반 우선순위 가중치 계산

이를 통해 DSCP 값이 높은 패킷이 우선적으로 스케줄링되어 QoS가 보장됩니다.

