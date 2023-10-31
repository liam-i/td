//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageInputReplyTo.h"
#include "td/telegram/MessageOrigin.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <functional>

namespace td {

class Dependencies;

class Td;

class RepliedMessageInfo {
  MessageId message_id_;
  DialogId dialog_id_;                  // for replies from another known chats
  int32 origin_date_ = 0;               // for replies in other chats
  MessageOrigin origin_;                // for replies in other chats
  unique_ptr<MessageContent> content_;  // for replies in other chats
  FormattedText quote_;
  bool is_quote_manual_ = false;

  friend bool operator==(const RepliedMessageInfo &lhs, const RepliedMessageInfo &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const RepliedMessageInfo &info);

 public:
  RepliedMessageInfo() = default;
  RepliedMessageInfo(const RepliedMessageInfo &) = delete;
  RepliedMessageInfo &operator=(const RepliedMessageInfo &) = delete;
  RepliedMessageInfo(RepliedMessageInfo &&) = default;
  RepliedMessageInfo &operator=(RepliedMessageInfo &&) = default;
  ~RepliedMessageInfo();

  explicit RepliedMessageInfo(MessageId reply_to_message_id) : message_id_(reply_to_message_id) {
  }

  RepliedMessageInfo(MessageId reply_to_message_id, DialogId reply_in_dialog_id)
      : message_id_(reply_to_message_id), dialog_id_(reply_in_dialog_id) {
  }

  RepliedMessageInfo(Td *td, tl_object_ptr<telegram_api::messageReplyHeader> &&reply_header, DialogId dialog_id,
                     MessageId message_id, int32 date);

  RepliedMessageInfo(Td *td, const MessageInputReplyTo &input_reply_to);

  bool is_empty() const {
    return message_id_ == MessageId() && dialog_id_ == DialogId() && origin_date_ == 0 && origin_.is_empty() &&
           quote_.text.empty() && content_ == nullptr;
  }

  bool need_reget() const;

  static bool need_reply_changed_warning(
      const Td *td, const RepliedMessageInfo &old_info, const RepliedMessageInfo &new_info,
      MessageId old_top_thread_message_id, bool is_yet_unsent,
      std::function<bool(const RepliedMessageInfo &info)> is_reply_to_deleted_message);

  vector<FileId> get_file_ids(Td *td) const;

  vector<UserId> get_min_user_ids(Td *td) const;

  vector<ChannelId> get_min_channel_ids(Td *td) const;

  void add_dependencies(Dependencies &dependencies, bool is_bot) const;

  td_api::object_ptr<td_api::messageReplyToMessage> get_message_reply_to_message_object(Td *td,
                                                                                        DialogId dialog_id) const;

  MessageInputReplyTo get_input_reply_to() const;

  void set_message_id(MessageId new_message_id) {
    CHECK(message_id_.is_valid() || message_id_.is_valid_scheduled());
    message_id_ = new_message_id;
  }

  MessageId get_same_chat_reply_to_message_id() const;

  MessageFullId get_reply_message_full_id(DialogId owner_dialog_id) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const RepliedMessageInfo &lhs, const RepliedMessageInfo &rhs);

bool operator!=(const RepliedMessageInfo &lhs, const RepliedMessageInfo &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const RepliedMessageInfo &info);

}  // namespace td
