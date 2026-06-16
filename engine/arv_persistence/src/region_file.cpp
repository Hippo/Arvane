#include "arv/persistence/region_file.hpp"

#include "atomic_file.hpp"

#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace arv::persistence {
namespace {

constexpr usize LightingBytesPerSection = voxel::ChunkVolume * sizeof(voxel::LightWord);
constexpr usize FluidBytesPerCell = sizeof(u32) + sizeof(u8) + sizeof(u8);
constexpr usize FluidBytesPerSection = voxel::ChunkVolume * FluidBytesPerCell;
constexpr std::string_view RegionDocumentFormat = "arvane.region";

[[nodiscard]] PersistenceError make_error(std::string message) {
  return PersistenceError{std::move(message)};
}

[[nodiscard]] Expected<u8, PersistenceError> bits_per_block_for_table_size(usize table_size) {
  if (table_size <= 16U) {
    return Expected<u8, PersistenceError>::success(4U);
  }
  if (table_size <= 256U) {
    return Expected<u8, PersistenceError>::success(8U);
  }
  if (table_size <= 4096U) {
    return Expected<u8, PersistenceError>::success(12U);
  }
  return Expected<u8, PersistenceError>::failure(
      make_error("block state table exceeds supported 12-bit region encoding"));
}

[[nodiscard]] std::vector<u8> pack_block_indices(std::span<const u16> indices, u8 bits_per_block) {
  std::vector<u8> bytes;
  bytes.reserve((indices.size() * bits_per_block + 7U) / 8U);

  u64 accumulator{};
  u32 bits_in_accumulator{};
  const u64 mask = (1ULL << bits_per_block) - 1ULL;
  for (const u16 index : indices) {
    accumulator |= (static_cast<u64>(index) & mask) << bits_in_accumulator;
    bits_in_accumulator += bits_per_block;
    while (bits_in_accumulator >= 8U) {
      bytes.push_back(static_cast<u8>(accumulator & 0xFFU));
      accumulator >>= 8U;
      bits_in_accumulator -= 8U;
    }
  }

  if (bits_in_accumulator > 0U) {
    bytes.push_back(static_cast<u8>(accumulator & 0xFFU));
  }

  return bytes;
}

[[nodiscard]] Expected<std::array<u16, voxel::ChunkVolume>, PersistenceError> unpack_block_indices(
    std::span<const u8> bytes,
    u8 bits_per_block) {
  std::array<u16, voxel::ChunkVolume> indices{};
  u64 accumulator{};
  u32 bits_in_accumulator{};
  usize byte_index{};
  const u64 mask = (1ULL << bits_per_block) - 1ULL;

  for (usize index = 0; index < voxel::ChunkVolume; ++index) {
    while (bits_in_accumulator < bits_per_block) {
      if (byte_index >= bytes.size()) {
        return Expected<std::array<u16, voxel::ChunkVolume>, PersistenceError>::failure(
            make_error("packed block data ended early"));
      }
      accumulator |= static_cast<u64>(bytes[byte_index++]) << bits_in_accumulator;
      bits_in_accumulator += 8U;
    }

    indices[index] = static_cast<u16>(accumulator & mask);
    accumulator >>= bits_per_block;
    bits_in_accumulator -= bits_per_block;
  }

  return Expected<std::array<u16, voxel::ChunkVolume>, PersistenceError>::success(indices);
}

void append_u32_le(std::vector<u8>& bytes, u32 value) {
  for (usize index = 0; index < sizeof(u32); ++index) {
    bytes.push_back(static_cast<u8>((value >> (index * 8U)) & 0xFFU));
  }
}

[[nodiscard]] Expected<u32, PersistenceError> read_u32_le(
    std::span<const u8> bytes,
    usize& offset,
    std::string_view purpose) {
  if (offset + sizeof(u32) > bytes.size()) {
    return Expected<u32, PersistenceError>::failure(
        make_error(std::string{purpose} + " is truncated"));
  }
  u32 value{};
  for (usize index = 0; index < sizeof(u32); ++index) {
    value |= static_cast<u32>(bytes[offset + index]) << (index * 8U);
  }
  offset += sizeof(u32);
  return Expected<u32, PersistenceError>::success(value);
}

[[nodiscard]] std::vector<u8> encode_fluid_cells(
    const std::array<voxel::FluidCell, voxel::ChunkVolume>& fluids) {
  std::vector<u8> bytes;
  bytes.reserve(FluidBytesPerSection);
  for (const voxel::FluidCell fluid : fluids) {
    append_u32_le(bytes, fluid.type.value);
    bytes.push_back(fluid.level);
    bytes.push_back(static_cast<u8>(fluid.flags));
  }
  return bytes;
}

[[nodiscard]] Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError>
decode_fluid_cells(std::span<const u8> bytes) {
  if (bytes.size() != FluidBytesPerSection) {
    return Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError>::failure(
        make_error("fluid data section has unexpected size"));
  }

  std::array<voxel::FluidCell, voxel::ChunkVolume> fluids{};
  usize offset{};
  for (voxel::FluidCell& fluid : fluids) {
    auto type = read_u32_le(bytes, offset, "fluid cell type");
    if (!type || offset + 2U > bytes.size()) {
      return Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError>::failure(
          make_error("fluid data section is truncated"));
    }
    const u8 level = bytes[offset++];
    const u8 flags = bytes[offset++];
    fluid = {
        voxel::FluidTypeID{type.value()},
        level,
        static_cast<voxel::FluidFlags>(flags)};
  }
  return Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError>::success(fluids);
}

[[nodiscard]] std::string field_path(std::string_view parent, std::string_view key) {
  std::string path{parent};
  path.push_back('.');
  path += key;
  return path;
}

[[nodiscard]] Expected<const io::DataValue*, PersistenceError> required_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  if (object.kind() != io::DataKind::Object) {
    return Expected<const io::DataValue*, PersistenceError>::failure(
        make_error(std::string{parent_path} + " must be an object"));
  }
  const io::DataValue* value = object.get(key);
  if (value == nullptr) {
    return Expected<const io::DataValue*, PersistenceError>::failure(
        make_error("missing region field " + field_path(parent_path, key)));
  }
  return Expected<const io::DataValue*, PersistenceError>::success(value);
}

[[nodiscard]] Expected<i64, PersistenceError> integer_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = required_field(object, key, parent_path);
  if (!value) {
    return Expected<i64, PersistenceError>::failure(value.error());
  }
  if (value.value()->kind() == io::DataKind::I64) {
    return Expected<i64, PersistenceError>::success(value.value()->as_i64_or(0));
  }
  if (value.value()->kind() == io::DataKind::U64) {
    const u64 raw = value.value()->as_u64_or(0);
    if (raw <= static_cast<u64>(std::numeric_limits<i64>::max())) {
      return Expected<i64, PersistenceError>::success(static_cast<i64>(raw));
    }
  }
  return Expected<i64, PersistenceError>::failure(
      make_error(field_path(parent_path, key) + " must be an integer"));
}

[[nodiscard]] Expected<i32, PersistenceError> i32_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = integer_field(object, key, parent_path);
  if (!value) {
    return Expected<i32, PersistenceError>::failure(value.error());
  }
  if (value.value() < static_cast<i64>(std::numeric_limits<i32>::min()) ||
      value.value() > static_cast<i64>(std::numeric_limits<i32>::max())) {
    return Expected<i32, PersistenceError>::failure(
        make_error(field_path(parent_path, key) + " is out of i32 range"));
  }
  return Expected<i32, PersistenceError>::success(static_cast<i32>(value.value()));
}

[[nodiscard]] Expected<u8, PersistenceError> u8_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = integer_field(object, key, parent_path);
  if (!value) {
    return Expected<u8, PersistenceError>::failure(value.error());
  }
  if (value.value() < 0 ||
      value.value() > static_cast<i64>(std::numeric_limits<u8>::max())) {
    return Expected<u8, PersistenceError>::failure(
        make_error(field_path(parent_path, key) + " is out of u8 range"));
  }
  return Expected<u8, PersistenceError>::success(static_cast<u8>(value.value()));
}

[[nodiscard]] Expected<u32, PersistenceError> u32_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = integer_field(object, key, parent_path);
  if (!value) {
    return Expected<u32, PersistenceError>::failure(value.error());
  }
  if (value.value() < 0 ||
      value.value() > static_cast<i64>(std::numeric_limits<u32>::max())) {
    return Expected<u32, PersistenceError>::failure(
        make_error(field_path(parent_path, key) + " is out of u32 range"));
  }
  return Expected<u32, PersistenceError>::success(static_cast<u32>(value.value()));
}

[[nodiscard]] Expected<bool, PersistenceError> bool_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = required_field(object, key, parent_path);
  if (!value) {
    return Expected<bool, PersistenceError>::failure(value.error());
  }
  if (value.value()->kind() != io::DataKind::Bool) {
    return Expected<bool, PersistenceError>::failure(
        make_error(field_path(parent_path, key) + " must be a bool"));
  }
  return Expected<bool, PersistenceError>::success(value.value()->as_bool_or(false));
}

[[nodiscard]] Expected<std::string, PersistenceError> string_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = required_field(object, key, parent_path);
  if (!value) {
    return Expected<std::string, PersistenceError>::failure(value.error());
  }
  if (value.value()->kind() != io::DataKind::String) {
    return Expected<std::string, PersistenceError>::failure(
        make_error(field_path(parent_path, key) + " must be a string"));
  }
  return Expected<std::string, PersistenceError>::success(
      std::string{value.value()->as_string_or({})});
}

[[nodiscard]] Expected<std::span<const u8>, PersistenceError> bytes_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = required_field(object, key, parent_path);
  if (!value) {
    return Expected<std::span<const u8>, PersistenceError>::failure(value.error());
  }
  const io::DataValue::Bytes* bytes = value.value()->bytes_if();
  if (bytes == nullptr) {
    return Expected<std::span<const u8>, PersistenceError>::failure(
        make_error(field_path(parent_path, key) + " must be bytes"));
  }
  return Expected<std::span<const u8>, PersistenceError>::success(std::span<const u8>{*bytes});
}

[[nodiscard]] Expected<const io::DataValue::Array*, PersistenceError> array_field(
    const io::DataValue& object,
    std::string_view key,
    std::string_view parent_path) {
  auto value = required_field(object, key, parent_path);
  if (!value) {
    return Expected<const io::DataValue::Array*, PersistenceError>::failure(value.error());
  }
  const io::DataValue::Array* array = value.value()->array_if();
  if (array == nullptr) {
    return Expected<const io::DataValue::Array*, PersistenceError>::failure(
        make_error(field_path(parent_path, key) + " must be an array"));
  }
  return Expected<const io::DataValue::Array*, PersistenceError>::success(array);
}

[[nodiscard]] io::DataValue encode_item_stack(player::ItemStack stack) {
  io::DataValue value = io::DataValue::object();
  value.set("item", io::DataValue{static_cast<u64>(stack.item.value)});
  value.set("count", io::DataValue{static_cast<u64>(stack.count)});
  if (stack.data.has_value()) {
    io::DataValue data = io::DataValue::object();
    data.set("schema_version", io::DataValue{static_cast<u64>(stack.data->schema_version)});
    data.set("root", stack.data->root);
    value.set("data", std::move(data));
  }
  return value;
}

[[nodiscard]] Expected<player::ItemStack, PersistenceError> decode_item_stack(
    const io::DataValue& value,
    std::string_view path) {
  if (value.kind() != io::DataKind::Object) {
    return Expected<player::ItemStack, PersistenceError>::failure(
        make_error(std::string{path} + " must be an object"));
  }
  auto item = u32_field(value, "item", path);
  auto count = u32_field(value, "count", path);
  if (!item) {
    return Expected<player::ItemStack, PersistenceError>::failure(item.error());
  }
  if (!count) {
    return Expected<player::ItemStack, PersistenceError>::failure(count.error());
  }
  if (count.value() == 0U ||
      count.value() > static_cast<u32>(std::numeric_limits<u16>::max())) {
    return Expected<player::ItemStack, PersistenceError>::failure(
        make_error(field_path(path, "count") + " is out of item stack range"));
  }

  player::ItemStack stack;
  stack.item = player::ItemID{item.value()};
  stack.count = static_cast<u16>(count.value());
  if (const io::DataValue* data = value.get("data"); data != nullptr) {
    if (data->kind() != io::DataKind::Object) {
      return Expected<player::ItemStack, PersistenceError>::failure(
          make_error(field_path(path, "data") + " must be an object"));
    }
    const std::string data_path = field_path(path, "data");
    auto schema_version = u32_field(*data, "schema_version", data_path);
    if (!schema_version) {
      return Expected<player::ItemStack, PersistenceError>::failure(schema_version.error());
    }
    const io::DataValue* root = data->get("root");
    if (root == nullptr) {
      return Expected<player::ItemStack, PersistenceError>::failure(
          make_error("missing region field " + field_path(data_path, "root")));
    }
    stack.data = io::DataDocument{schema_version.value(), *root};
  }
  return Expected<player::ItemStack, PersistenceError>::success(std::move(stack));
}

[[nodiscard]] io::DataValue encode_block_containers(
    std::span<const world::SavedBlockContainer> containers) {
  io::DataValue values = io::DataValue::array();
  for (const world::SavedBlockContainer& container : containers) {
    io::DataValue node = io::DataValue::object();
    node.set("x", io::DataValue{static_cast<i64>(container.local.value.x)});
    node.set("y", io::DataValue{static_cast<i64>(container.local.value.y)});
    node.set("z", io::DataValue{static_cast<i64>(container.local.value.z)});
    node.set("container_type", io::DataValue{container.container_type});
    node.set("slot_count", io::DataValue{static_cast<u64>(container.slot_count)});

    io::DataValue slots = io::DataValue::array();
    for (const world::BlockContainerSlotRecord& slot : container.slots) {
      io::DataValue slot_node = io::DataValue::object();
      slot_node.set("slot", io::DataValue{static_cast<u64>(slot.slot)});
      slot_node.set("stack", encode_item_stack(slot.stack));
      slots.push(std::move(slot_node));
    }
    node.set("slots", std::move(slots));
    values.push(std::move(node));
  }
  return values;
}

[[nodiscard]] Expected<std::vector<world::SavedBlockContainer>, PersistenceError>
decode_block_containers(
    const io::DataValue& chunk_node,
    std::string_view chunk_path) {
  std::vector<world::SavedBlockContainer> containers;
  const io::DataValue* containers_node = chunk_node.get("block_containers");
  if (containers_node == nullptr) {
    return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::success(
        std::move(containers));
  }
  const io::DataValue::Array* array = containers_node->array_if();
  if (array == nullptr) {
    return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(
        make_error(field_path(chunk_path, "block_containers") + " must be an array"));
  }

  containers.reserve(array->size());
  for (usize index = 0; index < array->size(); ++index) {
    const io::DataValue& node = (*array)[index];
    const std::string container_path =
        field_path(chunk_path, "block_containers") + "[" + std::to_string(index) + "]";
    if (node.kind() != io::DataKind::Object) {
      return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(
          make_error(container_path + " must be an object"));
    }
    auto x = i32_field(node, "x", container_path);
    auto y = i32_field(node, "y", container_path);
    auto z = i32_field(node, "z", container_path);
    auto container_type = string_field(node, "container_type", container_path);
    auto slot_count = u32_field(node, "slot_count", container_path);
    auto slots = array_field(node, "slots", container_path);
    if (!x) {
      return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(x.error());
    }
    if (!y) {
      return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(y.error());
    }
    if (!z) {
      return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(z.error());
    }
    if (!container_type) {
      return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(container_type.error());
    }
    if (!slot_count) {
      return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(slot_count.error());
    }
    if (!slots) {
      return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(slots.error());
    }

    world::SavedBlockContainer container;
    container.local = {{x.value(), y.value(), z.value()}};
    container.container_type = std::move(container_type.value());
    container.slot_count = slot_count.value();
    container.slots.reserve(slots.value()->size());
    for (usize slot_index = 0; slot_index < slots.value()->size(); ++slot_index) {
      const io::DataValue& slot_node = (*slots.value())[slot_index];
      const std::string slot_path =
          container_path + ".slots[" + std::to_string(slot_index) + "]";
      if (slot_node.kind() != io::DataKind::Object) {
        return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(
            make_error(slot_path + " must be an object"));
      }
      auto slot = u32_field(slot_node, "slot", slot_path);
      auto stack_field = required_field(slot_node, "stack", slot_path);
      if (!slot) {
        return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(slot.error());
      }
      if (!stack_field) {
        return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(stack_field.error());
      }
      auto stack = decode_item_stack(*stack_field.value(), field_path(slot_path, "stack"));
      if (!stack) {
        return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::failure(stack.error());
      }
      container.slots.push_back({slot.value(), std::move(stack.value())});
    }
    containers.push_back(std::move(container));
  }
  return Expected<std::vector<world::SavedBlockContainer>, PersistenceError>::success(
      std::move(containers));
}

[[nodiscard]] Expected<io::DataDocument, PersistenceError> decode_region_document(
    std::span<const u8> bytes) {
  auto decoded = io::decode_binary(bytes);
  if (!decoded) {
    return Expected<io::DataDocument, PersistenceError>::failure(
        make_error("failed to decode region binary document: " + decoded.error().describe()));
  }
  return Expected<io::DataDocument, PersistenceError>::success(std::move(decoded.value()));
}

[[nodiscard]] Expected<RegionFileHeader, PersistenceError> header_from_document(
    std::span<const u8> bytes,
    const io::DataDocument& document) {
  if (document.root.kind() != io::DataKind::Object) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("region document root must be an object"));
  }

  auto format = string_field(document.root, "format", "root");
  if (!format) {
    return Expected<RegionFileHeader, PersistenceError>::failure(format.error());
  }
  if (format.value() != RegionDocumentFormat) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("region document format is not arvane.region"));
  }

  auto region_node = required_field(document.root, "region", "root");
  if (!region_node) {
    return Expected<RegionFileHeader, PersistenceError>::failure(region_node.error());
  }
  auto region_x = i32_field(*region_node.value(), "x", "root.region");
  auto region_z = i32_field(*region_node.value(), "z", "root.region");
  if (!region_x) {
    return Expected<RegionFileHeader, PersistenceError>::failure(region_x.error());
  }
  if (!region_z) {
    return Expected<RegionFileHeader, PersistenceError>::failure(region_z.error());
  }

  RegionFileHeader header;
  header.format_version = CurrentRegionFormatVersion;
  if (const io::DataValue* version = document.root.get("version");
      version != nullptr &&
      (version->kind() == io::DataKind::I64 || version->kind() == io::DataKind::U64)) {
    const i64 raw_version = version->as_i64_or(CurrentRegionFormatVersion);
    if (raw_version < MinimumReadableRegionFormatVersion ||
        raw_version > CurrentRegionFormatVersion) {
      return Expected<RegionFileHeader, PersistenceError>::failure(
          make_error("unsupported region format version"));
    }
    header.format_version = static_cast<u16>(raw_version);
  }
  if (bytes.size() > std::numeric_limits<u32>::max()) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("region file is too large"));
  }
  header.header_size = 0U;
  header.region = {region_x.value(), region_z.value()};
  header.chunk_count = RegionChunkSlotCount;
  header.location_table_offset = 0U;
  header.metadata_table_offset = 0U;
  header.region_tables_offset = 0U;
  header.data_start_offset = 0U;
  header.file_size = static_cast<u32>(bytes.size());
  header.flags = 0U;
  return Expected<RegionFileHeader, PersistenceError>::success(header);
}

Expected<void, PersistenceError> write_binary_atomic(
    const std::filesystem::path& path,
    std::span<const u8> bytes) {
  std::error_code error;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return Expected<void, PersistenceError>::failure(
          make_error("failed to create directory for region file: " + error.message()));
    }
  }

  const std::filesystem::path temp_path = detail::temporary_path_for(path);
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Expected<void, PersistenceError>::failure(
          make_error("failed to open temporary region file for write"));
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      return Expected<void, PersistenceError>::failure(
          make_error("failed to write region file bytes"));
    }
    output.flush();
    if (!output) {
      return Expected<void, PersistenceError>::failure(
          make_error("failed to flush region file bytes"));
    }
  }

  std::filesystem::rename(temp_path, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temp_path, path, error);
  }
  if (error) {
    return Expected<void, PersistenceError>::failure(
        make_error("failed to finalize region file write: " + error.message()));
  }

  return Expected<void, PersistenceError>::success();
}

} // namespace

Expected<RegionFileHeader, PersistenceError> parse_region_header(std::span<const u8> bytes) {
  auto document = decode_region_document(bytes);
  if (!document) {
    return Expected<RegionFileHeader, PersistenceError>::failure(document.error());
  }
  return header_from_document(bytes, document.value());
}

Expected<std::vector<u8>, PersistenceError> encode_region_file(
    RegionCoord region,
    std::span<const SavedChunkSection> chunks,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y) {
  (void)min_chunk_y;

  HashMap<std::string, u16> block_state_indices;
  std::vector<std::string> block_state_table;
  block_state_table.push_back(std::string{voxel::AirBlockName});
  block_state_indices.emplace(std::string{voxel::AirBlockName}, 0U);

  std::vector<const SavedChunkSection*> sorted_chunks;
  sorted_chunks.reserve(chunks.size());
  for (const SavedChunkSection& chunk : chunks) {
    if (chunk_column_to_region(chunk_to_column(chunk.coord)) != region) {
      return Expected<std::vector<u8>, PersistenceError>::failure(
          make_error("chunk does not belong to the requested region file"));
    }
    sorted_chunks.push_back(&chunk);

    for (const voxel::BlockVariantID variant : chunk.blocks) {
      const std::string name{registry.variant_name(variant)};
      if (block_state_indices.find(name) != block_state_indices.end()) {
        continue;
      }
      const u16 next_index = static_cast<u16>(block_state_table.size());
      block_state_table.push_back(name);
      block_state_indices.emplace(name, next_index);
    }
  }
  std::sort(
      sorted_chunks.begin(),
      sorted_chunks.end(),
      [](const SavedChunkSection* lhs, const SavedChunkSection* rhs) {
        if (lhs->coord.value.x != rhs->coord.value.x) {
          return lhs->coord.value.x < rhs->coord.value.x;
        }
        if (lhs->coord.value.z != rhs->coord.value.z) {
          return lhs->coord.value.z < rhs->coord.value.z;
        }
        return lhs->coord.value.y < rhs->coord.value.y;
      });

  auto bits_result = bits_per_block_for_table_size(block_state_table.size());
  if (!bits_result) {
    return Expected<std::vector<u8>, PersistenceError>::failure(bits_result.error());
  }
  const u8 bits_per_block = bits_result.value();

  io::DataValue root = io::DataValue::object();
  root.set("format", io::DataValue{RegionDocumentFormat});
  root.set("version", io::DataValue{static_cast<u64>(CurrentRegionFormatVersion)});
  io::DataValue region_node = io::DataValue::object();
  region_node.set("x", io::DataValue{static_cast<i64>(region.x)});
  region_node.set("z", io::DataValue{static_cast<i64>(region.z)});
  root.set("region", std::move(region_node));

  io::DataValue block_states = io::DataValue::array();
  for (const std::string& block_state : block_state_table) {
    block_states.push(io::DataValue{block_state});
  }
  root.set("block_states", std::move(block_states));

  io::DataValue chunk_values = io::DataValue::array();
  for (const SavedChunkSection* section : sorted_chunks) {
    std::array<u16, voxel::ChunkVolume> indices{};
    for (usize index = 0; index < voxel::ChunkVolume; ++index) {
      const std::string key{registry.variant_name(section->blocks[index])};
      indices[index] = block_state_indices[key];
    }
    const std::vector<u8> packed_blocks = pack_block_indices(indices, bits_per_block);
    const std::vector<u8> packed_fluids = encode_fluid_cells(section->fluids);

    io::DataValue chunk = io::DataValue::object();
    chunk.set("x", io::DataValue{static_cast<i64>(section->coord.value.x)});
    chunk.set("y", io::DataValue{static_cast<i64>(section->coord.value.y)});
    chunk.set("z", io::DataValue{static_cast<i64>(section->coord.value.z)});
    chunk.set("lighting_valid", io::DataValue{section->lighting_valid});
    chunk.set("bits_per_block", io::DataValue{static_cast<u64>(bits_per_block)});
    chunk.set("blocks", io::DataValue::bytes(packed_blocks));
    chunk.set(
        "lights",
        io::DataValue::bytes(
            {reinterpret_cast<const u8*>(section->lights.data()), LightingBytesPerSection}));
    chunk.set("fluids", io::DataValue::bytes(packed_fluids));
    chunk.set("block_containers", encode_block_containers(section->block_containers));
    chunk_values.push(std::move(chunk));
  }
  root.set("chunks", std::move(chunk_values));

  io::DataDocument document{CurrentRegionFormatVersion, std::move(root)};
  auto encoded = io::encode_binary(document);
  if (!encoded) {
    return Expected<std::vector<u8>, PersistenceError>::failure(
        make_error("failed to encode region binary document: " + encoded.error().describe()));
  }
  return Expected<std::vector<u8>, PersistenceError>::success(std::move(encoded.value()));
}

Expected<DecodedRegionFile, PersistenceError> decode_region_file(
    std::span<const u8> bytes,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y) {
  (void)min_chunk_y;

  auto document = decode_region_document(bytes);
  if (!document) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(document.error());
  }
  auto header = header_from_document(bytes, document.value());
  if (!header) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(header.error());
  }

  auto block_states_result = array_field(document.value().root, "block_states", "root");
  if (!block_states_result) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(block_states_result.error());
  }

  std::vector<std::string> block_state_table;
  block_state_table.reserve(block_states_result.value()->size());
  for (const io::DataValue& entry : *block_states_result.value()) {
    if (entry.kind() != io::DataKind::String) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(
          make_error("root.block_states entries must be strings"));
    }
    block_state_table.emplace_back(entry.as_string_or({}));
  }
  if (block_state_table.empty()) {
    block_state_table.push_back(std::string{voxel::AirBlockName});
  }

  auto chunks_result = array_field(document.value().root, "chunks", "root");
  if (!chunks_result) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(chunks_result.error());
  }

  DecodedRegionFile decoded;
  decoded.header = header.value();
  decoded.chunks.reserve(chunks_result.value()->size());

  for (usize chunk_index = 0; chunk_index < chunks_result.value()->size(); ++chunk_index) {
    const io::DataValue& chunk_node = (*chunks_result.value())[chunk_index];
    const std::string chunk_path = "root.chunks[" + std::to_string(chunk_index) + "]";
    if (chunk_node.kind() != io::DataKind::Object) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(
          make_error(chunk_path + " must be an object"));
    }

    auto chunk_x = i32_field(chunk_node, "x", chunk_path);
    auto chunk_y = i32_field(chunk_node, "y", chunk_path);
    auto chunk_z = i32_field(chunk_node, "z", chunk_path);
    auto lighting_valid = bool_field(chunk_node, "lighting_valid", chunk_path);
    auto bits_per_block = u8_field(chunk_node, "bits_per_block", chunk_path);
    auto block_data = bytes_field(chunk_node, "blocks", chunk_path);
    auto lighting_data = bytes_field(chunk_node, "lights", chunk_path);
    auto fluid_data = bytes_field(chunk_node, "fluids", chunk_path);
    auto block_containers = decode_block_containers(chunk_node, chunk_path);
    if (!chunk_x) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(chunk_x.error());
    }
    if (!chunk_y) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(chunk_y.error());
    }
    if (!chunk_z) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(chunk_z.error());
    }
    if (!lighting_valid) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(lighting_valid.error());
    }
    if (!bits_per_block) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(bits_per_block.error());
    }
    if (!block_data) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(block_data.error());
    }
    if (!lighting_data) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(lighting_data.error());
    }
    if (!fluid_data) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(fluid_data.error());
    }
    if (!block_containers) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(block_containers.error());
    }
    if (lighting_data.value().size() != LightingBytesPerSection) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(
          make_error(chunk_path + ".lights has unexpected size"));
    }

    auto indices = unpack_block_indices(block_data.value(), bits_per_block.value());
    if (!indices) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(indices.error());
    }
    auto fluids = decode_fluid_cells(fluid_data.value());
    if (!fluids) {
      return Expected<DecodedRegionFile, PersistenceError>::failure(fluids.error());
    }

    SavedChunkSection section;
    section.coord = {{chunk_x.value(), chunk_y.value(), chunk_z.value()}};
    section.lighting_valid = lighting_valid.value();
    for (usize index = 0; index < voxel::ChunkVolume; ++index) {
      const u16 block_index = indices.value()[index];
      if (block_index >= block_state_table.size()) {
        section.blocks[index] = voxel::AirBlockVariantID;
        continue;
      }

      const auto resolved = registry.find_variant(block_state_table[block_index]);
      // TODO(arvane): Replace missing saved block states with a documented
      // arvane:missing_block fallback once that registry entry exists.
      section.blocks[index] = resolved.value_or(voxel::AirBlockVariantID);
    }
    std::memcpy(section.lights.data(), lighting_data.value().data(), LightingBytesPerSection);
    section.fluids = std::move(fluids.value());
    section.block_containers = std::move(block_containers.value());
    decoded.chunks.push_back(std::move(section));
  }

  return Expected<DecodedRegionFile, PersistenceError>::success(std::move(decoded));
}

Expected<void, PersistenceError> write_region_file(
    const std::filesystem::path& path,
    RegionCoord region,
    std::span<const SavedChunkSection> chunks,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y) {
  auto encoded = encode_region_file(region, chunks, registry, min_chunk_y);
  if (!encoded) {
    return Expected<void, PersistenceError>::failure(encoded.error());
  }
  return write_binary_atomic(path, encoded.value());
}

Expected<DecodedRegionFile, PersistenceError> read_region_file(
    const std::filesystem::path& path,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(
        make_error("failed to open region file for read"));
  }

  input.seekg(0, std::ios::end);
  const std::streamsize size = input.tellg();
  input.seekg(0, std::ios::beg);
  if (size < 0) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(
        make_error("failed to determine region file size"));
  }

  std::vector<u8> bytes(static_cast<usize>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(
        make_error("failed to read region file bytes"));
  }

  return decode_region_file(bytes, registry, min_chunk_y);
}

} // namespace arv::persistence
