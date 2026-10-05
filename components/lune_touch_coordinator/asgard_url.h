#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace esphome::lune_touch_coordinator::asgard_url {

inline bool valid_host(const char *host) {
  return host != nullptr && host[0] != '\0' && std::strpbrk(host, " /?#") == nullptr;
}

inline bool request_succeeded(bool transport_ok, int status_code) {
  return transport_ok && status_code >= 200 && status_code < 300;
}

inline bool encode_entity_once(const char *src, char *out, size_t out_len) {
  if (out_len == 0 || src == nullptr) return false;
  static constexpr char HEX[] = "0123456789ABCDEF";
  size_t off = 0;
  for (size_t i = 0; src[i] != '\0'; ++i) {
    const unsigned char c = static_cast<unsigned char>(src[i]);
    const bool safe = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
    const bool encoded = c == '%' && std::isxdigit(static_cast<unsigned char>(src[i + 1])) &&
                         std::isxdigit(static_cast<unsigned char>(src[i + 2]));
    if (off + (safe ? 1 : 3) >= out_len) { out[0] = '\0'; return false; }
    if (safe) out[off++] = static_cast<char>(c);
    else if (encoded) { out[off++] = '%'; out[off++] = src[++i]; out[off++] = src[++i]; }
    else { out[off++] = '%'; out[off++] = HEX[c >> 4]; out[off++] = HEX[c & 0x0f]; }
  }
  out[off] = '\0';
  return true;
}

inline bool build_number_url(const char *host, uint16_t port, const char *entity, float value,
                             char *out, size_t out_len) {
  char encoded[145];
  if (!valid_host(host) || port == 0 || !encode_entity_once(entity, encoded, sizeof(encoded))) return false;
  const int written = std::snprintf(out, out_len, "http://%s:%u/number/%s/set?value=%.2f",
                                    host, static_cast<unsigned>(port), encoded, value);
  return written >= 0 && static_cast<size_t>(written) < out_len;
}

inline bool build_number_read_url(const char *host, uint16_t port, const char *entity,
                                  char *out, size_t out_len) {
  char encoded[145];
  if (!valid_host(host) || port == 0 || !encode_entity_once(entity, encoded, sizeof(encoded))) return false;
  const int written = std::snprintf(out, out_len, "http://%s:%u/number/%s",
                                    host, static_cast<unsigned>(port), encoded);
  return written >= 0 && static_cast<size_t>(written) < out_len;
}

inline bool build_climate_target_url(const char *host, uint16_t port, const char *entity,
                                     float target_c, char *out, size_t out_len) {
  char encoded[145];
  if (!valid_host(host) || port == 0 || !encode_entity_once(entity, encoded, sizeof(encoded)))
    return false;
  const int written =
      std::snprintf(out, out_len, "http://%s:%u/climate/%s/set?target_temperature=%.1f", host,
                    static_cast<unsigned>(port), encoded, target_c);
  return written >= 0 && static_cast<size_t>(written) < out_len;
}

inline bool build_climate_read_url(const char *host, uint16_t port, const char *entity, char *out,
                                   size_t out_len) {
  char encoded[145];
  if (!valid_host(host) || port == 0 || !encode_entity_once(entity, encoded, sizeof(encoded)))
    return false;
  const int written = std::snprintf(out, out_len, "http://%s:%u/climate/%s", host,
                                    static_cast<unsigned>(port), encoded);
  return written >= 0 && static_cast<size_t>(written) < out_len;
}

inline bool build_binary_sensor_url(const char *host, uint16_t port, const char *entity, char *out,
                                    size_t out_len) {
  char encoded[145];
  if (!valid_host(host) || port == 0 || !encode_entity_once(entity, encoded, sizeof(encoded)))
    return false;
  const int written = std::snprintf(out, out_len, "http://%s:%u/binary_sensor/%s", host,
                                    static_cast<unsigned>(port), encoded);
  return written >= 0 && static_cast<size_t>(written) < out_len;
}

// Optional override for non-ESPHome endpoints. Empty/null means use build_number_* above.
// Placeholders: {host} {port} {entity} (URL-encoded once) {value} (write only, %.2f).
inline bool valid_url_template(const char *tmpl, bool require_value_placeholder) {
  if (tmpl == nullptr || tmpl[0] == '\0') return true;
  size_t n = 0;
  for (const char *p = tmpl; *p != '\0'; ++p, ++n) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c > 0x7e || n >= 191) return false;
  }
  if (require_value_placeholder && std::strstr(tmpl, "{value}") == nullptr) return false;
  return true;
}

inline bool append_token_(char *out, size_t out_len, size_t *off, const char *token) {
  if (token == nullptr || off == nullptr) return false;
  const size_t len = std::strlen(token);
  if (*off + len >= out_len) return false;
  std::memcpy(out + *off, token, len);
  *off += len;
  return true;
}

inline bool expand_url_template(const char *tmpl, const char *host, uint16_t port,
                                const char *entity, float value, bool include_value,
                                char *out, size_t out_len) {
  if (tmpl == nullptr || tmpl[0] == '\0' || out == nullptr || out_len == 0) return false;
  char encoded[145];
  char port_buf[8];
  char value_buf[24];
  if (!encode_entity_once(entity, encoded, sizeof(encoded))) return false;
  std::snprintf(port_buf, sizeof(port_buf), "%u", static_cast<unsigned>(port));
  if (include_value)
    std::snprintf(value_buf, sizeof(value_buf), "%.2f", value);
  size_t off = 0;
  for (const char *p = tmpl; *p != '\0';) {
    if (*p != '{') {
      if (off + 1 >= out_len) { out[0] = '\0'; return false; }
      out[off++] = *p++;
      continue;
    }
    const char *end = std::strchr(p, '}');
    if (end == nullptr) { out[0] = '\0'; return false; }
    const size_t token_len = static_cast<size_t>(end - p + 1);
    const char *replacement = nullptr;
    if (token_len == 6 && std::strncmp(p, "{host}", 6) == 0) replacement = host;
    else if (token_len == 6 && std::strncmp(p, "{port}", 6) == 0) replacement = port_buf;
    else if (token_len == 8 && std::strncmp(p, "{entity}", 8) == 0) replacement = encoded;
    else if (token_len == 7 && std::strncmp(p, "{value}", 7) == 0) {
      if (!include_value) { out[0] = '\0'; return false; }
      replacement = value_buf;
    } else {
      out[0] = '\0';
      return false;
    }
    if (!append_token_(out, out_len, &off, replacement)) { out[0] = '\0'; return false; }
    p = end + 1;
  }
  out[off] = '\0';
  return off > 0;
}

}  // namespace esphome::lune_touch_coordinator::asgard_url
