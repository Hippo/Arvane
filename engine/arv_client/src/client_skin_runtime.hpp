#pragma once

#include "arv/client/local_identity.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/skins.hpp"

#include <optional>
#include <string>
#include <vector>

namespace arv::client::detail {

class ClientProtocolSender;

class ClientSkinRuntime {
public:
  void handle_skin_rules(
      const LocalPlayerIdentity& identity,
      ClientProtocolSender& sender,
      protocol::ClientboundSkinRules rules);
  void handle_profile_info(
      const LocalPlayerIdentity& identity,
      ClientProtocolSender& sender,
      protocol::ClientboundPlayerProfileInfo info);
  void handle_profile_info_batch(
      const LocalPlayerIdentity& identity,
      ClientProtocolSender& sender,
      protocol::ClientboundPlayerProfileInfoBatch batch);
  void handle_skin_data(
      const LocalPlayerIdentity& identity,
      protocol::ClientboundSkinData data);
  void handle_skin_data_begin_chunked(protocol::ClientboundSkinDataBeginChunked begin);
  void handle_skin_data_chunk(protocol::ClientboundSkinDataChunk chunk);
  void handle_skin_data_end_chunked(
      const LocalPlayerIdentity& identity,
      protocol::ClientboundSkinDataEndChunked end);
  void handle_skin_request_deny(protocol::ClientboundSkinRequestDeny deny);
  void handle_skin_upload_accept(ClientProtocolSender& sender, protocol::ClientboundSkinUploadAccept accept);
  void handle_skin_upload_deny(protocol::ClientboundSkinUploadDeny deny);
  void handle_skin_upload_complete(protocol::ClientboundSkinUploadComplete complete);
  void handle_skin_upload_failed(protocol::ClientboundSkinUploadFailed failed);

  [[nodiscard]] const protocol::PlayerProfileInfo* profile_for_player(
      PlayerID player_id) const noexcept;
  [[nodiscard]] std::vector<protocol::PlayerProfileInfo> profiles() const;

private:
  struct PendingUpload {
    protocol::ServerboundSkinUploadBegin begin{};
    std::vector<u8> bytes{};
  };

  struct PendingDownload {
    std::string skin_hash{};
    protocol::SkinImageFormat format{protocol::SkinImageFormat::Png};
    u16 width{};
    u16 height{};
    u32 total_bytes{};
    std::vector<u8> bytes{};
  };

  void maybe_request_profile_skin(
      const LocalPlayerIdentity& identity,
      ClientProtocolSender& sender,
      const protocol::SkinDescriptor& skin);
  [[nodiscard]] u32 next_request_id() noexcept;

  protocol::ClientboundSkinRules rules_{};
  bool has_rules_{};
  std::optional<PendingUpload> pending_upload_{};
  u32 next_request_id_{1};
  HashMap<PlayerID, protocol::PlayerProfileInfo> profiles_by_player_{};
  HashMap<u32, std::string> requested_hash_by_id_{};
  HashSet<std::string> requested_hashes_{};
  HashMap<u32, PendingDownload> downloads_{};
};

} // namespace arv::client::detail
