/*
 *
 * Copyright 2021-2025 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsran/ran/qos/dscp_qos_mapping.h"
#include "srsran/srslog/srslog.h"
#include <algorithm>

using namespace srsran;

// IPv4 version number
static constexpr uint8_t IPV4_VERSION = 4;
// IPv6 version number
static constexpr uint8_t IPV6_VERSION = 6;
// Minimum IPv4 header length
static constexpr size_t IPV4_MIN_HEADER_LEN = 20;
// IPv6 header length (fixed)
static constexpr size_t IPV6_HEADER_LEN = 40;

dscp_qos_classifier::dscp_qos_classifier() : dscp_qos_classifier(dscp_qos_mapping_config{})
{
}

dscp_qos_classifier::dscp_qos_classifier(const dscp_qos_mapping_config& config) : config_(config)
{
  init_default_mappings();
}

void dscp_qos_classifier::init_default_mappings()
{
  // Initialize all DSCP values to default QFI (8 = Best Effort)
  std::fill(dscp_to_qfi_map_.begin(), dscp_to_qfi_map_.end(), config_.default_qfi);

  // Map standard DSCP classes to sequential QFI values (1-8)
  // Based on RFC 4594 DSCP classes
  // Note: QFI→5QI mapping must be configured in Core network as follows:
  //   QFI 1 → 5QI 1 (Voice)
  //   QFI 2 → 5QI 2 (Video)
  //   QFI 3 → 5QI 3 (Gaming)
  //   QFI 4 → 5QI 5 (Signaling)
  //   QFI 5 → 5QI 6 (Streaming)
  //   QFI 6 → 5QI 7 (Broadcast)
  //   QFI 7 → 5QI 8 (Premium data)
  //   QFI 8 → 5QI 9 (Best Effort)
  
  // QFI 1: Voice traffic (maps to 5QI 1)
  // Expedited Forwarding (EF) and Voice Admit
  dscp_to_qfi_map_[dscp_classes::ef.to_uint()] = uint_to_qos_flow_id(1);
  dscp_to_qfi_map_[dscp_classes::voice_admit.to_uint()] = uint_to_qos_flow_id(1);
  
  // QFI 2: Video traffic (maps to 5QI 2)
  // Assured Forwarding Class 4 (AF4x) - Conversational Video
  dscp_to_qfi_map_[dscp_classes::af41.to_uint()] = uint_to_qos_flow_id(2);
  dscp_to_qfi_map_[dscp_classes::af42.to_uint()] = uint_to_qos_flow_id(2);
  dscp_to_qfi_map_[dscp_classes::af43.to_uint()] = uint_to_qos_flow_id(2);
  
  // QFI 3: Gaming traffic (maps to 5QI 3)
  // Class Selector 4 / Assured Forwarding Class 3 (AF3x) - Real-time gaming
  dscp_to_qfi_map_[dscp_classes::cs4.to_uint()] = uint_to_qos_flow_id(3);
  dscp_to_qfi_map_[dscp_classes::af31.to_uint()] = uint_to_qos_flow_id(3);
  dscp_to_qfi_map_[dscp_classes::af32.to_uint()] = uint_to_qos_flow_id(3);
  dscp_to_qfi_map_[dscp_classes::af33.to_uint()] = uint_to_qos_flow_id(3);
  
  // QFI 4: Signaling traffic (maps to 5QI 5)
  // Class Selector 5/6/7 - IMS/Network signaling
  dscp_to_qfi_map_[dscp_classes::cs5.to_uint()] = uint_to_qos_flow_id(4);
  dscp_to_qfi_map_[dscp_classes::cs6.to_uint()] = uint_to_qos_flow_id(4);
  dscp_to_qfi_map_[dscp_classes::cs7.to_uint()] = uint_to_qos_flow_id(4);
  
  // QFI 5: Streaming traffic (maps to 5QI 6)
  // Assured Forwarding Class 2 (AF2x) - Video buffered streaming
  dscp_to_qfi_map_[dscp_classes::af21.to_uint()] = uint_to_qos_flow_id(5);
  dscp_to_qfi_map_[dscp_classes::af22.to_uint()] = uint_to_qos_flow_id(5);
  dscp_to_qfi_map_[dscp_classes::af23.to_uint()] = uint_to_qos_flow_id(5);
  
  // QFI 6: Broadcast traffic (maps to 5QI 7)
  // Class Selector 3 - Broadcast/Multicast video
  dscp_to_qfi_map_[dscp_classes::cs3.to_uint()] = uint_to_qos_flow_id(6);
  
  // QFI 7: Premium data (maps to 5QI 8)
  // Class Selector 2 / Assured Forwarding Class 1 (AF1x) - TCP bulk data
  dscp_to_qfi_map_[dscp_classes::cs2.to_uint()] = uint_to_qos_flow_id(7);
  dscp_to_qfi_map_[dscp_classes::af11.to_uint()] = uint_to_qos_flow_id(7);
  dscp_to_qfi_map_[dscp_classes::af12.to_uint()] = uint_to_qos_flow_id(7);
  dscp_to_qfi_map_[dscp_classes::af13.to_uint()] = uint_to_qos_flow_id(7);
  
  // QFI 8: Best Effort (maps to 5QI 9)
  // Default Forwarding / Class Selector 1
  dscp_to_qfi_map_[dscp_classes::default_forwarding.to_uint()] = uint_to_qos_flow_id(8);
  dscp_to_qfi_map_[dscp_classes::cs1.to_uint()] = uint_to_qos_flow_id(8);
}

qos_flow_id_t dscp_qos_classifier::get_qfi(dscp_value_t dscp) const
{
  static auto& logger = srslog::fetch_basic_logger("DSCP-QOS");
  
  if (!config_.enable_dscp_mapping) {
    logger.debug("DSCP mapping disabled, using default QFI={}", qos_flow_id_to_uint(config_.default_qfi));
    return config_.default_qfi;
  }
  
  qos_flow_id_t mapped_qfi = dscp_to_qfi_map_[dscp.to_uint()];
  logger.info("DSCP to QFI mapping: DSCP={} → QFI={}", dscp.to_uint(), qos_flow_id_to_uint(mapped_qfi));
  
  return mapped_qfi;
}

void dscp_qos_classifier::set_mapping(dscp_value_t dscp, qos_flow_id_t qfi)
{
  dscp_to_qfi_map_[dscp.to_uint()] = qfi;
}

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
    logger.debug("DSCP extracted from IPv4 packet: DSCP={} TOS={:#x} pdu_len={}", dscp_val, tos, pdu.length());
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
    // First 4 bits of traffic class are in bits 4-7 of first byte
    // Last 4 bits of traffic class are in bits 0-3 of second byte
    uint8_t traffic_class = ((first & 0x0F) << 4) | ((second >> 4) & 0x0F);
    // DSCP is the upper 6 bits of traffic class
    uint8_t dscp_val = traffic_class >> 2;
    logger.debug("DSCP extracted from IPv6 packet: DSCP={} TC={:#x} pdu_len={}", dscp_val, traffic_class, pdu.length());
    return dscp_value_t{dscp_val};
  }

  logger.debug("Unknown IP version: {} pdu_len={}", version, pdu.length());
  return std::nullopt;
}

std::optional<dscp_value_t> srsran::extract_dscp_from_ip_packet(byte_buffer_view pdu)
{
  static auto& logger = srslog::fetch_basic_logger("DSCP-QOS");
  
  if (pdu.empty()) {
    return std::nullopt;
  }

  // Get first byte to determine IP version
  uint8_t first_byte = pdu[0];
  uint8_t version    = (first_byte >> 4) & 0x0F;

  if (version == IPV4_VERSION) {
    // IPv4: DSCP is in bits 2-7 of the second byte (TOS field)
    if (pdu.length() < IPV4_MIN_HEADER_LEN) {
      return std::nullopt;
    }
    uint8_t tos = pdu[1];
    // DSCP is the upper 6 bits of TOS
    uint8_t dscp_val = tos >> 2;
    logger.debug("DSCP extracted from IPv4 packet: DSCP={} TOS={:#x} pdu_len={}", dscp_val, tos, pdu.length());
    return dscp_value_t{dscp_val};
  } else if (version == IPV6_VERSION) {
    // IPv6: DSCP is in bits 4-9 of the first 2 bytes (Traffic Class field)
    if (pdu.length() < IPV6_HEADER_LEN) {
      return std::nullopt;
    }
    uint8_t first  = pdu[0];
    uint8_t second = pdu[1];
    // Traffic class = bits 4-11 of the first 32 bits
    uint8_t traffic_class = ((first & 0x0F) << 4) | ((second >> 4) & 0x0F);
    // DSCP is the upper 6 bits of traffic class
    uint8_t dscp_val = traffic_class >> 2;
    logger.debug("DSCP extracted from IPv6 packet: DSCP={} TC={:#x} pdu_len={}", dscp_val, traffic_class, pdu.length());
    return dscp_value_t{dscp_val};
  }

  logger.debug("Unknown IP version: {} pdu_len={}", version, pdu.length());
  return std::nullopt;
}


