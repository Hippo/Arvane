#include "arv/io/data_codecs.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace arv::io {
namespace {

constexpr std::string_view BinaryMagic = "ARVDATA1";
constexpr u16 BinaryFormatVersion = 1U;

DataCodecError error_at(std::string path, std::string message) {
  return {std::move(path), std::move(message)};
}

class BinaryWriter {
public:
  template<typename T, bool IsEnum = std::is_enum_v<T>>
  struct RawType {
    using Type = T;
  };

  template<typename T>
  struct RawType<T, true> {
    using Type = std::underlying_type_t<T>;
  };

  template<typename T>
  void write(T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using Raw = typename RawType<T>::Type;
    const Raw raw = static_cast<Raw>(value);
    const usize offset = bytes_.size();
    bytes_.resize(offset + sizeof(Raw));
    for (usize i = 0U; i < sizeof(Raw); ++i) {
      bytes_[offset + i] =
          static_cast<u8>((static_cast<std::make_unsigned_t<Raw>>(raw) >> (i * 8U)) & 0xFFU);
    }
  }

  void write_f64(f64 value) {
    u64 raw{};
    std::memcpy(&raw, &value, sizeof(raw));
    write(raw);
  }

  void write_bytes(std::span<const u8> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  std::vector<u8> take() && {
    return std::move(bytes_);
  }

private:
  std::vector<u8> bytes_{};
};

class BinaryReader {
public:
  explicit BinaryReader(std::span<const u8> bytes) : bytes_(bytes) {
  }

  template<typename T, bool IsEnum = std::is_enum_v<T>>
  struct RawType {
    using Type = T;
  };

  template<typename T>
  struct RawType<T, true> {
    using Type = std::underlying_type_t<T>;
  };

  template<typename T>
  Expected<T, DataCodecError> read(std::string path) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using Raw = typename RawType<T>::Type;
    if (remaining() < sizeof(Raw)) {
      return Expected<T, DataCodecError>::failure(error_at(std::move(path), "unexpected end of data"));
    }
    std::make_unsigned_t<Raw> raw{};
    for (usize i = 0U; i < sizeof(Raw); ++i) {
      raw |= static_cast<std::make_unsigned_t<Raw>>(bytes_[offset_ + i]) << (i * 8U);
    }
    offset_ += sizeof(Raw);
    return Expected<T, DataCodecError>::success(static_cast<T>(static_cast<Raw>(raw)));
  }

  Expected<f64, DataCodecError> read_f64(std::string path) {
    auto raw = read<u64>(std::move(path));
    if (!raw) {
      return Expected<f64, DataCodecError>::failure(raw.error());
    }
    f64 value{};
    const u64 bits = raw.value();
    std::memcpy(&value, &bits, sizeof(value));
    return Expected<f64, DataCodecError>::success(value);
  }

  Expected<std::vector<u8>, DataCodecError> read_bytes(usize count, std::string path) {
    if (remaining() < count) {
      return Expected<std::vector<u8>, DataCodecError>::failure(
          error_at(std::move(path), "unexpected end of data"));
    }
    std::vector<u8> bytes(bytes_.begin() + static_cast<isize>(offset_),
                          bytes_.begin() + static_cast<isize>(offset_ + count));
    offset_ += count;
    return Expected<std::vector<u8>, DataCodecError>::success(std::move(bytes));
  }

  [[nodiscard]] usize remaining() const noexcept {
    return bytes_.size() - offset_;
  }

private:
  std::span<const u8> bytes_;
  usize offset_{};
};

void write_string(BinaryWriter& writer, std::string_view text) {
  writer.write<u32>(static_cast<u32>(text.size()));
  writer.write_bytes(std::span{
      reinterpret_cast<const u8*>(text.data()),
      text.size()});
}

void write_value(BinaryWriter& writer, const DataValue& value) {
  writer.write<DataKind>(value.kind());
  switch (value.kind()) {
    case DataKind::Null:
      break;
    case DataKind::Bool:
      writer.write<u8>(value.as_bool_or(false) ? 1U : 0U);
      break;
    case DataKind::I64:
      writer.write<i64>(value.as_i64_or(0));
      break;
    case DataKind::U64:
      writer.write<u64>(value.as_u64_or(0));
      break;
    case DataKind::F64:
      writer.write_f64(value.as_f64_or(0.0));
      break;
    case DataKind::String:
      write_string(writer, value.as_string_or({}));
      break;
    case DataKind::Bytes: {
      const auto* bytes = value.bytes_if();
      const usize count = bytes == nullptr ? 0U : bytes->size();
      writer.write<u32>(static_cast<u32>(count));
      if (bytes != nullptr) {
        writer.write_bytes(*bytes);
      }
      break;
    }
    case DataKind::Array: {
      const auto* array = value.array_if();
      const usize count = array == nullptr ? 0U : array->size();
      writer.write<u32>(static_cast<u32>(count));
      if (array != nullptr) {
        for (const DataValue& child : *array) {
          write_value(writer, child);
        }
      }
      break;
    }
    case DataKind::Object: {
      const auto* object = value.object_if();
      const usize count = object == nullptr ? 0U : object->size();
      writer.write<u32>(static_cast<u32>(count));
      if (object != nullptr) {
        for (const auto& [key, child] : *object) {
          write_string(writer, key);
          write_value(writer, child);
        }
      }
      break;
    }
  }
}

DataDocumentResult read_value(BinaryReader& reader, std::string path) {
  auto kind_result = reader.read<DataKind>(path);
  if (!kind_result) {
    return DataDocumentResult::failure(kind_result.error());
  }
  switch (kind_result.value()) {
    case DataKind::Null:
      return DataDocumentResult::success({0U, nullptr});
    case DataKind::Bool: {
      auto raw = reader.read<u8>(path);
      if (!raw) {
        return DataDocumentResult::failure(raw.error());
      }
      if (raw.value() > 1U) {
        return DataDocumentResult::failure(error_at(std::move(path), "invalid bool encoding"));
      }
      return DataDocumentResult::success({0U, raw.value() == 1U});
    }
    case DataKind::I64: {
      auto value = reader.read<i64>(path);
      return value ? DataDocumentResult::success({0U, value.value()})
                   : DataDocumentResult::failure(value.error());
    }
    case DataKind::U64: {
      auto value = reader.read<u64>(path);
      return value ? DataDocumentResult::success({0U, value.value()})
                   : DataDocumentResult::failure(value.error());
    }
    case DataKind::F64: {
      auto value = reader.read_f64(path);
      return value ? DataDocumentResult::success({0U, value.value()})
                   : DataDocumentResult::failure(value.error());
    }
    case DataKind::String: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      auto bytes = reader.read_bytes(size.value(), path);
      if (!bytes) {
        return DataDocumentResult::failure(bytes.error());
      }
      return DataDocumentResult::success({
          0U,
          std::string{reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()}});
    }
    case DataKind::Bytes: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      auto bytes = reader.read_bytes(size.value(), path);
      return bytes ? DataDocumentResult::success({0U, DataValue{std::move(bytes.value())}})
                   : DataDocumentResult::failure(bytes.error());
    }
    case DataKind::Array: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      DataValue::Array array;
      array.reserve(size.value());
      for (u32 index = 0U; index < size.value(); ++index) {
        auto child = read_value(reader, path + "[" + std::to_string(index) + "]");
        if (!child) {
          return child;
        }
        array.push_back(std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(array)}});
    }
    case DataKind::Object: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      DataValue::Object object;
      for (u32 index = 0U; index < size.value(); ++index) {
        auto key_size = reader.read<u32>(path);
        if (!key_size) {
          return DataDocumentResult::failure(key_size.error());
        }
        auto key_bytes = reader.read_bytes(key_size.value(), path);
        if (!key_bytes) {
          return DataDocumentResult::failure(key_bytes.error());
        }
        std::string key{
            reinterpret_cast<const char*>(key_bytes.value().data()),
            key_bytes.value().size()};
        auto child = read_value(reader, path + "." + key);
        if (!child) {
          return child;
        }
        object.emplace(std::move(key), std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(object)}});
    }
  }
  return DataDocumentResult::failure(error_at(std::move(path), "unknown data kind"));
}

} // namespace

DataBytesResult encode_binary(const DataDocument& document) {
  BinaryWriter writer;
  writer.write_bytes(std::span{
      reinterpret_cast<const u8*>(BinaryMagic.data()),
      BinaryMagic.size()});
  writer.write<u16>(BinaryFormatVersion);
  writer.write<u32>(document.schema_version);
  write_value(writer, document.root);
  return DataBytesResult::success(std::move(writer).take());
}

DataDocumentResult decode_binary(std::span<const u8> bytes) {
  BinaryReader reader{bytes};
  auto magic = reader.read_bytes(BinaryMagic.size(), "root.magic");
  if (!magic) {
    return DataDocumentResult::failure(magic.error());
  }
  if (std::string_view{reinterpret_cast<const char*>(magic.value().data()), magic.value().size()} !=
      BinaryMagic) {
    return DataDocumentResult::failure(error_at("root.magic", "invalid data document magic"));
  }
  auto version = reader.read<u16>("root.format_version");
  if (!version) {
    return DataDocumentResult::failure(version.error());
  }
  if (version.value() != BinaryFormatVersion) {
    return DataDocumentResult::failure(
        error_at("root.format_version", "unsupported data document format version"));
  }
  auto schema_version = reader.read<u32>("root.schema_version");
  if (!schema_version) {
    return DataDocumentResult::failure(schema_version.error());
  }
  auto document = read_value(reader, "root.data");
  if (!document) {
    return document;
  }
  if (reader.remaining() != 0U) {
    return DataDocumentResult::failure(error_at("root", "trailing bytes after data document"));
  }
  document.value().schema_version = schema_version.value();
  return document;
}

} // namespace arv::io
