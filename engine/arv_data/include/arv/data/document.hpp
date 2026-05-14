#pragma once

#include "arv/io/data_document.hpp"

namespace arv::data {

using DataDiagnostic = io::DataDiagnostic;
using DataKind = io::DataKind;
using DataPathResult = io::DataPathResult;
using DataValue = io::DataValue;
using DataDocument = io::DataDocument;

using io::data_kind_name;
using io::find_path;
using io::set_path;
using io::split_data_path;

} // namespace arv::data
