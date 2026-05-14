#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_text_literal(
    void* context,
    ArvStringView text,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  const std::string value{abi_view(text)};
  if (!arv::text::is_valid_utf8(value)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "text is not valid UTF-8");
  }
  *out_text = store_text(ctx, arv::text::plain_rich_text(value));
  return ok(ctx);
}

ArvResult api_text_translatable(
    void* context,
    ArvStringView translation_key,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  const std::string key{abi_view(translation_key)};
  if (key.empty()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "translation key is empty");
  }
  if (key.size() > protocol::MaxRichTextTranslationKeyBytes) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "translation key is too long");
  }
  if (!arv::text::is_valid_utf8(key)) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "translation key is not valid UTF-8");
  }
  *out_text = store_text(ctx, arv::text::translatable_rich_text(key));
  return ok(ctx);
}

ArvResult api_text_append(
    void* context,
    ArvTextHandle parent,
    ArvTextHandle child,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  arv::text::RichText* parent_text = text_from_handle(ctx, parent);
  arv::text::RichText* child_text = text_from_handle(ctx, child);
  if (parent_text == nullptr || child_text == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  arv::text::RichText copy = *parent_text;
  copy.children.push_back(*child_text);
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_style(
    void* context,
    ArvTextHandle text,
    ArvTextStylePatch style,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  arv::text::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  arv::text::RichText copy = *source;
  apply_text_style_patch(copy.style, style);
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(copy);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_color(
    void* context,
    ArvTextHandle text,
    ArvTextColor color,
    ArvTextHandle* out_text) {
  ArvTextStylePatch style{};
  style.has_color = true;
  style.color = color;
  return api_text_style(context, text, style, out_text);
}

void apply_hover_to_text(arv::text::RichText& text, arv::text::HoverEvent hover) {
  bool applied = false;
  if (!text.text.empty()) {
    text.hover_event = hover;
    applied = true;
  }
  for (arv::text::RichText& child : text.children) {
    apply_hover_to_text(child, hover);
    applied = true;
  }
  if (!applied) {
    text.hover_event = std::move(hover);
  }
}

void apply_click_to_text(arv::text::RichText& text, arv::text::ClickEvent click) {
  bool applied = false;
  if (!text.text.empty()) {
    text.click_event = click;
    applied = true;
  }
  for (arv::text::RichText& child : text.children) {
    apply_click_to_text(child, click);
    applied = true;
  }
  if (!applied) {
    text.click_event = std::move(click);
  }
}

ArvResult api_text_hover_text(
    void* context,
    ArvTextHandle text,
    ArvTextHandle hover_text,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  arv::text::RichText* source = text_from_handle(ctx, text);
  arv::text::RichText* hover_source = text_from_handle(ctx, hover_text);
  if (source == nullptr || hover_source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  arv::text::RichText copy = *source;
  apply_hover_to_text(copy, arv::text::make_show_text_hover(*hover_source));
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(copy);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_click(
    void* context,
    ArvTextHandle text,
    ArvTextClickAction action,
    ArvStringView value,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  arv::text::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  arv::text::RichText copy = *source;
  apply_click_to_text(
      copy,
      arv::text::ClickEvent{click_action_from_abi(action), abi_string(value)});
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(copy);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_plain_text(
    void* context,
    ArvTextHandle text,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  arv::text::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  *out_buffer = byte_buffer_from_string(arv::text::to_plain_text(*source));
  return ok(ctx);
}

ArvResult api_text_serialize(
    void* context,
    ArvTextHandle text,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  arv::text::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(*source);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  const std::vector<u8> bytes = protocol::serialize_rich_text(*source);
  std::string serialized;
  serialized.resize(bytes.size());
  if (!bytes.empty()) {
    std::memcpy(serialized.data(), bytes.data(), bytes.size());
  }
  *out_buffer = byte_buffer_from_string(std::move(serialized));
  return ok(ctx);
}

ArvResult api_text_validate(
    void* context,
    ArvTextHandle text,
    const ArvTextLimits* limits) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  arv::text::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const arv::text::RichTextValidation validation =
      arv::text::validate_rich_text(*source, text_limits_from_abi(limits));
  return validation.valid
             ? ok(ctx)
             : make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
}

void api_text_release(void* context, ArvTextHandle text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx != nullptr && text.value != 0U) {
    ctx->text_components.erase(text.value);
  }
}

} // namespace

void fill_text_api(PluginAPIStorage& storage) {
  storage.text_api = {
      &unsupported_no_arg,
      &api_text_literal,
      &api_text_translatable,
      &api_text_append,
      &api_text_style,
      &api_text_color,
      &api_text_hover_text,
      &api_text_click,
      &api_text_plain_text,
      &api_text_serialize,
      &api_text_validate,
      &api_text_release,
  };
}

} // namespace arv::plugin
