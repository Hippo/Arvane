#pragma once

#include "arv/io/data_document.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::io {

struct DataCodecError {
  std::string path{"root"};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

using DataDocumentResult = Expected<DataDocument, DataCodecError>;
using DataBytesResult = Expected<std::vector<u8>, DataCodecError>;
using DataTextResult = Expected<std::string, DataCodecError>;

[[nodiscard]] DataTextResult encode_yaml(const DataDocument& document);
[[nodiscard]] DataDocumentResult decode_yaml(std::string_view text);
[[nodiscard]] DataTextResult encode_yaml_value(const DataValue& value);
[[nodiscard]] DataDocumentResult decode_yaml_value(std::string_view text, u32 schema_version = 0);

[[nodiscard]] DataTextResult encode_json(const DataDocument& document);
[[nodiscard]] DataDocumentResult decode_json(std::string_view text);
[[nodiscard]] DataTextResult encode_json_value(const DataValue& value);
[[nodiscard]] DataDocumentResult decode_json_value(std::string_view text, u32 schema_version = 0);

[[nodiscard]] DataBytesResult encode_binary(const DataDocument& document);
[[nodiscard]] DataDocumentResult decode_binary(std::span<const u8> bytes);

} // namespace arv::io
