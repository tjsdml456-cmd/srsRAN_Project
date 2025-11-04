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

#pragma once

#include "srsran/adt/byte_buffer.h"
#include "srsran/ran/cu_types.h"
#include <cstdint>
#include <optional>

namespace srsran {

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

/// \brief Standard DSCP classes as defined in RFC 2474, RFC 3246, RFC 4594
namespace dscp_classes {
/// Default forwarding
constexpr dscp_value_t default_forwarding{0};
/// Class Selector 1-7
constexpr dscp_value_t cs1{8};
constexpr dscp_value_t cs2{16};
constexpr dscp_value_t cs3{24};
constexpr dscp_value_t cs4{32};
constexpr dscp_value_t cs5{40};
constexpr dscp_value_t cs6{48};
constexpr dscp_value_t cs7{56};
/// Assured Forwarding (AF) classes
constexpr dscp_value_t af11{10};
constexpr dscp_value_t af12{12};
constexpr dscp_value_t af13{14};
constexpr dscp_value_t af21{18};
constexpr dscp_value_t af22{20};
constexpr dscp_value_t af23{22};
constexpr dscp_value_t af31{26};
constexpr dscp_value_t af32{28};
constexpr dscp_value_t af33{30};
constexpr dscp_value_t af41{34};
constexpr dscp_value_t af42{36};
constexpr dscp_value_t af43{38};
/// Expedited Forwarding (EF) - highest priority
constexpr dscp_value_t ef{46};
/// Voice Admit (VA)
constexpr dscp_value_t voice_admit{44};
} // namespace dscp_classes

/// \brief Configuration for DSCP to QFI mapping
struct dscp_qos_mapping_config {
  /// Enable DSCP-based QoS Flow classification
  bool enable_dscp_mapping = false;
  /// Default QFI when DSCP mapping is not found (QFI 8 = Best Effort)
  qos_flow_id_t default_qfi = uint_to_qos_flow_id(8);
};

/// \brief DSCP to QFI classifier
/// Maps DSCP values extracted from IP packets to QoS Flow IDs (QFI).
/// This allows RAN to dynamically classify packets into appropriate QoS Flows
/// based on DSCP marking, utilizing the QFI→5QI mappings already configured by Core.
class dscp_qos_classifier
{
public:
  /// Constructor with default mapping
  dscp_qos_classifier();

  /// Constructor with custom configuration
  explicit dscp_qos_classifier(const dscp_qos_mapping_config& config);

  /// \brief Get QFI for a given DSCP value
  /// \param dscp The DSCP value extracted from IP header
  /// \return QoS Flow ID that should handle this packet
  qos_flow_id_t get_qfi(dscp_value_t dscp) const;

  /// \brief Set custom mapping for a DSCP value
  /// \param dscp The DSCP value
  /// \param qfi The QFI to map to
  void set_mapping(dscp_value_t dscp, qos_flow_id_t qfi);

  /// \brief Enable or disable DSCP-based classification
  void set_enabled(bool enabled) { config_.enable_dscp_mapping = enabled; }

  /// \brief Check if DSCP classification is enabled
  bool is_enabled() const { return config_.enable_dscp_mapping; }

private:
  /// Initialize default DSCP to QFI mappings
  void init_default_mappings();

  dscp_qos_mapping_config config_;
  /// Mapping table from DSCP (0-63) to QFI
  std::array<qos_flow_id_t, 64> dscp_to_qfi_map_;
};

/// \brief Extract DSCP value from IPv4 or IPv6 packet
/// \param pdu The IP packet buffer
/// \return DSCP value if successfully extracted, nullopt otherwise
std::optional<dscp_value_t> extract_dscp_from_ip_packet(const byte_buffer& pdu);

/// \brief Extract DSCP value from IPv4 or IPv6 packet (byte_buffer_view version)
/// \param pdu The IP packet buffer view
/// \return DSCP value if successfully extracted, nullopt otherwise
std::optional<dscp_value_t> extract_dscp_from_ip_packet(byte_buffer_view pdu);


} // namespace srsran


