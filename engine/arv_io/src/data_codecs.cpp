#include "arv/io/data_codecs.hpp"

namespace arv::io {

std::string DataCodecError::describe() const {
  return path + ": " + message;
}

} // namespace arv::io
