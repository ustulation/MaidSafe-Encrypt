/*
 * copyright maidsafe.net limited 2008
 * The following source code is property of maidsafe.net limited and
 * is not meant for external use. The use of this code is governed
 * by the license file LICENSE.TXT found in the root of this directory and also
 * on www.maidsafe.net.
 *
 * You are not free to copy, amend or otherwise use this source code without
 * explicit written permission of the board of directors of maidsafe.net
 *
 *  Created on: Nov 13, 2008
 *      Author: Team
 */

#include "maidsafe/clientbufferpackethandler.h"
#include "maidsafe/kademlia_service_messages.pb.h"

namespace maidsafe {

ClientBufferPacketHandler::ClientBufferPacketHandler(
    maidsafe::BufferPacketRpcs *rpcs, kad::KNode *knode) : crypto_obj_(),
    rpcs_(rpcs), knode_(knode) {
  crypto_obj_.set_hash_algorithm(crypto::SHA_512);
  crypto_obj_.set_symm_algorithm(crypto::AES_256);
}

void ClientBufferPacketHandler::CreateBufferPacket(const BPInputParameters
    &args, bp_operations_cb cb) {
  BufferPacket buffer_packet;
  GenericPacket *ser_owner_info = buffer_packet.add_owner_info();
  BufferPacketInfo buffer_packet_info;
  buffer_packet_info.set_owner(args.sign_id);
  buffer_packet_info.set_ownerpublickey(args.public_key);
  buffer_packet_info.set_online(1);
  ser_owner_info->set_data(buffer_packet_info.SerializeAsString());
  ser_owner_info->set_signature(crypto_obj_.AsymSign(
    ser_owner_info->data(), "", args.private_key, crypto::STRING_STRING));

  boost::shared_ptr<CreateBPData> data(new CreateBPData);
  data->cb = cb;
  data->request.set_bufferpacket_name(crypto_obj_.Hash(args.sign_id +
    args.public_key, "", crypto::STRING_STRING, false));

  data->request.set_data(buffer_packet.SerializePartialAsString());
  data->request.set_pmid(args.sign_id);
  data->request.set_public_key(args.public_key);
  data->request.set_signed_public_key(crypto_obj_.AsymSign(args.public_key, "",
    args.private_key, crypto::STRING_STRING));
  data->request.set_signed_request(crypto_obj_.AsymSign(crypto_obj_.Hash(
      args.public_key + data->request.signed_public_key() +
      data->request.bufferpacket_name(), "", crypto::STRING_STRING, false), "",
      args.private_key, crypto::STRING_STRING));

  IterativeStore(data);
}

void ClientBufferPacketHandler::IterativeStore(boost::shared_ptr<CreateBPData>
    data) {
  if (data->is_calledback)
    return;

  if (data->successful_stores >= kMinChunkCopies) {
    data->cb(kSuccess);
    data->is_calledback = true;
    return;
  }

  // Getting the contacts to store the new buffer packet
  boost::uint16_t concurrent_stores = kMinChunkCopies - data->successful_stores;

  if (concurrent_stores > ClientBufferPacketHandler::kParallelStores)
    concurrent_stores = ClientBufferPacketHandler::kParallelStores;

  std::vector<kad::Contact> ctcs;
  knode_->GetRandomContacts(concurrent_stores, data->exclude_ctcs, &ctcs);

  if (ctcs.empty()) {
    data->cb(kStoreNewBPError);
    data->is_calledback = true;
    return;
  }

  std::vector<kad::Contact>::iterator it = ctcs.begin();
  while (it != ctcs.end()) {
    bool local = (knode_->CheckContactLocalAddress(it->node_id(),
      it->local_ip(), it->local_port(), it->host_ip()) ==
      kad::LOCAL);
    CreateBPCallbackData cb_data;
    cb_data.ctrl = new rpcprotocol::Controller;
    cb_data.ctc = *it;
    cb_data.data = data;
    CreateBPResponse *resp = new CreateBPResponse;
    google::protobuf::Closure *done = google::protobuf::NewCallback <
      ClientBufferPacketHandler, const CreateBPResponse*, CreateBPCallbackData >
      (this, &ClientBufferPacketHandler::CreateBPCallback, resp, cb_data);
    data->exclude_ctcs.push_back(cb_data.ctc);
    rpcs_->CreateBP(cb_data.ctc, local, &data->request, resp, cb_data.ctrl,
      done);
    ++it;
  }
}

void ClientBufferPacketHandler::CreateBPCallback(const CreateBPResponse *resp,
    CreateBPCallbackData cb_data) {
  if (cb_data.data->is_calledback) {
    delete resp;
    delete cb_data.ctrl;
    IterativeStore(cb_data.data);
    return;
  }

  if (!resp->IsInitialized() || cb_data.ctrl->Failed()) {
    delete resp;
    delete cb_data.ctrl;
    IterativeStore(cb_data.data);
    return;
  }

  if (resp->result() == kAck && resp->pmid_id() == cb_data.ctc.node_id())
    ++cb_data.data->successful_stores;

  delete resp;
  delete cb_data.ctrl;
  IterativeStore(cb_data.data);
}

void ClientBufferPacketHandler::ModifyOwnerInfo(const BPInputParameters &args,
  const int &status, const std::vector<std::string> &users,
  bp_operations_cb cb) {
  boost::shared_ptr<ChangeBPData> data(new ChangeBPData);
  BufferPacketInfo buffer_packet_info;
  buffer_packet_info.set_owner(args.sign_id);
  buffer_packet_info.set_ownerpublickey(args.public_key);
  buffer_packet_info.set_online(status);
  for (unsigned int i = 0; i < users.size(); ++i)
    buffer_packet_info.add_users(users.at(i));

  GenericPacket ser_owner_info;
  ser_owner_info.set_data(buffer_packet_info.SerializeAsString());
  ser_owner_info.set_signature(crypto_obj_.AsymSign(
    ser_owner_info.data(), "", args.private_key, crypto::STRING_STRING));

  data->modify_request.set_data(ser_owner_info.SerializeAsString());
  data->modify_request.set_bufferpacket_name(crypto_obj_.Hash(args.sign_id +
    args.public_key, "", crypto::STRING_STRING, false));
  data->modify_request.set_pmid(args.sign_id);
  data->modify_request.set_public_key(args.public_key);
  data->modify_request.set_signed_public_key(crypto_obj_.AsymSign(
    args.public_key, "", args.private_key, crypto::STRING_STRING));
  data->modify_request.set_signed_request(crypto_obj_.AsymSign(crypto_obj_.Hash(
      args.public_key + data->modify_request.signed_public_key() +
      data->modify_request.bufferpacket_name(),
      "", crypto::STRING_STRING, false), "", args.private_key,
      crypto::STRING_STRING));

  data->cb = cb;
  data->type = MODIFY_INFO;
  FindReferences(boost::bind(&ClientBufferPacketHandler::FindReferences_CB,
    this, _1, data), data);
}

void ClientBufferPacketHandler::AddMessage(const BPInputParameters &args,
    const std::string &recver_public_key, const std::string &receiver_id,
    const std::string &message, const MessageType &m_type,
    bp_operations_cb cb) {
  boost::shared_ptr<ChangeBPData> data(new ChangeBPData);

  BufferPacketMessage bpmsg;
  bpmsg.set_sender_id(args.sign_id);
  bpmsg.set_sender_public_key(args.public_key);
  bpmsg.set_type(m_type);
  bpmsg.set_timestamp(base::get_epoch_time());
  // generating key to encrypt msg with AES
  uint32_t iter = base::random_32bit_uinteger() % 1000 +1;
  std::string aes_key = crypto_obj_.SecurePassword(
      crypto_obj_.Hash(message, "", crypto::STRING_STRING, true), iter);

  bpmsg.set_aesenc_message(crypto_obj_.SymmEncrypt(message, "",
    crypto::STRING_STRING, aes_key));
  // encrypting key with receivers public key
  bpmsg.set_rsaenc_key(crypto_obj_.AsymEncrypt(aes_key, "", recver_public_key,
    crypto::STRING_STRING));


  GenericPacket ser_bpmsg;
  ser_bpmsg.set_data(bpmsg.SerializeAsString());
  ser_bpmsg.set_signature(crypto_obj_.AsymSign(
    ser_bpmsg.data(), "", args.private_key, crypto::STRING_STRING));

  data->add_msg_request.set_data(ser_bpmsg.SerializeAsString());

  data->add_msg_request.set_bufferpacket_name(crypto_obj_.Hash(receiver_id +
    recver_public_key, "", crypto::STRING_STRING, false));
  data->add_msg_request.set_pmid(args.sign_id);
  data->add_msg_request.set_public_key(args.public_key);
  data->add_msg_request.set_signed_public_key(crypto_obj_.AsymSign(
    args.public_key, "", args.private_key, crypto::STRING_STRING));
  data->add_msg_request.set_signed_request(crypto_obj_.AsymSign(
      crypto_obj_.Hash(args.public_key +
      data->add_msg_request.signed_public_key() +
      data->add_msg_request.bufferpacket_name(), "", crypto::STRING_STRING,
      false), "", args.private_key, crypto::STRING_STRING));

  data->cb = cb;
  data->type = ADD_MESSAGE;
  FindReferences(boost::bind(&ClientBufferPacketHandler::FindReferences_CB,
    this, _1, data), data);
}

void ClientBufferPacketHandler::GetMessages(const BPInputParameters &args,
  bp_getmessages_cb cb) {
  boost::shared_ptr<ChangeBPData> data(new ChangeBPData);
  data->get_msgs_request.set_bufferpacket_name(crypto_obj_.Hash(args.sign_id +
    args.public_key, "", crypto::STRING_STRING, false));
  data->get_msgs_request.set_public_key(args.public_key);
  data->get_msgs_request.set_pmid(args.sign_id);
  data->get_msgs_request.set_signed_public_key(crypto_obj_.AsymSign(
    args.public_key, "", args.private_key, crypto::STRING_STRING));
  data->get_msgs_request.set_signed_request(crypto_obj_.AsymSign(
    crypto_obj_.Hash(args.public_key +
    data->get_msgs_request.signed_public_key() +
    data->get_msgs_request.bufferpacket_name(), "", crypto::STRING_STRING,
    false), "", args.private_key, crypto::STRING_STRING));

  data->cb_getmsgs = cb;
  data->type = GET_MESSAGES;
  data->private_key = args.private_key;
  FindReferences(boost::bind(&ClientBufferPacketHandler::FindReferences_CB,
    this, _1, data), data);
}

void ClientBufferPacketHandler::FindReferences(base::callback_func_type cb,
  boost::shared_ptr<ChangeBPData> data) {
  switch (data->type) {
    case ADD_MESSAGE: knode_->FindValue(
      data->add_msg_request.bufferpacket_name(), false, cb);
      break;
    case GET_MESSAGES: knode_->FindValue(
      data->get_msgs_request.bufferpacket_name(), false, cb);
      break;
    case MODIFY_INFO: knode_->FindValue(
      data->modify_request.bufferpacket_name(), false, cb);
      break;
  }
}

void ClientBufferPacketHandler::FindRemoteContact(base::callback_func_type cb,
  boost::shared_ptr<ChangeBPData> data, const int &idx) {
  knode_->FindNode(data->holder_ids[idx], cb, false);
}

void ClientBufferPacketHandler::FindReferences_CB(const std::string &result,
  boost::shared_ptr<ChangeBPData> data) {
  kad::FindResponse rslt;
  if (!rslt.ParseFromString(result) || rslt.result() !=
    kad::kRpcResultSuccess) {
    switch (data->type) {
      case MODIFY_INFO: data->cb(kModifyBPError);
                        break;
      case ADD_MESSAGE: data->cb(kBPAddMessageError);
                        break;
      case GET_MESSAGES: std::list<ValidatedBufferPacketMessage> msgs;
                         data->cb_getmsgs(kBPMessagesRetrievalError, msgs);
                         break;
    }
    return;
  }
  for (int i = 0; i < rslt.values_size(); ++i)
    data->holder_ids.push_back(rslt.values(i));

  ModifyBPCallbackData cb_data;
  cb_data.data = data;
  IterativeFindContacts(cb_data);
}

void ClientBufferPacketHandler::FindRemoteContact_CB(const std::string &result,
    boost::shared_ptr<ChangeBPData> data) {
  kad::FindNodeResult rslt;
  kad::Contact ctc;
  ModifyBPCallbackData cb_data;
  cb_data.data = data;
  if (!rslt.ParseFromString(result) ||
      rslt.result() != kad::kRpcResultSuccess ||
      !ctc.ParseFromString(rslt.contact())) {
    IterativeFindContacts(cb_data);
  } else {
    bool local = (knode_->CheckContactLocalAddress(ctc.node_id(),
      ctc.local_ip(), ctc.local_port(), ctc.host_ip()) == kad::LOCAL);
    cb_data.ctrl = new rpcprotocol::Controller;
    cb_data.ctc = ctc;

    if (cb_data.data->type == MODIFY_INFO) {
      cb_data.modify_response = new ModifyBPInfoResponse;
      google::protobuf::Closure *done = google::protobuf::NewCallback <
        ClientBufferPacketHandler, ModifyBPCallbackData > (this,
        &ClientBufferPacketHandler::IterativeFindContacts, cb_data);
      rpcs_->ModifyBPInfo(ctc, local, &data->modify_request,
        cb_data.modify_response, cb_data.ctrl, done);
    } else if (cb_data.data->type == ADD_MESSAGE) {
      cb_data.add_msg_response = new AddBPMessageResponse;
      google::protobuf::Closure *done = google::protobuf::NewCallback <
        ClientBufferPacketHandler, ModifyBPCallbackData > (this,
        &ClientBufferPacketHandler::IterativeFindContacts, cb_data);
      rpcs_->AddBPMessage(ctc, local, &data->add_msg_request,
        cb_data.add_msg_response, cb_data.ctrl, done);
    } else {
      cb_data.get_msgs_response = new GetBPMessagesResponse;
      google::protobuf::Closure *done = google::protobuf::NewCallback <
        ClientBufferPacketHandler, ModifyBPCallbackData > (this,
        &ClientBufferPacketHandler::IterativeFindContacts, cb_data);
      rpcs_->GetBPMessages(ctc, local, &data->get_msgs_request,
        cb_data.get_msgs_response, cb_data.ctrl, done);
    }
  }
}

void ClientBufferPacketHandler::IterativeFindContacts(
    ModifyBPCallbackData data) {
  if (data.data->is_calledback) {
    if (data.ctrl != NULL) {
      switch (data.data->type) {
        case MODIFY_INFO: delete data.modify_response;
                          break;
        case ADD_MESSAGE: delete data.add_msg_response;
                          break;
        case GET_MESSAGES: delete data.get_msgs_response;
                           break;
      }
      delete data.ctrl;
    }
    return;
  }

  // Reply of ModifyBPInfo Rpc
  if (data.ctrl != NULL) {
    if (!data.ctrl->Failed()) {
      switch (data.data->type) {
        case MODIFY_INFO: if (data.modify_response->IsInitialized() &&
                             data.modify_response->result() == kAck &&
                             data.modify_response->pmid_id() ==
                               data.ctc.node_id())
                            ++data.data->successful_ops;
                          delete data.modify_response;
                          break;
        case ADD_MESSAGE: if (data.add_msg_response->IsInitialized() &&
                             data.add_msg_response->result() == kAck &&
                             data.add_msg_response->pmid_id() ==
                               data.ctc.node_id())
                            ++data.data->successful_ops;
                          delete data.add_msg_response;
                          break;
        case GET_MESSAGES: if (data.get_msgs_response->IsInitialized() &&
                             data.get_msgs_response->result() == kAck &&
                             data.get_msgs_response->pmid_id() ==
                               data.ctc.node_id()) {
                             std::list<ValidatedBufferPacketMessage> msgs =
                               ValidateMsgs(data.get_msgs_response,
                               data.data->private_key);
                             data.data->cb_getmsgs(kSuccess, msgs);
                             return;
                           }
                           break;
      }
    }
    delete data.ctrl;
  }

  // No more holders contacted
  if (data.data->idx >= data.data->holder_ids.size()) {
    data.data->is_calledback = true;
    if (data.data->successful_ops > 0) {
      data.data->cb(kSuccess);
    } else {
      switch (data.data->type) {
        case MODIFY_INFO: data.data->cb(kModifyBPError);
                          break;
        case ADD_MESSAGE: data.data->cb(kBPAddMessageError);
                           break;
        case GET_MESSAGES: std::list<ValidatedBufferPacketMessage> msgs;
                           data.data->cb_getmsgs(kBPMessagesRetrievalError,
                             msgs);
                           break;
      }
    }
    return;
  }

  // Getting remaining ids to search
  boost::uint16_t remaining = data.data->holder_ids.size() - data.data->idx;
  if (remaining > ClientBufferPacketHandler::kParallelFindCtcs)
    remaining = ClientBufferPacketHandler::kParallelFindCtcs;

  while (remaining > 0) {
    const int curr_idx(data.data->idx);
    ++data.data->idx;
    FindRemoteContact(boost::bind(
      &ClientBufferPacketHandler::FindRemoteContact_CB, this, _1, data.data),
      data.data, curr_idx);
    --remaining;
  }
}

std::list<ValidatedBufferPacketMessage> ClientBufferPacketHandler::ValidateMsgs(
    const GetBPMessagesResponse *response, const std::string &private_key) {
  std::list<ValidatedBufferPacketMessage> result;
  for (int i = 0; i < response->messages_size(); ++i) {
    ValidatedBufferPacketMessage msg;
    if (msg.ParseFromString(response->messages(i))) {
      std::string aes_key = crypto_obj_.AsymDecrypt(msg.index(), "",
          private_key, crypto::STRING_STRING);
      msg.set_message(crypto_obj_.SymmDecrypt(msg.message(),
          "", crypto::STRING_STRING, aes_key));
      result.push_back(msg);
    }
  }
  return result;
}

}  // namespace maidsafe