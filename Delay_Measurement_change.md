# 지연 측정 변경 사항 정리

## 개요
RAN(Radio Access Network) 지연 측정을 위해 3개의 주요 지연 지점을 측정하도록 코드를 변경했습니다.

## 측정하는 지연 지점

### 1. T_in: 패킷 도착 시간 (Packet Arrival Time)
- **의미**: RLC 계층에서 패킷이 도착한 시간 (HOL: Head-of-Line Time-of-Arrival)
- **측정 위치**: `lib/scheduler/ue_context/dl_logical_channel_manager.h`

### 2. T_sched: 스케줄러 결정 시간 (Scheduler Decision Time)
- **의미**: 스케줄러가 TB(Transport Block)를 할당한 시간
- **측정 위치**: `lib/scheduler/ue_scheduling/ue_cell_grid_allocator.cpp`

### 3. T_tx: PDSCH 전송 시간 (PDSCH Transmission Time)
- **의미**: PDSCH가 실제로 전송되는 시간 (slot + symbol 정보 포함)
- **측정 위치**: `lib/scheduler/ue_scheduling/ue_cell_grid_allocator.cpp`

### 4. T_ack: HARQ ACK 수신 시간 (HARQ ACK Reception Time)
- **의미**: UE로부터 HARQ ACK를 수신한 시간
- **측정 위치**: `lib/scheduler/ue_scheduling/ue_event_manager.cpp`

## 변경된 파일 상세

### 1. `lib/scheduler/ue_context/dl_logical_channel_manager.h`

#### 변경 내용
- **위치**: `handle_dl_buffer_status_indication()` 함수 내 (181-188줄)
- **추가 기능**: 
  - T_in 로깅 추가
  - HOL TOA(HOL Time-of-Arrival) 정보를 로그로 출력
  - DSCP 값과 버퍼 상태 정보 포함

#### 코드 변경
```cpp
// Log T_in: Packet arrival time (HOL time-of-arrival)
if (hol_toa.valid() and buffer_status > 0) {
  auto now = std::chrono::steady_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  int dscp_value = hol_dscp.has_value() ? static_cast<int>(hol_dscp->to_uint()) : -1;
  srslog::fetch_basic_logger("SCHED").info("[RAN_DELAY] T_in: ue_lcid={} hol_toa_slot={} hol_toa_count={} buffer_status={} dscp={} time_ms={}",
             static_cast<unsigned>(lcid), hol_toa.slot_index(), hol_toa.count(), buffer_status, dscp_value, time_ms);
}
```

#### 로그 출력 형식
```
[RAN_DELAY] T_in: ue_lcid={lcid} hol_toa_slot={slot} hol_toa_count={count} buffer_status={bytes} dscp={dscp} time_ms={timestamp}
```

---

### 2. `lib/scheduler/ue_scheduling/ue_cell_grid_allocator.cpp`

#### 변경 내용
- **위치**: `set_pdsch_params()` 함수 내 (397-449줄)
- **추가 기능**:
  1. **T_sched 로깅** (417-437줄): 스케줄러가 TB를 할당한 시점
  2. **T_tx 로깅** (439-449줄): PDSCH 전송 시점

#### 주요 변경 사항

##### T_sched 로깅 (스케줄러 결정 시간)
- 모든 활성 LCID에서 가장 오래된 HOL TOA를 찾아 큐잉 지연 계산
- 큐잉 지연 = T_sched - T_in (slots 및 ms 단위)
- DSCP 사용 여부 및 스케줄링 모드 정보 포함

```cpp
// Find the oldest HOL TOA and DSCP from all active LCIDs with pending bytes
slot_point oldest_hol_toa;
std::optional<dscp_value_t> oldest_hol_dscp;
bool has_hol_toa = false;
// ... (모든 LCID를 순회하며 가장 오래된 HOL TOA 찾기)

// Log T_sched: Scheduler decision time (when TB is allocated)
auto now = std::chrono::steady_clock::now();
auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
if (has_hol_toa) {
  // Calculate queueing delay: T_sched - T_in (in slots, then convert to ms)
  int64_t queueing_delay_slots = static_cast<int64_t>(pdsch_alloc.slot.count()) - static_cast<int64_t>(oldest_hol_toa.count());
  double queueing_delay_ms = queueing_delay_slots * slot_duration_ms;
  logger.info("[RAN_DELAY] T_sched: ... queueing_delay_slots={} queueing_delay_ms={:.3f} ...");
}
```

##### T_tx 로깅 (PDSCH 전송 시간)
- PDSCH 전송 슬롯 및 심볼 정보 포함
- 전송 지연 계산 (심볼 수 기반)

```cpp
// Log T_tx: PDSCH transmission slot time with symbol information
unsigned nof_symbols = pdsch_td_cfg.symbols.length();
double transmission_delay_ms = nof_symbols * slot_duration_ms / 14.0; // 14 symbols per slot
logger.info("[RAN_DELAY] T_tx: ... symbols=[{},{}] nof_symbols={} ... transmission_delay_ms={:.3f}");
```

#### 로그 출력 형식

**T_sched:**
```
[RAN_DELAY] T_sched: ue={ue_index} rnti={rnti} harq_id={harq_id} pdsch_slot={slot} tb_size={bytes} hol_toa_slot={slot} hol_toa_count={count} dscp={dscp} dscp_used={0|1} scheduling_mode={DSCP|5QI_ONLY} queueing_delay_slots={slots} queueing_delay_ms={ms} time_ms={timestamp}
```

**T_tx:**
```
[RAN_DELAY] T_tx: ue={ue_index} rnti={rnti} harq_id={harq_id} pdsch_slot={slot} slot_index={index} slot_count={count} symbols=[{start},{stop}] nof_symbols={count} scs={scs} dscp={dscp} dscp_used={0|1} scheduling_mode={DSCP|5QI_ONLY} transmission_delay_ms={ms}
```

---

### 3. `lib/scheduler/ue_scheduling/ue_event_manager.cpp`

#### 변경 내용
- **위치**: `handle_harq_ind()` 함수 내 (998-1024줄)
- **추가 기능**: T_ack 로깅 추가 (HARQ ACK 수신 시간)

#### 코드 변경
```cpp
// Log T_ack: HARQ ACK reception time
auto now = std::chrono::steady_clock::now();
auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
const char* ack_status_str = (harq_bits[harq_idx] == mac_harq_ack_report_status::ack) ? "ACK" :
                             (harq_bits[harq_idx] == mac_harq_ack_report_status::nack) ? "NACK" : "DTX";

// Get PDSCH slot from HARQ process (if available)
slot_point pdsch_slot = result->h_dl.pdsch_slot();
if (pdsch_slot.valid()) {
  // Calculate air interface delay: (T_ack - T_tx) - Transmission_delay
  int64_t air_interface_delay_slots = static_cast<int64_t>(uci_sl.count()) - static_cast<int64_t>(pdsch_slot.count());
  double slot_duration_ms = 1.0 / (1U << static_cast<unsigned>(scs));
  double air_interface_delay_ms = air_interface_delay_slots * slot_duration_ms;
  
  logger.info("[RAN_DELAY] T_ack: ... air_interface_delay_slots={} air_interface_delay_ms={:.3f} ...");
}
```

#### 로그 출력 형식
```
[RAN_DELAY] T_ack: ue={ue_index} rnti={rnti} harq_id={harq_id} uci_slot={slot} slot_index={index} slot_count={count} pdsch_slot={slot} pdsch_slot_count={count} air_interface_delay_slots={slots} air_interface_delay_ms={ms} ack_status={ACK|NACK|DTX} tbs={bytes} time_ms={timestamp}
```

---

### 4. `lib/mac/mac_dl/mac_cell_processor.cpp`

#### 변경 내용
- **위치**: `update_logical_channel_dl_buffer_states()` 함수 내 (567-574줄)
- **추가 기능**: HOL TOA를 steady_clock에서 system_clock로 변환

#### 코드 변경
```cpp
// Convert hol_toa from steady_clock to system_clock for MAC message
if (rlc_bs.hol_toa.has_value()) {
  // Convert steady_clock time_point to system_clock time_point
  auto steady_now = std::chrono::steady_clock::now();
  auto system_now = std::chrono::system_clock::now();
  auto elapsed = steady_now - rlc_bs.hol_toa.value();
  bs.hol_toa = system_now - elapsed;
}
```

#### 목적
- RLC 계층에서 전달된 HOL TOA(steady_clock 기반)를 MAC 메시지에 맞게 system_clock로 변환
- 시간 동기화를 위한 변환 처리

---

## 측정 가능한 지연 메트릭

### 1. 큐잉 지연 (Queueing Delay)
- **계산**: `T_sched - T_in`
- **의미**: 패킷이 RLC에 도착한 후 스케줄러가 할당하기까지의 대기 시간
- **단위**: slots, milliseconds
Queueing Delay = “버퍼에 머문 시간”**

쉽게 말해서,

T_in = 패킷이 RLC 버퍼에 들어온 순간

T_sched = 그 패킷이 MAC에 의해 전송하라고 선택된 순간

그러면,

버퍼에 들어온 이후 → 전송되기 전까지
얼마나 오래 기다렸는가?

= 이것이 바로 Queueing Delay.

### 2. 전송 지연 (Transmission Delay)
- **계산**: PDSCH 심볼 수 기반
- **의미**: PDSCH가 실제로 전송되는데 걸리는 시간
- **단위**: milliseconds
- **공식**: `nof_symbols * slot_duration_ms / 14.0`

### 3. 공중 인터페이스 지연 (Air Interface Delay)
PDSCH가 전송된 뒤 → UE로 갔다가 → ACK/NACK이 다시 gNB로 돌아올 때까지 걸리는 순수 무선 왕복 시간
- **계산**: `T_ack - T_tx` (슬롯 단위)
- **의미**: PDSCH 전송부터 HARQ ACK 수신까지의 시간
- **단위**: slots, milliseconds

### 4. 전체 지연 (End-to-End Delay)
gNB PHY가 실제로 PDSCH 데이터 심볼을 보내는 데 걸리는 시간
- **계산**: `T_ack - T_in` (전체 경로)
- **의미**: 패킷 도착부터 ACK 수신까지의 전체 지연
- **단위**: slots, milliseconds

## 로그 태그
모든 지연 측정 로그는 `[RAN_DELAY]` 태그로 시작하여 쉽게 필터링할 수 있습니다.

## 로그 레벨
- 모든 지연 측정 로그는 `info` 레벨로 출력됩니다.
- 로거 이름: `SCHED`

## 참고 사항
1. **시간 단위 변환**: 
   - 15kHz SCS: 1 slot = 1ms
   - 30kHz SCS: 1 slot = 0.5ms
   - 60kHz SCS: 1 slot = 0.25ms
   - 120kHz SCS: 1 slot = 0.125ms

2. **DSCP 지원**: 
   - DSCP 기반 스케줄링이 활성화된 경우 `dscp_used=1`, `scheduling_mode=DSCP`
   - 그렇지 않은 경우 `dscp_used=0`, `scheduling_mode=5QI_ONLY`

3. **HOL TOA 추적**: 
   - 모든 활성 LCID 중 가장 오래된 HOL TOA를 사용하여 정확한 큐잉 지연 계산

## 사용 예시

로그에서 지연을 분석하려면:
```bash
# T_in 로그 필터링
grep "\[RAN_DELAY\] T_in" log_file

# T_sched 로그 필터링
grep "\[RAN_DELAY\] T_sched" log_file

# T_tx 로그 필터링
grep "\[RAN_DELAY\] T_tx" log_file

# T_ack 로그 필터링
grep "\[RAN_DELAY\] T_ack" log_file

# 특정 UE의 모든 지연 로그
grep "\[RAN_DELAY\].*ue=0" log_file
```

## 변경 요약

| 파일 | 변경 내용 | 목적 |
|------|----------|------|
| `dl_logical_channel_manager.h` | T_in 로깅 추가 | 패킷 도착 시간 측정 |
| `ue_cell_grid_allocator.cpp` | T_sched, T_tx 로깅 추가 | 스케줄링 및 전송 시간 측정 |
| `ue_event_manager.cpp` | T_ack 로깅 추가 | ACK 수신 시간 측정 |
| `mac_cell_processor.cpp` | HOL TOA 클럭 변환 | 시간 동기화 |

