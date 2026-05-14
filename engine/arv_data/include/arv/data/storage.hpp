#pragma once

#include "arv/io/storage.hpp"

namespace arv::data {

using DataStorageCode = io::DataStorageCode;
using DataStorageError = io::DataStorageError;
using PersistentDataStorageService = io::PersistentDataStorageService;
using PersistentScope = io::PersistentScope;
using PersistentScopeKind = io::PersistentScopeKind;
using ResourceKey = io::ResourceKey;
using StorageLoadResult = io::StorageLoadResult;
using StoragePathResult = io::StoragePathResult;
using StorageRemoveResult = io::StorageRemoveResult;
using StorageSaveResult = io::StorageSaveResult;
using ConfigStorageService = io::ConfigStorageService;

using io::safe_resource_relative_path;

} // namespace arv::data
