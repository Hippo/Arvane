#pragma once

#include "arv/io/data_codecs.hpp"

namespace arv::data {

using DataBytesResult = io::DataBytesResult;
using DataCodecError = io::DataCodecError;
using DataDocumentResult = io::DataDocumentResult;
using DataTextResult = io::DataTextResult;

using io::decode_binary;
using io::decode_json;
using io::decode_json_value;
using io::decode_yaml;
using io::decode_yaml_value;
using io::encode_binary;
using io::encode_json;
using io::encode_json_value;
using io::encode_yaml;
using io::encode_yaml_value;

} // namespace arv::data
