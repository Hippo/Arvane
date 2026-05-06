#include "arv/persistence/region_file.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace arv::persistence {
namespace {

constexpr std::array<char, 4> RegionMagic{'A', 'R', 'V', 'R'};
constexpr u16 BlockStateTableType = 1;
constexpr u8 ChunkMetadataVersion = 1;
constexpr u8 ChunkFlagLightingValid = 1U << 2U;
constexpr usize ChunkLocationEntrySize = 8U;
constexpr usize ChunkMetadataEntrySize = 8U;
constexpr usize LightingBytesPerSection = voxel::ChunkVolume * sizeof(voxel::LightWord);
constexpr usize FluidBytesPerCell = sizeof(u32) + sizeof(u8) + sizeof(u8);
constexpr usize FluidBytesPerSection = voxel::ChunkVolume * FluidBytesPerCell;

class ByteWriter {
public:
  void write_u8(u8 value) {
    bytes_.push_back(value);
  }

  void write_u16(u16 value) {
    bytes_.push_back(static_cast<u8>(value & 0xFFU));
    bytes_.push_back(static_cast<u8>((value >> 8U) & 0xFFU));
  }

  void write_u32(u32 value) {
    for (usize index = 0; index < sizeof(u32); ++index) {
      bytes_.push_back(static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
  }

  void write_i32(i32 value) {
    u32 raw{};
    std::memcpy(&raw, &value, sizeof(value));
    write_u32(raw);
  }

  void write_bytes(std::span<const u8> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  void write_text(std::string_view text) {
    write_u16(static_cast<u16>(text.size()));
    write_bytes({reinterpret_cast<const u8*>(text.data()), text.size()});
  }

  void pad_to(usize size) {
    if (bytes_.size() < size) {
      bytes_.resize(size, 0U);
    }
  }

  void patch_u32(usize offset, u32 value) {
    for (usize index = 0; index < sizeof(u32); ++index) {
      bytes_[offset + index] = static_cast<u8>((value >> (index * 8U)) & 0xFFU);
    }
  }

  void patch_u16(usize offset, u16 value) {
    bytes_[offset] = static_cast<u8>(value & 0xFFU);
    bytes_[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
  }

  [[nodiscard]] usize size() const noexcept {
    return bytes_.size();
  }

  [[nodiscard]] std::vector<u8>& bytes() noexcept {
    return bytes_;
  }

  [[nodiscard]] std::vector<u8> take() && {
    return std::move(bytes_);
  }

private:
  std::vector<u8> bytes_{};
};

class ByteReader {
public:
  explicit ByteReader(std::span<const u8> bytes) : bytes_(bytes) {
  }

  [[nodiscard]] bool can_read(usize count) const noexcept {
    return offset_ + count <= bytes_.size();
  }

  [[nodiscard]] std::optional<u8> read_u8() {
    if (!can_read(1U)) {
      return std::nullopt;
    }
    return bytes_[offset_++];
  }

  [[nodiscard]] std::optional<u16> read_u16() {
    if (!can_read(sizeof(u16))) {
      return std::nullopt;
    }
    const u16 value = static_cast<u16>(bytes_[offset_]) |
                      (static_cast<u16>(bytes_[offset_ + 1U]) << 8U);
    offset_ += sizeof(u16);
    return value;
  }

  [[nodiscard]] std::optional<u32> read_u32() {
    if (!can_read(sizeof(u32))) {
      return std::nullopt;
    }

    u32 value{};
    for (usize index = 0; index < sizeof(u32); ++index) {
      value |= static_cast<u32>(bytes_[offset_ + index]) << (index * 8U);
    }
    offset_ += sizeof(u32);
    return value;
  }

  [[nodiscard]] std::optional<i32> read_i32() {
    const auto raw = read_u32();
    if (!raw) {
      return std::nullopt;
    }

    i32 value{};
    const u32 raw_value = raw.value();
    std::memcpy(&value, &raw_value, sizeof(value));
    return value;
  }

  [[nodiscard]] std::optional<std::string> read_text() {
    const auto size = read_u16();
    if (!size || !can_read(size.value())) {
      return std::nullopt;
    }
    const char* first = reinterpret_cast<const char*>(bytes_.data() + offset_);
    std::string text(first, first + size.value());
    offset_ += size.value();
    return text;
  }

  [[nodiscard]] std::optional<std::span<const u8>> read_bytes(usize count) {
    if (!can_read(count)) {
      return std::nullopt;
    }
    const std::span<const u8> view = bytes_.subspan(offset_, count);
    offset_ += count;
    return view;
  }

  [[nodiscard]] usize offset() const noexcept {
    return offset_;
  }

private:
  std::span<const u8> bytes_;
  usize offset_{};
};

[[nodiscard]] PersistenceError make_error(std::string message) {
  return PersistenceError{std::move(message)};
}

[[nodiscard]] u32 region_slot_index(ChunkSectionCoord coord) noexcept {
  const RegionLocalChunkCoord local = chunk_column_to_region_local(chunk_to_column(coord));
  return static_cast<u32>(local.x + local.z * RegionSizeInChunks);
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

void write_fluid_cells(ByteWriter& writer, const std::array<voxel::FluidCell, voxel::ChunkVolume>& fluids) {
  for (const voxel::FluidCell fluid : fluids) {
    writer.write_u32(fluid.type.value);
    writer.write_u8(fluid.level);
    writer.write_u8(static_cast<u8>(fluid.flags));
  }
}

[[nodiscard]] Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError> read_fluid_cells(
    std::span<const u8> bytes) {
  if (bytes.size() != FluidBytesPerSection) {
    return Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError>::failure(
        make_error("fluid data section has unexpected size"));
  }

  ByteReader reader(bytes);
  std::array<voxel::FluidCell, voxel::ChunkVolume> fluids{};
  for (voxel::FluidCell& fluid : fluids) {
    const auto type = reader.read_u32();
    const auto level = reader.read_u8();
    const auto flags = reader.read_u8();
    if (!type || !level || !flags) {
      return Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError>::failure(
          make_error("fluid data section is truncated"));
    }
    fluid = {
        voxel::FluidTypeID{type.value()},
        level.value(),
        static_cast<voxel::FluidFlags>(flags.value())};
  }
  return Expected<std::array<voxel::FluidCell, voxel::ChunkVolume>, PersistenceError>::success(fluids);
}

[[nodiscard]] Expected<std::vector<u8>, PersistenceError> encode_region_tables(
    const std::vector<std::string>& block_state_table) {
  ByteWriter tables;
  ByteWriter block_table_data;
  for (const std::string& entry : block_state_table) {
    block_table_data.write_text(entry);
  }

  tables.write_u16(BlockStateTableType);
  tables.write_u16(static_cast<u16>(block_state_table.size()));
  tables.write_u32(static_cast<u32>(block_table_data.size()));
  tables.write_bytes(block_table_data.bytes());

  ByteWriter result;
  result.write_u32(static_cast<u32>(tables.size()));
  result.write_bytes(tables.bytes());
  return Expected<std::vector<u8>, PersistenceError>::success(std::move(result).take());
}

[[nodiscard]] Expected<std::vector<std::string>, PersistenceError> decode_block_state_table(
    std::span<const u8> bytes) {
  ByteReader reader(bytes);
  const auto table_block_size = reader.read_u32();
  if (!table_block_size || !reader.can_read(table_block_size.value())) {
    return Expected<std::vector<std::string>, PersistenceError>::failure(
        make_error("region tables block is truncated"));
  }

  std::vector<std::string> block_state_table;
  const usize table_block_end = reader.offset() + table_block_size.value();
  while (reader.offset() < table_block_end) {
    const auto table_type = reader.read_u16();
    const auto entry_count = reader.read_u16();
    const auto data_size = reader.read_u32();
    if (!table_type || !entry_count || !data_size || !reader.can_read(data_size.value())) {
      return Expected<std::vector<std::string>, PersistenceError>::failure(
          make_error("invalid region table entry"));
    }

    const std::span<const u8> table_data = reader.read_bytes(data_size.value()).value();
    if (table_type.value() != BlockStateTableType) {
      continue;
    }

    ByteReader table_reader(table_data);
    block_state_table.reserve(entry_count.value());
    for (u16 entry = 0; entry < entry_count.value(); ++entry) {
      const auto text = table_reader.read_text();
      if (!text) {
        return Expected<std::vector<std::string>, PersistenceError>::failure(
            make_error("block state table entry is truncated"));
      }
      block_state_table.push_back(text.value());
    }
  }

  if (block_state_table.empty()) {
    block_state_table.push_back(std::string{voxel::AirBlockName});
  }
  return Expected<std::vector<std::string>, PersistenceError>::success(std::move(block_state_table));
}

[[nodiscard]] std::filesystem::path temporary_path_for(const std::filesystem::path& path) {
  return path.parent_path() / (path.filename().string() + ".tmp");
}

Expected<void, PersistenceError> write_binary_atomic(
    const std::filesystem::path& path,
    std::span<const u8> bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Expected<void, PersistenceError>::failure(
        make_error("failed to create directory for region file: " + error.message()));
  }

  const std::filesystem::path temp_path = temporary_path_for(path);
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
  if (bytes.size() < RegionHeaderSize) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("region file is shorter than the fixed header"));
  }

  if (!std::equal(RegionMagic.begin(), RegionMagic.end(), reinterpret_cast<const char*>(bytes.data()))) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("region file magic does not match ARVR"));
  }

  ByteReader reader(bytes.subspan(4U));
  const auto format_version = reader.read_u16();
  const auto header_size = reader.read_u16();
  const auto region_x = reader.read_u32();
  const auto region_z = reader.read_u32();
  const auto chunk_count = reader.read_u32();
  const auto location_table_offset = reader.read_u32();
  const auto metadata_table_offset = reader.read_u32();
  const auto region_tables_offset = reader.read_u32();
  const auto data_start_offset = reader.read_u32();
  const auto file_size = reader.read_u32();
  const auto flags = reader.read_u32();

  if (!format_version || !header_size || !region_x || !region_z || !chunk_count ||
      !location_table_offset || !metadata_table_offset || !region_tables_offset ||
      !data_start_offset || !file_size || !flags) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("failed to decode region header"));
  }

  RegionFileHeader header;
  header.format_version = format_version.value();
  header.header_size = header_size.value();
  header.region = {static_cast<i32>(region_x.value()), static_cast<i32>(region_z.value())};
  header.chunk_count = chunk_count.value();
  header.location_table_offset = location_table_offset.value();
  header.metadata_table_offset = metadata_table_offset.value();
  header.region_tables_offset = region_tables_offset.value();
  header.data_start_offset = data_start_offset.value();
  header.file_size = file_size.value();
  header.flags = flags.value();

  const u64 location_table_end =
      static_cast<u64>(header.location_table_offset) +
      static_cast<u64>(header.chunk_count) * ChunkLocationEntrySize;
  const u64 metadata_table_end =
      static_cast<u64>(header.metadata_table_offset) +
      static_cast<u64>(header.chunk_count) * ChunkMetadataEntrySize;

  if (header.format_version < MinimumReadableRegionFormatVersion ||
      header.format_version > CurrentRegionFormatVersion) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("unsupported region format version"));
  }
  if (header.header_size < RegionHeaderSize || header.chunk_count > RegionChunkSlotCount ||
      header.file_size > bytes.size() || header.location_table_offset < header.header_size ||
      header.metadata_table_offset < location_table_end ||
      header.region_tables_offset < metadata_table_end ||
      header.data_start_offset < header.region_tables_offset ||
      location_table_end > header.file_size || metadata_table_end > header.file_size ||
      header.region_tables_offset >= header.file_size ||
      header.data_start_offset > header.file_size) {
    return Expected<RegionFileHeader, PersistenceError>::failure(
        make_error("region header contains invalid offsets or file size"));
  }

  return Expected<RegionFileHeader, PersistenceError>::success(header);
}

Expected<std::vector<u8>, PersistenceError> encode_region_file(
    RegionCoord region,
    std::span<const SavedChunkSection> chunks,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y) {
  HashMap<std::string, u16> block_state_indices;
  std::vector<std::string> block_state_table;
  block_state_table.push_back(std::string{voxel::AirBlockName});
  block_state_indices.emplace(std::string{voxel::AirBlockName}, 0U);

  std::array<std::vector<const SavedChunkSection*>, RegionChunkSlotCount> chunk_columns;
  for (const SavedChunkSection& chunk : chunks) {
    if (chunk_column_to_region(chunk_to_column(chunk.coord)) != region) {
      return Expected<std::vector<u8>, PersistenceError>::failure(
          make_error("chunk does not belong to the requested region file"));
    }

    for (const voxel::BlockVariantID variant : chunk.blocks) {
      const std::string name{registry.variant_name(variant)};
      if (block_state_indices.find(name) != block_state_indices.end()) {
        continue;
      }
      const u16 next_index = static_cast<u16>(block_state_table.size());
      block_state_table.push_back(name);
      block_state_indices.emplace(name, next_index);
    }

    chunk_columns[region_slot_index(chunk.coord)].push_back(&chunk);
  }

  auto bits_result = bits_per_block_for_table_size(block_state_table.size());
  if (!bits_result) {
    return Expected<std::vector<u8>, PersistenceError>::failure(bits_result.error());
  }
  const u8 bits_per_block = bits_result.value();

  auto tables_result = encode_region_tables(block_state_table);
  if (!tables_result) {
    return tables_result;
  }
  const std::vector<u8> region_tables = std::move(tables_result.value());

  RegionFileHeader header;
  header.region = region;
  header.location_table_offset = RegionHeaderSize;
  header.metadata_table_offset =
      header.location_table_offset + RegionChunkSlotCount * ChunkLocationEntrySize;
  header.region_tables_offset =
      header.metadata_table_offset + RegionChunkSlotCount * ChunkMetadataEntrySize;
  header.data_start_offset = header.region_tables_offset + static_cast<u32>(region_tables.size());

  ByteWriter writer;
  writer.write_bytes({reinterpret_cast<const u8*>(RegionMagic.data()), RegionMagic.size()});
  writer.write_u16(header.format_version);
  writer.write_u16(header.header_size);
  writer.write_u32(static_cast<u32>(header.region.x));
  writer.write_u32(static_cast<u32>(header.region.z));
  writer.write_u32(header.chunk_count);
  writer.write_u32(header.location_table_offset);
  writer.write_u32(header.metadata_table_offset);
  writer.write_u32(header.region_tables_offset);
  writer.write_u32(header.data_start_offset);
  writer.write_u32(0U); // patched file size
  writer.write_u32(header.flags);
  writer.pad_to(RegionHeaderSize);
  writer.pad_to(header.location_table_offset + RegionChunkSlotCount * ChunkLocationEntrySize);
  writer.pad_to(header.metadata_table_offset + RegionChunkSlotCount * ChunkMetadataEntrySize);
  writer.pad_to(header.region_tables_offset);
  writer.write_bytes(region_tables);
  writer.pad_to(header.data_start_offset);

  for (u32 slot_index = 0; slot_index < RegionChunkSlotCount; ++slot_index) {
    auto& column = chunk_columns[slot_index];
    if (column.empty()) {
      continue;
    }

    std::sort(column.begin(), column.end(), [&](const SavedChunkSection* lhs, const SavedChunkSection* rhs) {
      return lhs->coord.value.y < rhs->coord.value.y;
    });

    if (column.size() > std::numeric_limits<u32>::max()) {
      return Expected<std::vector<u8>, PersistenceError>::failure(
          make_error("chunk column has too many sections for region encoding"));
    }

    ByteWriter payload_writer;
    u16 section_mask{};
    bool section_mask_overflow = false;
    bool lighting_valid = true;
    for (const SavedChunkSection* section : column) {
      const i64 local_y =
          static_cast<i64>(section->coord.value.y) - static_cast<i64>(min_chunk_y);
      if (local_y >= 0 && local_y < 16) {
        section_mask |= static_cast<u16>(1U << static_cast<u32>(local_y));
      } else {
        section_mask_overflow = true;
      }
      lighting_valid = lighting_valid && section->lighting_valid;

      std::array<u16, voxel::ChunkVolume> indices{};
      for (usize index = 0; index < voxel::ChunkVolume; ++index) {
        const std::string key{registry.variant_name(section->blocks[index])};
        indices[index] = block_state_indices[key];
      }
      const std::vector<u8> packed_blocks = pack_block_indices(indices, bits_per_block);

      payload_writer.write_i32(section->coord.value.y);
      payload_writer.write_u8(bits_per_block);
      payload_writer.write_u8(0U);
      payload_writer.write_u16(0U);
      payload_writer.write_u32(static_cast<u32>(packed_blocks.size()));
      payload_writer.write_bytes(packed_blocks);
      payload_writer.write_u32(static_cast<u32>(LightingBytesPerSection));
      payload_writer.write_bytes(
          {reinterpret_cast<const u8*>(section->lights.data()), LightingBytesPerSection});
      payload_writer.write_u32(static_cast<u32>(FluidBytesPerSection));
      write_fluid_cells(payload_writer, section->fluids);
    }

    payload_writer.write_u32(0U); // block entity count
    payload_writer.write_u32(0U); // scheduled tick count

    ByteWriter chunk_writer;
    chunk_writer.write_u8(0U); // compression none
    chunk_writer.write_u8(0U);
    chunk_writer.write_u16(0U);
    chunk_writer.write_u32(static_cast<u32>(column.size()));
    chunk_writer.write_u32(static_cast<u32>(payload_writer.size()));
    chunk_writer.write_u32(static_cast<u32>(payload_writer.size()));
    chunk_writer.write_bytes(payload_writer.bytes());

    const u32 offset = static_cast<u32>(writer.size());
    const u32 length = static_cast<u32>(chunk_writer.size());
    writer.write_bytes(chunk_writer.bytes());

    const usize location_offset =
        header.location_table_offset + slot_index * ChunkLocationEntrySize;
    writer.patch_u32(location_offset, offset);
    writer.patch_u32(location_offset + 4U, length);

    const usize metadata_offset =
        header.metadata_table_offset + slot_index * ChunkMetadataEntrySize;
    if (section_mask_overflow) {
      section_mask = std::numeric_limits<u16>::max();
    }
    writer.patch_u16(metadata_offset, section_mask);
    writer.bytes()[metadata_offset + 2U] = lighting_valid ? ChunkFlagLightingValid : 0U;
    writer.bytes()[metadata_offset + 3U] = ChunkMetadataVersion;
  }

  header.file_size = static_cast<u32>(writer.size());
  writer.patch_u32(36U, header.file_size);
  return Expected<std::vector<u8>, PersistenceError>::success(std::move(writer).take());
}

Expected<DecodedRegionFile, PersistenceError> decode_region_file(
    std::span<const u8> bytes,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y) {
  auto header_result = parse_region_header(bytes);
  if (!header_result) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(header_result.error());
  }
  const RegionFileHeader header = header_result.value();

  auto table_result = decode_block_state_table(bytes.subspan(header.region_tables_offset));
  if (!table_result) {
    return Expected<DecodedRegionFile, PersistenceError>::failure(table_result.error());
  }
  const std::vector<std::string> block_state_table = std::move(table_result.value());

  DecodedRegionFile decoded;
  decoded.header = header;

  for (u32 slot_index = 0; slot_index < header.chunk_count; ++slot_index) {
    ByteReader location_reader(
        bytes.subspan(header.location_table_offset + slot_index * ChunkLocationEntrySize, ChunkLocationEntrySize));
    const auto chunk_offset = location_reader.read_u32();
    const auto chunk_length = location_reader.read_u32();
    if (!chunk_offset || !chunk_length || chunk_offset.value() == 0U || chunk_length.value() == 0U) {
      continue;
    }
    if (chunk_offset.value() + chunk_length.value() > header.file_size) {
      continue;
    }

    ByteReader metadata_reader(
        bytes.subspan(header.metadata_table_offset + slot_index * ChunkMetadataEntrySize, ChunkMetadataEntrySize));
    const auto section_mask = metadata_reader.read_u16();
    const auto flags = metadata_reader.read_u8();
    const auto version = metadata_reader.read_u8();
    const auto reserved = metadata_reader.read_u32();
    if (!section_mask || !flags || !version || !reserved) {
      continue;
    }

    const std::span<const u8> chunk_bytes = bytes.subspan(chunk_offset.value(), chunk_length.value());
    ByteReader chunk_reader(chunk_bytes);
    const auto compression_type = chunk_reader.read_u8();
    if (!compression_type) {
      continue;
    }

    u32 section_count{};
    std::optional<u32> uncompressed_size;
    std::optional<u32> compressed_size;
    if (header.format_version >= 3U) {
      const auto chunk_reserved_u8 = chunk_reader.read_u8();
      const auto chunk_reserved_u16 = chunk_reader.read_u16();
      const auto encoded_section_count = chunk_reader.read_u32();
      uncompressed_size = chunk_reader.read_u32();
      compressed_size = chunk_reader.read_u32();
      if (!chunk_reserved_u8 || !chunk_reserved_u16 || !encoded_section_count ||
          !uncompressed_size || !compressed_size) {
        continue;
      }
      section_count = encoded_section_count.value();
    } else {
      const auto encoded_section_count = chunk_reader.read_u8();
      const auto chunk_reserved = chunk_reader.read_u16();
      uncompressed_size = chunk_reader.read_u32();
      compressed_size = chunk_reader.read_u32();
      if (!encoded_section_count || !chunk_reserved || !uncompressed_size || !compressed_size) {
        continue;
      }
      section_count = encoded_section_count.value();
    }

    if (compression_type.value() != 0U || !chunk_reader.can_read(compressed_size.value())) {
      continue;
    }
    auto payload_view = chunk_reader.read_bytes(compressed_size.value());
    if (!payload_view || compression_type.value() != 0U || payload_view->size() != uncompressed_size.value()) {
      continue;
    }

    const u32 local_x = slot_index % RegionSizeInChunks;
    const u32 local_z = slot_index / RegionSizeInChunks;
    const i32 chunk_x = header.region.x * RegionSizeInChunks + static_cast<i32>(local_x);
    const i32 chunk_z = header.region.z * RegionSizeInChunks + static_cast<i32>(local_z);

    ByteReader payload_reader(*payload_view);
    for (u32 section_index = 0; section_index < section_count; ++section_index) {
      i32 chunk_y{};
      u8 bits_per_block{};
      std::optional<u32> block_data_size;
      if (header.format_version >= 3U) {
        const auto encoded_chunk_y = payload_reader.read_i32();
        const auto encoded_bits_per_block = payload_reader.read_u8();
        const auto section_reserved_u8 = payload_reader.read_u8();
        const auto section_reserved_u16 = payload_reader.read_u16();
        block_data_size = payload_reader.read_u32();
        if (!encoded_chunk_y || !encoded_bits_per_block || !section_reserved_u8 ||
            !section_reserved_u16 || !block_data_size) {
          break;
        }
        chunk_y = encoded_chunk_y.value();
        bits_per_block = encoded_bits_per_block.value();
      } else {
        const auto y_index = payload_reader.read_u8();
        const auto encoded_bits_per_block = payload_reader.read_u8();
        const auto section_reserved = payload_reader.read_u16();
        block_data_size = payload_reader.read_u32();
        if (!y_index || !encoded_bits_per_block || !section_reserved || !block_data_size) {
          break;
        }
        chunk_y = min_chunk_y + static_cast<i32>(y_index.value());
        bits_per_block = encoded_bits_per_block.value();
      }

      auto block_data = payload_reader.read_bytes(block_data_size.value());
      const auto lighting_data_size = payload_reader.read_u32();
      if (!block_data || !lighting_data_size) {
        break;
      }
      auto lighting_data = payload_reader.read_bytes(lighting_data_size.value());
      if (!lighting_data || lighting_data->size() != LightingBytesPerSection) {
        break;
      }

      std::array<voxel::FluidCell, voxel::ChunkVolume> fluids{};
      if (header.format_version >= 2U) {
        const auto fluid_data_size = payload_reader.read_u32();
        if (!fluid_data_size) {
          break;
        }
        auto fluid_data = payload_reader.read_bytes(fluid_data_size.value());
        if (!fluid_data) {
          break;
        }
        auto fluid_result = read_fluid_cells(*fluid_data);
        if (!fluid_result) {
          break;
        }
        fluids = std::move(fluid_result.value());
      } else {
        fluids.fill(voxel::empty_fluid_cell());
      }

      auto indices_result = unpack_block_indices(*block_data, bits_per_block);
      if (!indices_result) {
        break;
      }

      SavedChunkSection section;
      section.coord = {{chunk_x, chunk_y, chunk_z}};
      section.lighting_valid = (flags.value() & ChunkFlagLightingValid) != 0U;

      for (usize index = 0; index < voxel::ChunkVolume; ++index) {
        const u16 block_index = indices_result.value()[index];
        if (block_index >= block_state_table.size()) {
          section.blocks[index] = voxel::AirBlockVariantID;
          continue;
        }

        const auto resolved = registry.find_variant(block_state_table[block_index]);
        // TODO(arvane): Replace missing saved block states with a documented
        // arvane:missing_block fallback once that registry entry exists.
        section.blocks[index] = resolved.value_or(voxel::AirBlockVariantID);
      }
      std::memcpy(section.lights.data(), lighting_data->data(), LightingBytesPerSection);
      section.fluids = std::move(fluids);
      decoded.chunks.push_back(std::move(section));
    }
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
