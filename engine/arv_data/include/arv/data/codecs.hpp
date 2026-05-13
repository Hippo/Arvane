#pragma once

#include "arv/data/document.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::data {

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

[[nodiscard]] DataTextResult encode_json(const DataDocument& document);
[[nodiscard]] DataDocumentResult decode_json(std::string_view text);

[[nodiscard]] DataBytesResult encode_binary(const DataDocument& document);
[[nodiscard]] DataDocumentResult decode_binary(std::span<const u8> bytes);

} // namespace arv::data
