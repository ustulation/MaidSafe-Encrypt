/*
 * copyright maidsafe.net limited 2008
 * The following source code is property of maidsafe.net limited and
 * is not meant for external use. The use of this code is governed
 * by the license file LICENSE.TXT found in teh root of this directory and also
 * on www.maidsafe.net.
 *
 * You are not free to copy, amend or otherwise use this source code without
 * explicit written permission of the board of directors of maidsafe.net
 *
 *  Created on: Sep 29, 2008
 *      Author: Haiyang, Jose, Fraser
 */

#include "maidsafe/client/pdclient.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <maidsafe/kademlia_service_messages.pb.h>
#include <maidsafe/maidsafe-dht.h>

#include "maidsafe/client/sessionsingleton.h"
#include "maidsafe/maidsafe.h"
#include "protobuf/general_messages.pb.h"
#include "protobuf/maidsafe_service_messages.pb.h"

namespace maidsafe {

inline void dummy_callback(const std::string&) {}

void PDClient::CheckChunk(boost::shared_ptr<GetArgs> get_args) {
  const boost::shared_ptr<CheckChunkResponse>
      check_chunk_response_(new CheckChunkResponse());
  google::protobuf::Closure* callback =
      google::protobuf::NewCallback(this,
                                    &PDClient::CheckChunkCallback,
                                    check_chunk_response_,
                                    get_args);
#ifdef DEBUG
  printf("\tIn PDClient::CheckChunk, before CheckContactLocalAddress.\n");
#endif
  kad::connect_to_node conn_type =
      knode_->CheckContactLocalAddress(get_args->chunk_holder_.node_id(),
                                       get_args->chunk_holder_.local_ip(),
                                       get_args->chunk_holder_.local_port(),
                                       get_args->chunk_holder_.host_ip());
#ifdef DEBUG
  printf("\tIn PDClient::CheckChunk, after CheckContactLocalAddress.\n");
#endif
  bool local = false;
  std::string ip = get_args->chunk_holder_.host_ip();
  uint16_t port = static_cast<uint16_t>(
                     get_args->chunk_holder_.host_port());
  if (conn_type == kad::LOCAL) {
    ip = get_args->chunk_holder_.local_ip();
    port = get_args->chunk_holder_.local_port();
    local = true;
    get_args->retry_remote = true;
  }
  client_rpcs_->CheckChunk(get_args->data_->chunk_name,
                          ip,
                          port,
                          check_chunk_response_.get(),
                          callback,
                          local);
}

void PDClient::CheckChunkCallback(
    const boost::shared_ptr<CheckChunkResponse> check_chunk_response,
    boost::shared_ptr<GetArgs> get_args) {
  if (get_args->data_->is_callbacked ||
      !knode_->is_joined()) {
#ifdef DEBUG
    if (get_args->data_->is_callbacked)
      printf("Data has already been called back.\n");
    if (!knode_->is_joined())
      printf("Not joined to network.\n");
#endif
    // callback can only be called once
    return;
  }
  if (check_chunk_response->IsInitialized() &&
      check_chunk_response->has_pmid_id() &&
      check_chunk_response->pmid_id() != get_args->chunk_holder_.node_id()) {
    if (get_args->retry_remote) {
      get_args->retry_remote = false;
#ifdef DEBUG
      printf("\tIn PDClient::CheckChunkCallback, before UpdatePDRTContac...\n");
#endif
      knode_->UpdatePDRTContactToRemote(get_args->chunk_holder_.node_id());
#ifdef DEBUG
      printf("\tIn PDClient::CheckChunkCallback, after UpdatePDRTContact...\n");
#endif
      boost::shared_ptr<CheckChunkResponse> resp(new CheckChunkResponse());
      google::protobuf::Closure* done =
          google::protobuf::NewCallback(this,
                                        &PDClient::CheckChunkCallback,
                                        resp,
                                        get_args);
      client_rpcs_->CheckChunk(get_args->data_->chunk_name,
                          get_args->chunk_holder_.host_ip(),
                          get_args->chunk_holder_.host_port(),
                          resp.get(),
                          done,
                          false);
      return;
    }
  }
  if (!check_chunk_response->IsInitialized() ||
      check_chunk_response->result() == kRpcResultFailure) {
#ifdef DEBUG
    printf("Doesn't have the chunk.\n");
#endif
    ++get_args->data_->failed_holders;
    if (get_args->data_->failed_holders >=
        get_args->data_->number_holders) {
      // the chunk references did not respond to the check
      GetResponse local_result;
      std::string local_result_str("");
      local_result.set_result(kRpcResultFailure);
      local_result.SerializeToString(&local_result_str);
#ifdef DEBUG
      printf("check chunkcallback --- callback\n");
#endif
      get_args->data_->cb(local_result_str);
      get_args->data_->is_callbacked = true;
    }
  } else {
    // only send one contact the get chunk request
    if (!get_args->data_->is_active) {
      get_args->data_->is_active = true;
#ifdef DEBUG
      printf("\tIn PDClient::CheckChunkCallback, before CheckContactLoca...\n");
#endif
      kad::connect_to_node conn_type =
        knode_->CheckContactLocalAddress(get_args->chunk_holder_.node_id(),
                                         get_args->chunk_holder_.local_ip(),
                                         get_args->chunk_holder_.local_port(),
                                         get_args->chunk_holder_.host_ip());
#ifdef DEBUG
      printf("\tIn PDClient::CheckChunkCallback, after CheckContactLocal...\n");
#endif
      bool local = false;
      std::string ip = get_args->chunk_holder_.host_ip();
      uint16_t port = static_cast<uint16_t>(
                         get_args->chunk_holder_.host_port());
      if (conn_type == kad::LOCAL) {
        ip = get_args->chunk_holder_.local_ip();
        port = get_args->chunk_holder_.local_port();
        local = true;
        get_args->retry_remote = true;
      }
      // if we're trying to get messages (a buffer packet)
      if (get_args->data_->get_msgs) {
        const boost::shared_ptr<GetMessagesResponse>
            get_messages_response_(new GetMessagesResponse());
        google::protobuf::Closure* callback =
            google::protobuf::NewCallback(this,
                                          &PDClient::GetMessagesCallback,
                                          get_messages_response_,
                                          get_args);
        client_rpcs_->GetMessages(get_args->data_->chunk_name,
                                 get_args->data_->pub_key,
                                 get_args->data_->sig_pub_key,
                                 ip,
                                 port,
                                 get_messages_response_.get(),
                                 callback,
                                 local);
      } else {
       const boost::shared_ptr<GetResponse> get_response_(new GetResponse());
       google::protobuf::Closure* callback =
            google::protobuf::NewCallback(this,
                                          &PDClient::GetChunkCallback,
                                          get_response_,
                                          get_args);
        client_rpcs_->Get(get_args->data_->chunk_name,
                         ip,
                         port,
                         get_response_.get(),
                         callback,
                         local);
      }
    }
  }
}

void PDClient::GetMessages(const std::string &chunk_name,
                           const std::string &public_key,
                           const std::string &signed_public_key,
                           base::callback_func_type cb) {
//  boost::recursive_mutex::scoped_lock guard(*recursive_mutex_);
  boost::shared_ptr<LoadChunkData> data(new LoadChunkData(chunk_name, cb));
  data->get_msgs = true;
  data->pub_key = public_key;
  data->sig_pub_key = signed_public_key;
  FindChunkRef(data);
}

void PDClient::GetChunk(const std::string &chunk_name,
                        base::callback_func_type cb) {
  // preparing the shared pointer with data for the LoadChunk operation
  // boost::recursive_mutex::scoped_lock guard(*recursive_mutex_);
#ifdef DEBUG
  std::string hex;
  base::encode_to_hex(chunk_name, &hex);
  hex = hex.substr(0, 10) + "...";
  printf("In PDClient::GetChunk (%i), chunk_name = %s\n",
         knode_->host_port(), hex.c_str());
#endif
  boost::shared_ptr<LoadChunkData> data(new LoadChunkData(chunk_name, cb));
  FindChunkRef(data);
}

void PDClient::GetMessagesCallback(
    const boost::shared_ptr<GetMessagesResponse> get_messages_response,
    boost::shared_ptr<GetArgs> get_args) {
  if (get_args->data_->is_callbacked)
    return;
  if (get_messages_response->IsInitialized() &&
      get_messages_response->has_pmid_id() &&
      get_messages_response->pmid_id() != get_args->chunk_holder_.node_id()) {
    if (get_args->retry_remote) {
      get_args->retry_remote = false;
#ifdef DEBUG
      printf("\tIn PDClient::GetMessagesCallback, before UpdatePDRTConta...\n");
#endif
      knode_->UpdatePDRTContactToRemote(get_args->chunk_holder_.node_id());
#ifdef DEBUG
      printf("\tIn PDClient::GetMessagesCallback, after UpdatePDRTContac...\n");
#endif
      boost::shared_ptr<GetMessagesResponse> resp(new GetMessagesResponse());
      google::protobuf::Closure* done =
          google::protobuf::NewCallback(this,
                                        &PDClient::GetMessagesCallback,
                                        resp,
                                        get_args);
      client_rpcs_->GetMessages(get_args->data_->chunk_name,
                               get_args->data_->pub_key,
                               get_args->data_->sig_pub_key,
                               get_args->chunk_holder_.host_ip(),
                               get_args->chunk_holder_.host_port(),
                               resp.get(),
                               done,
                               false);
      return;
    }
  }
  if (!get_messages_response->IsInitialized() ||
      get_messages_response->result() == kRpcResultFailure) {
    get_args->data_->failed_chunk_holders.push_back(get_args->chunk_holder_);
    RetryGetChunk(get_args->data_);
  } else {
    get_args->data_->is_callbacked = true;
    std::string result;
    get_messages_response->SerializeToString(&result);
    get_args->data_->cb(result);
  }
}

void PDClient::GetChunkCallback(
    const boost::shared_ptr<GetResponse> get_response,
    boost::shared_ptr<GetArgs> get_args) {
  if (get_args->data_->is_callbacked)
    return;
  if (get_response->IsInitialized() &&
      get_response->has_pmid_id() &&
      get_response->pmid_id() != get_args->chunk_holder_.node_id()) {
    if (get_args->retry_remote) {
      get_args->retry_remote = false;
#ifdef DEBUG
      printf("\tIn PDClient::GetChunkCallback, before UpdatePDRTContactT...\n");
#endif
      knode_->UpdatePDRTContactToRemote(get_args->chunk_holder_.node_id());
#ifdef DEBUG
      printf("\tIn PDClient::GetChunkCallback, after UpdatePDRTContactTo...\n");
#endif
      boost::shared_ptr<GetResponse> resp(new GetResponse());
      google::protobuf::Closure* done =
          google::protobuf::NewCallback(this,
                                        &PDClient::GetChunkCallback,
                                        resp,
                                        get_args);
      client_rpcs_->Get(get_args->data_->chunk_name,
                       get_args->chunk_holder_.host_ip(),
                       get_args->chunk_holder_.host_port(),
                       resp.get(),
                       done,
                       false);
      return;
    }
  }
  if (!get_response->IsInitialized() ||
      get_response->result() == kRpcResultFailure ||
      !get_response->has_content()) {
#ifdef DEBUG
    if (!get_response->IsInitialized())
      printf("Response in GetChunkCallback isn't initialised.\n");
    else if (get_response->result() == kRpcResultFailure)
      printf("GetChunkCallback response came back failed.\n");
    else
      printf("Response has no chunk content.\n");
#endif
    get_args->data_->failed_chunk_holders.push_back(get_args->chunk_holder_);
    RetryGetChunk(get_args->data_);
  } else {
    get_args->data_->is_callbacked = true;
    std::string result;
    get_response->SerializeToString(&result);
    get_args->data_->cb(result);
  }
}

void PDClient::RetryGetChunk(boost::shared_ptr<LoadChunkData> data) {
  if (data->is_callbacked || !knode_->is_joined()) {
    // callback can only be called once
    return;
  }
  bool send_request = false;
  if (data->retry < kMaxChunkLoadRetries) {
    ++data->retry;
    data->number_holders = data->chunk_holders.size() -
      data->failed_chunk_holders.size();
    data->failed_holders = 0;
    data->is_active = false;
    for (int i = 0; i < static_cast<int>(data->chunk_holders.size()); ++i) {
      kad::Contact remote = data->chunk_holders[i];
      bool send = true;
      for (int j = 0; j < static_cast<int>(data->failed_chunk_holders.size())
            && send; ++j)
        if (remote == data->failed_chunk_holders[j])
          send = false;
      if (send) {
        boost::shared_ptr<GetArgs> get_args_(new GetArgs(remote, data));
        CheckChunk(get_args_);
        send_request = true;
      }
    }
  }
  if (!send_request) {
    GetResponse local_result;
    std::string local_result_str("");
    local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    data->is_callbacked = true;
#ifdef DEBUG
    printf("In PDClient::RetryGetChunk, returning kRpcResultFailure.\n");
#endif
    data->cb(local_result_str);
  }
}

void PDClient::FindChunkRef(boost::shared_ptr<LoadChunkData> data) {
#ifdef DEBUG
  printf("\tIn PDClient::FindChunkRef, before FindValue.\n");
#endif
  knode_->FindValue(
      data->chunk_name,
      boost::bind(&PDClient::FindChunkRefCallback, this, _1, data));
#ifdef DEBUG
  printf("\tIn PDClient::FindChunkRef, after FindValue.\n");
#endif
}

void PDClient::FindChunkRefCallback(const std::string &result,
                                    boost::shared_ptr<LoadChunkData> data) {
  if (data->is_callbacked || !knode_->is_joined()) {
#ifdef DEBUG
    if (data->is_callbacked)
      printf("Data is callbacked\n");
    else if (!knode_->is_joined())
      printf("Knode not joined\n");
    // callback can only be called once
#endif
    return;
  }
  kad::FindResponse result_msg;
  if (!result_msg.ParseFromString(result) ||
      result_msg.result() == kRpcResultFailure ||
      result_msg.values_size() == 0) {
    // no chunk references were found
#ifdef DEBUG
    if (!result_msg.ParseFromString(result)) {
      printf("not initialized\n");
    } else if (result_msg.result() == kRpcResultFailure) {
      printf("result = kRpcResultFailure\n");
    } else {
      printf("result_msg.values_size() == 0\n");
    }
    printf("chunk references not found\n");
#endif
    GetResponse local_result;
    std::string local_result_str("");
    local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    data->is_callbacked = true;
    data->cb(local_result_str);
    return;
  }
  data->number_holders = result_msg.values_size();
  bool correct_info(false);
#ifdef DEBUG
  printf("No. de values %d.\n", result_msg.values_size());
#endif
  for (int i = 0; i < result_msg.values_size(); ++i) {
    kad::SignedValue signed_value;
    if (signed_value.ParseFromString(result_msg.values(i))) {
      std::string contact_info = signed_value.value();
      kad::Contact remote;
      if (remote.ParseFromString(contact_info)) {
        data->chunk_holders.push_back(remote);
        correct_info = true;
        boost::shared_ptr<GetArgs> get_args_(new GetArgs(remote, data));
        CheckChunk(get_args_);
      } else {
        --data->number_holders;
      }
    } else {
      --data->number_holders;
    }
  }
  if (!correct_info) {
#ifdef DEBUG
    printf("Could not get contact info from the values retrieved.\n");
#endif
    GetResponse local_result;
    std::string local_result_str("");
    local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    data->is_callbacked = true;
    data->cb(local_result_str);
  }
}

void PDClient::StoreChunk(const std::string &chunk_name,
                          const std::string &content,
                          const std::string &public_key,
                          const std::string &signed_public_key,
                          const std::string &signed_request,
                          const maidsafe::ValueType &data_type,
                          base::callback_func_type cb) {
//  boost::recursive_mutex::scoped_lock guard(*recursive_mutex_);
#ifdef DEBUG
  std::string hex;
  base::encode_to_hex(chunk_name, &hex);
  hex = hex.substr(0, 10) + "...";
  printf("In PDClient::StoreChunk (%i), chunk_name = %s\n", knode_->host_port(),
         hex.c_str());
#endif
  boost::shared_ptr<StoreChunkData> data(new StoreChunkData(chunk_name,
    content, cb, public_key, signed_public_key, signed_request, data_type));
  IterativeStoreChunk(data);
}

void PDClient::IterativeStoreChunk(boost::shared_ptr<StoreChunkData> data) {
  if (data->is_callbacked) {
#ifdef DEBUG
    printf("\tIn PDClient::IterativeStoreChunk, data already callbacked\n");
#endif
    return;
  }
  // enough copies have been stored successfully
  StoreResponse store_chunk_result;
  std::string result_str;
#ifdef DEBUG
  printf("\tIn PDClient::IterativeStoreChunk, stored copies: %d\n",
         data->stored_copies);
#endif
  if (data->stored_copies >= kMinChunkCopies) {
    store_chunk_result.set_result(kRpcResultSuccess);
    store_chunk_result.SerializeToString(&result_str);
    data->is_callbacked = true;
#ifdef DEBUG
    printf("\tIn PDClient::IterativeStoreChunk, store OK; callingback.\n");
#endif
    data->cb(result_str);
#ifdef DEBUG
    printf("nalga de camello 4\n");
#endif
    return;
  }
  int missing_copies = kMinChunkCopies - data->stored_copies;
#ifdef DEBUG
  printf("Copies missing = %i\n", missing_copies);
#endif
  if (static_cast<int>(data->active_contacts.size()) == 0
      && missing_copies > 0) {
    // finished with the active contacts and did not
    // store the minimum number of copies
    ++data->retry;
    if (data->retry > kMaxChunkStoreRetries) {
#ifdef DEBUG
      printf("Total of copies stored: %i\n", data->stored_copies);
#endif
      store_chunk_result.set_result(kRpcResultFailure);
      store_chunk_result.SerializeToString(&result_str);
      data->is_callbacked = true;
      data->cb(result_str);
      return;
    }
    if (kad::kAlpha > missing_copies)
      data->parallelstores = missing_copies;
    else
      data->parallelstores = kad::kAlpha;
    data->active_contacts.clear();
    // data->active_contacts_done = 0;
    // getting contacts
    std::vector<kad::Contact> contacts;
    std::vector<kad::Contact> ex_contacts;
    for (int i = 0; i < static_cast<int>(data->chunk_holders.size()); ++i)
      ex_contacts.push_back(data->chunk_holders[i]);
    for (int i = 0; i < static_cast<int>(data->failed_contacts.size()); ++i)
      ex_contacts.push_back(data->failed_contacts[i]);
    // TODO(Fraser#5#): 2009-03-11 - Replace random function below with one
    //                  which chooses nodes based on RTTs
    GetRandomContacts(data->parallelstores, ex_contacts, &contacts);
    if (contacts.empty()) {
      // No contacts
      store_chunk_result.set_result(kRpcResultFailure);
      store_chunk_result.SerializeToString(&result_str);
      data->is_callbacked = true;
      data->cb(result_str);
      // Initiate the Kademlia joining sequence - perform a search for this
      // node's own ID
#ifdef DEBUG
      printf("\tIn PDClient::IterativeStoreChunk, before FindCloseNodes.\n");
#endif
      knode_->FindCloseNodes(knode_->node_id(), &dummy_callback);
#ifdef DEBUG
      printf("\tIn PDClient::IterativeStoreChunk, after FindCloseNodes.\n");
#endif
      return;
    }
    for (int i = 0; i < static_cast<int>(contacts.size()); ++i)
      data->active_contacts.push_back(contacts[i]);
    data->index = 0;
    for (int i = 0; i < data->parallelstores &&
        data->index < static_cast<int>(data->active_contacts.size()); ++i) {
      ExecuteStoreChunk(data->active_contacts[data->index], 0, data);
      ++data->index;
    }
  }
}

int PDClient::StoreChunkPrep(const std::string &chunkname,
                             const boost::uint64_t &data_size,
                             const std::string &public_key,
                             const std::string &signed_public_key,
                             const std::string &signed_request,
                             const std::string &remote_ip,
                             const uint16_t &remote_port,
                             bool local,
                             const std::string &remote_id) {
//  std::string pmid = SessionSingleton::getInstance()->Id(PMID);
  std::string pmid('9', 64);
  const boost::shared_ptr<StorePrepResponse>
      store_prep_response_(new StorePrepResponse());
  bool store_prep_response_returned(false);
  google::protobuf::Closure* callback = google::protobuf::NewCallback(this,
      &PDClient::StoreChunkPrepCallback, &store_prep_response_returned);
  client_rpcs_->StorePrep(chunkname, data_size, pmid, public_key,
      signed_public_key, signed_request, remote_ip, remote_port,
      store_prep_response_.get(), callback, local);
  int count(0), timeout(10000);
  while (count < timeout && !store_prep_response_returned) {
    count += 10;
    boost::this_thread::sleep(boost::posix_time::milliseconds(10));
  }
  if (store_prep_response_->pmid_id() == remote_id &&
      store_prep_response_->result() == kAck) {
printf("PDClient::StoreChunkPrep returning 0.\n");
    return 0;
  }
printf("PDClient::StoreChunkPrep returning -1.\n");
  return -1;
}

void PDClient::StoreChunkPrepCallback(bool *store_prep_response_returned) {
#ifdef DEBUG
  printf("In PDClient::StoreChunkPrepCallback.\n");
#endif
  *store_prep_response_returned = true;
}

void PDClient::ExecuteStoreChunk(const kad::Contact &remote,
                                 int retry,
                                 boost::shared_ptr<StoreChunkData> data) {
  boost::shared_ptr<SendArgs> send_args_(new SendArgs(remote, retry, data));
#ifdef DEBUG
//  printf("Chunk name: %s\n", hex_.c_str());
//  printf("Chunk content: %s\n", send_args_->data_->content.c_str());
//  printf("Public Key: %s\n", send_args_->data_->pub_key.c_str());
//  printf("Signed Pub Key: %s\n", send_args_->data_->sig_pub_key.c_str());
//  printf("Signed Request: %s\n", send_args_->data_->sig_req.c_str());
//  printf("Data Type: %i\n", send_args_->data_->data_type);
//  printf("Host IP: %s\n", send_args_->chunk_holder_.host_ip().c_str());
  printf("\tIn PDClient::ExecuteStoreChunk, storing to: %i\n",
         send_args_->chunk_holder_.host_port());
  printf("\tIn PDClient::ExecuteStoreChunk, before CheckContactLocalAddr...\n");
#endif
  kad::connect_to_node conn_type =
    knode_->CheckContactLocalAddress(send_args_->chunk_holder_.node_id(),
                                     send_args_->chunk_holder_.local_ip(),
                                     send_args_->chunk_holder_.local_port(),
                                     send_args_->chunk_holder_.host_ip());
#ifdef DEBUG
  printf("\tIn PDClient::ExecuteStoreChunk, after CheckContactLocalAddress.\n");
#endif
  bool local = false;
  std::string ip = send_args_->chunk_holder_.host_ip();
  uint16_t port = static_cast<uint16_t>(send_args_->chunk_holder_.host_port());
  if (conn_type == kad::LOCAL) {
    ip = send_args_->chunk_holder_.local_ip();
    port = send_args_->chunk_holder_.local_port();
    local = true;
    send_args_->retry_remote = true;
  }
  if (StoreChunkPrep(send_args_->data_->chunk_name,
                     send_args_->data_->content.size(),
                     send_args_->data_->pub_key,
                     send_args_->data_->sig_pub_key,
                     send_args_->data_->sig_req,
                     ip,
                     port,
                     local,
                     send_args_->chunk_holder_.node_id()) != 0) {
#ifdef DEBUG
    printf("\tIn PDClient::ExecuteStoreChunk, StoreChunkPrep failed.\n");
#endif
    send_args_->data_->failed_contacts.push_back(send_args_->chunk_holder_);
    IterativeStoreChunk(send_args_->data_);
    return;
  }
  const boost::shared_ptr<StoreResponse> store_response_(new StoreResponse());
  google::protobuf::Closure* callback = google::protobuf::NewCallback(this,
      &PDClient::StoreChunkCallback, store_response_, send_args_);
  client_rpcs_->Store(send_args_->data_->chunk_name,
                     send_args_->data_->content,
                     send_args_->data_->pub_key,
                     send_args_->data_->sig_pub_key,
                     send_args_->data_->sig_req,
                     send_args_->data_->data_type,
                     ip,
                     port,
                     store_response_.get(),
                     callback,
                     local);
}

void PDClient::StoreChunkCallback(
    const boost::shared_ptr<StoreResponse> store_response,
    boost::shared_ptr<SendArgs> send_args) {
#ifdef DEBUG
  printf("\tIn PDClient::StoreChunkCallback\n");
#endif
  if (send_args->data_->is_callbacked || !knode_->is_joined()) {
    // callback can only be called once
    return;
  }
  if (store_response->IsInitialized() &&
      store_response->has_pmid_id() &&
      store_response->pmid_id() != send_args->chunk_holder_.node_id()) {
    if (send_args->retry_remote) {
      send_args->retry_remote = false;
#ifdef DEBUG
      printf("\tIn PDClient::StoreChunkCallback, before UpdatePDRTContac...\n");
#endif
      knode_->UpdatePDRTContactToRemote(send_args->chunk_holder_.node_id());
#ifdef DEBUG
      printf("\tIn PDClient::StoreChunkCallback, after UpdatePDRTContact...\n");
#endif
      boost::shared_ptr<StoreResponse> resp(new StoreResponse());
      google::protobuf::Closure* done =
          google::protobuf::NewCallback(this,
                                        &PDClient::StoreChunkCallback,
                                        resp,
                                        send_args);
      client_rpcs_->Store(send_args->data_->chunk_name,
                         send_args->data_->content,
                         send_args->data_->pub_key,
                         send_args->data_->sig_pub_key,
                         send_args->data_->sig_req,
                         send_args->data_->data_type,
                         send_args->chunk_holder_.host_ip(),
                         send_args->chunk_holder_.host_port(),
                         resp.get(),
                         done,
                         false);
      return;
    }
  }
  // remove it from the active list
  for (int i = 0;
      i < static_cast<int>(send_args->data_->active_contacts.size()); i++) {
    if (send_args->data_->active_contacts[i] == send_args->chunk_holder_) {
#ifdef DEBUG
      printf("\tIn PDClient::StoreChunkCallback, cancelled active contact.\n");
#endif
      send_args->data_->active_contacts.erase(
        send_args->data_->active_contacts.begin() + i);
      break;
    }
  }
  // ++send_args->data_->active_contacts_done;
  if (!store_response->IsInitialized()) {
#ifdef DEBUG
    printf("\tIn PDClient::StoreChunkCallback, response is not initialized.\n");
#endif
    send_args->data_->failed_contacts.push_back(send_args->chunk_holder_);
    // ++send_args->data_->active_contacts_done;
    IterativeStoreChunk(send_args->data_);
    return;
  }
#ifdef DEBUG
  printf("\tIn PDClient::StoreChunkCallback, response is initialized.\n");
#endif
  if (store_response->result() != kRpcResultSuccess) {
    ++send_args->retry_;
    if (send_args->retry_ < kMaxChunkStoreRetries) {
#ifdef DEBUG
      printf("\tIn PDClient::StoreChunkCallback, store failed - retry no.%i.\n",
             send_args->retry_);
#endif
      send_args->data_->active_contacts.push_back(send_args->chunk_holder_);
      ExecuteStoreChunk(send_args->chunk_holder_,
                        send_args->retry_,
                        send_args->data_);
      return;
    }
    send_args->data_->failed_contacts.push_back(send_args->chunk_holder_);
#ifdef DEBUG
    printf("\tIn PDClient::StoreChunkCallback, store failed; no more tries.\n");
#endif
    // ++send_args->data_->active_contacts_done;
  } else {
    ++send_args->data_->stored_copies;
    send_args->data_->chunk_holders.push_back(send_args->chunk_holder_);
#ifdef DEBUG
    printf("\tIn PDClient::StoreChunkCallback, store succeeded.\n");
#endif
    // ++send_args->data_->active_contacts_done;
  }
  IterativeStoreChunk(send_args->data_);
}

void PDClient::GetRandomContacts(
    const int &count,
    const std::vector<kad::Contact> &exclude_contacts,
    std::vector<kad::Contact> *contacts) {
#ifdef DEBUG
  printf("\tIn PDClient::GetRandomContacts, before GetRandomContacts.\n");
#endif
  knode_->GetRandomContacts(count, exclude_contacts, contacts);
#ifdef DEBUG
  printf("\tIn PDClient::GetRandomContacts, after GetRandomContacts.\n");
#endif
}

void PDClient::UpdateChunk(const std::string &chunk_name,
                           const std::string &content,
                           const std::string &public_key,
                           const std::string &signed_public_key,
                           const std::string &signed_request,
                           const maidsafe::ValueType &data_type,
                           base::callback_func_type cb) {
//  boost::recursive_mutex::scoped_lock guard(*recursive_mutex_);
#ifdef DEBUG
  std::string hex;
  base::encode_to_hex(chunk_name, &hex);
  hex = hex.substr(0, 10) + "...";
  printf("In PDClient::UpdateChunk (%i), chunk_name = %s\n",
         knode_->host_port(), hex.c_str());
#endif
  // Look up the chunk references
  knode_->FindValue(chunk_name,
                    boost::bind(&PDClient::IterativeCheckAlive,
                                this,
                                _1,
                                chunk_name,
                                content,
                                public_key,
                                signed_public_key,
                                signed_request,
                                data_type,
                                cb));
#ifdef DEBUG
  printf("\tIn PDClient::UpdateChunk, after FindValue.\n");
#endif
}

void PDClient::IterativeCheckAlive(const std::string &result,
                                   std::string chunk_name,
                                   std::string content,
                                   std::string public_key,
                                   std::string signed_public_key,
                                   std::string signed_request,
                                   maidsafe::ValueType data_type,
                                   base::callback_func_type cb) {
  kad::FindResponse result_msg;
  if (!result_msg.ParseFromString(result) ||
      result_msg.result() == kRpcResultFailure ||
      result_msg.values_size() == 0) {
#ifdef DEBUG
    printf("No chunk reference found.\n");
#endif
    // no chunk references were found
    UpdateResponse local_result;
    std::string local_result_str("");
    local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    cb(local_result_str);
    return;
  }
  // Get the alive chunk holders by using simple ping operation
  boost::shared_ptr<UpdateChunkData>
      data(new UpdateChunkData(chunk_name,
                               content,
                               cb,
                               public_key,
                               signed_public_key,
                               signed_request,
                               data_type));
  bool correct_info(false);
  for (int i = 0; i < result_msg.values_size(); ++i) {
    kad::SignedValue signed_value;
    if (signed_value.ParseFromString(result_msg.values(i))) {
      std::string contact_info = signed_value.value();
      kad::Contact remote;
      if (remote.ParseFromString(contact_info)) {
        data->chunk_holders.push_back(remote);
        correct_info = true;
#ifdef DEBUG
        printf("\tIn PDClient::IterativeCheckAlive, before Ping.\n");
#endif
        knode_->Ping(remote, boost::bind(&PDClient::IterativeCheckAliveCallback,
                                        this,
                                        _1,
                                        1,
                                        remote,
                                        data));
#ifdef DEBUG
        printf("\tIn PDClient::IterativeCheckAlive, after Ping.\n");
#endif
      }
    }
  }
  if (!correct_info) {
#ifdef DEBUG
    printf("No valid chunk reference found.\n");
#endif
    UpdateResponse local_result;
    std::string local_result_str("");
    local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    data->is_callbacked = true;
    data->cb(local_result_str);
  }
}

void PDClient::IterativeCheckAliveCallback(
    const std::string &result,
    int retry,
    kad::Contact remote,
    boost::shared_ptr<UpdateChunkData> data) {
  if (data->is_callbacked) {
    return;
  }
  kad::PingResponse result_msg;
  if ((!result_msg.ParseFromString(result)) ||
      (result_msg.result() == kRpcResultFailure)) {
    // we assume the node to be dead if it fails to respond to kMaxPingRetries
    // consecutive pings
    if (retry > kMaxPingRetries) {
      // dead node
      ++data->contacted_holders;
      IterativeUpdateChunk(data);
    } else {  // ping again
#ifdef DEBUG
      printf("\tIn PDClient::IterativeCheckAliveCallback, before Ping.\n");
#endif
      knode_->Ping(remote, boost::bind(&PDClient::IterativeCheckAliveCallback,
                                      this,
                                      _1,
                                      ++retry,
                                      remote,
                                      data));
#ifdef DEBUG
      printf("\tIn PDClient::IterativeCheckAliveCallback, after Ping.\n");
#endif
    }
  } else {  // alive contacts
    data->alive_holders.push_back(remote);
    IterativeUpdateChunk(data);
  }
}

void PDClient::IterativeUpdateChunk(boost::shared_ptr<UpdateChunkData> data) {
  if (data->contacted_holders ==
      static_cast<int>(data->chunk_holders.size())) {
    // It's time to finish chunk updating
    UpdateResponse local_result;
    std::string local_result_str("");
    if (data->updated_copies >= kMinChunkCopies ||
        data->updated_copies == static_cast<int>(data->alive_holders.size()))
      local_result.set_result(kRpcResultSuccess);
    else
      local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    data->is_callbacked = true;
    data->cb(local_result_str);
    return;
  }
  // let's go for the next chunk updating
  if (data->active_updating < kad::kAlpha
      && data->index < static_cast<int>(data->alive_holders.size())) {
    kad::Contact remote = data->alive_holders[data->index];
    ++data->index;
    ++data->active_updating;
    boost::shared_ptr<UpdateArgs>
        update_args_(new UpdateArgs(remote, 0, data));
    const boost::shared_ptr<UpdateResponse>
        update_response_(new UpdateResponse());
    google::protobuf::Closure* callback =
        google::protobuf::NewCallback(this,
                                      &PDClient::IterativeUpdateChunkCallback,
                                      update_response_,
                                      update_args_);
#ifdef DEBUG
    printf("\tIn PDClient::IterativeUpdateChunk, before CheckContactLoca...\n");
#endif
    kad::connect_to_node conn_type =
      knode_->CheckContactLocalAddress(update_args_->chunk_holder_.node_id(),
                                       update_args_->chunk_holder_.local_ip(),
                                       update_args_->chunk_holder_.local_port(),
                                       update_args_->chunk_holder_.host_ip());
#ifdef DEBUG
    printf("\tIn PDClient::IterativeUpdateChunk, after CheckContactLocal...\n");
#endif
    bool local = false;
    std::string ip = update_args_->chunk_holder_.host_ip();
    uint16_t port = static_cast<uint16_t>(
                        update_args_->chunk_holder_.host_port());
    if (conn_type == kad::LOCAL) {
      ip = update_args_->chunk_holder_.local_ip();
      port = update_args_->chunk_holder_.local_port();
      local = true;
      update_args_->retry_remote = true;
    }
    client_rpcs_->Update(update_args_->data_->chunk_name,
                        update_args_->data_->content,
                        update_args_->data_->pub_key,
                        update_args_->data_->sig_pub_key,
                        update_args_->data_->sig_req,
                        update_args_->data_->data_type,
                        ip,
                        port,
                        update_response_.get(),
                        callback,
                        local);
  }
}

void PDClient::IterativeUpdateChunkCallback(
    const boost::shared_ptr<UpdateResponse> update_response,
    boost::shared_ptr<UpdateArgs> update_args) {
  if (update_args->data_->is_callbacked || !knode_->is_joined()) {
    return;
  }
  if (update_response->IsInitialized() &&
      update_response->has_pmid_id() &&
      update_response->pmid_id() != update_args->chunk_holder_.node_id()) {
    if (update_args->retry_remote) {
      update_args->retry_remote = false;
#ifdef DEBUG
      printf("\tIn PDClient::IterativeUpdateChunkCallback, before Update...\n");
#endif
      knode_->UpdatePDRTContactToRemote(update_args->chunk_holder_.node_id());
#ifdef DEBUG
      printf("\tIn PDClient::IterativeUpdateChunkCallback, after UpdateP...\n");
#endif
      boost::shared_ptr<UpdateResponse> resp(new UpdateResponse());
      google::protobuf::Closure* done =
          google::protobuf::NewCallback(this,
                                        &PDClient::IterativeUpdateChunkCallback,
                                        resp,
                                        update_args);
      client_rpcs_->Update(update_args->data_->chunk_name,
                          update_args->data_->content,
                          update_args->data_->pub_key,
                          update_args->data_->sig_pub_key,
                          update_args->data_->sig_req,
                          update_args->data_->data_type,
                          update_args->chunk_holder_.host_ip(),
                          update_args->chunk_holder_.host_port(),
                          resp.get(),
                          done,
                          false);
      return;
    }
  }
  if (!update_response->IsInitialized() ||
      update_response->result() == kRpcResultFailure) {
    if (update_args->retry_ > kMaxChunkStoreRetries) {
#ifdef DEBUG
      printf("Failed to update chunk.\n");
#endif
      --update_args->data_->active_updating;
      ++update_args->data_->contacted_holders;
      IterativeUpdateChunk(update_args->data_);
    } else {
#ifdef DEBUG
      printf("Re-trying to update chunk.\n");
#endif
      ++update_args->retry_;
      const boost::shared_ptr<UpdateResponse>
          update_response_(new UpdateResponse());
      google::protobuf::Closure* callback =
          google::protobuf::NewCallback(this,
                                        &PDClient::IterativeUpdateChunkCallback,
                                        update_response_,
                                        update_args);
#ifdef DEBUG
      printf("\tIn PDClient::IterativeUpdateChunkCallback, before CheckC...\n");
#endif
      kad::connect_to_node conn_type =
      knode_->CheckContactLocalAddress(update_args->chunk_holder_.node_id(),
                                       update_args->chunk_holder_.local_ip(),
                                       update_args->chunk_holder_.local_port(),
                                       update_args->chunk_holder_.host_ip());
#ifdef DEBUG
      printf("\tIn PDClient::IterativeUpdateChunkCallback, after CheckCo...\n");
#endif
      bool local = false;
      std::string ip = update_args->chunk_holder_.host_ip();
      uint16_t port = static_cast<uint16_t>(
                          update_args->chunk_holder_.host_port());
      if (conn_type == kad::LOCAL) {
        ip = update_args->chunk_holder_.local_ip();
        port = update_args->chunk_holder_.local_port();
        local = true;
        update_args->retry_remote = true;
      }
      client_rpcs_->Update(update_args->data_->chunk_name,
                          update_args->data_->content,
                          update_args->data_->pub_key,
                          update_args->data_->sig_pub_key,
                          update_args->data_->sig_req,
                          update_args->data_->data_type,
                          ip,
                          port,
                          update_response_.get(),
                          callback,
                          local);
    }
  } else {
#ifdef DEBUG
    printf("Updated chunk successfully.\n");
#endif
    --update_args->data_->active_updating;
    ++update_args->data_->contacted_holders;
    ++update_args->data_->updated_copies;
    IterativeUpdateChunk(update_args->data_);
  }
}

void PDClient::DeleteChunk(const std::string &chunk_name,
                           const std::string &public_key,
                           const std::string &signed_public_key,
                           const std::string &signed_request,
                           const maidsafe::ValueType &data_type,
                           base::callback_func_type cb) {
  // verify the chunk size
  // Look up the chunk references
#ifdef DEBUG
  printf("\tIn PDClient::DeleteChunk, before FindValue.\n");
#endif
  knode_->FindValue(chunk_name,
                    boost::bind(&PDClient::DeleteChunk_IterativeCheckAlive,
                                this,
                                _1,
                                chunk_name,
                                public_key,
                                signed_public_key,
                                signed_request,
                                data_type,
                                cb));
#ifdef DEBUG
  printf("\tIn PDClient::DeleteChunk, after FindValue.\n");
#endif
}

void PDClient::DeleteChunk_IterativeCheckAlive(const std::string &result,
                                               std::string non_hex_chunk_name,
                                               std::string public_key,
                                               std::string signed_public_key,
                                               std::string signed_request,
                                               maidsafe::ValueType data_type,
                                               base::callback_func_type cb) {
  kad::FindResponse result_msg;
  if (!result_msg.ParseFromString(result) ||
      result_msg.result() == kRpcResultFailure ||
      result_msg.values_size() == 0) {
    // no chunk references were found
    DeleteResponse local_result;
    std::string local_result_str("");
    local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    cb(local_result_str);
    return;
  }
  // Get the alive chunk holders by using simple ping operation
  boost::shared_ptr<DeleteChunkData>
      data(new DeleteChunkData(non_hex_chunk_name,
                               cb,
                               public_key,
                               signed_public_key,
                               signed_request,
                               data_type));
  bool correct_info = false;
  for (int i = 0; i < result_msg.values_size(); ++i) {
    kad::SignedValue signed_value;
    if (signed_value.ParseFromString(result_msg.values(i))) {
      std::string contact_info = signed_value.value();
      kad::Contact remote;
      if (remote.ParseFromString(contact_info)) {
        data->chunk_holders.push_back(remote);
        correct_info = true;
#ifdef DEBUG
        printf("\tIn PDClient::DeleteChunk_IterativeCheckAlive, before Ping\n");
#endif
        knode_->Ping(remote,
                     boost::bind(&PDClient::DeleteChunk_CheckAliveCallback,
                                 this,
                                 _1,
                                 1,
                                 remote,
                                 data));
#ifdef DEBUG
        printf("\tIn PDClient::DeleteChunk_IterativeCheckAlive, after Ping.\n");
#endif
      }
    }
  }
  if (!correct_info) {
    DeleteResponse local_result;
    std::string local_result_str("");
    local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    data->is_callbacked = true;
    data->cb(local_result_str);
  }
}

void PDClient::DeleteChunk_CheckAliveCallback(
    const std::string &result,
    int retry,
    kad::Contact remote,
    boost::shared_ptr<DeleteChunkData> data) {
  if (data->is_callbacked) {
    return;
  }
  kad::PingResponse result_msg;
  if ((!result_msg.ParseFromString(result)) ||
      (result_msg.result() == kRpcResultFailure)) {
    // we assume the node to be dead if it fails to respond to kMaxPingRetries
    // consecutive pings
    if (retry > kMaxPingRetries) {
      // dead node
      ++data->contacted_holders;
      DeleteChunk_IterativeDeleteChunk(data);
    } else {  // ping again
#ifdef DEBUG
      printf("\tIn PDClient::DeleteChunk_CheckAliveCallback, before Ping.\n");
#endif
      knode_->Ping(remote,
                   boost::bind(&PDClient::DeleteChunk_CheckAliveCallback,
                               this,
                               _1,
                               ++retry,
                               remote,
                               data));
#ifdef DEBUG
      printf("\tIn PDClient::DeleteChunk_CheckAliveCallback, after Ping.\n");
#endif
    }
  } else {  // alive contacts
    data->alive_holders.push_back(remote);
    DeleteChunk_IterativeDeleteChunk(data);
  }
}

void PDClient::DeleteChunk_IterativeDeleteChunk(
    boost::shared_ptr<DeleteChunkData> data) {
  if (data->contacted_holders ==
      static_cast<int>(data->chunk_holders.size())) {
    DeleteResponse local_result;
    std::string local_result_str("");
    if (data->deleted_copies >= kMinChunkCopies ||
        data->deleted_copies == static_cast<int>(data->alive_holders.size()))
      local_result.set_result(kRpcResultSuccess);
    else
      local_result.set_result(kRpcResultFailure);
    local_result.SerializeToString(&local_result_str);
    data->is_callbacked = true;
    data->cb(local_result_str);
    return;
  }
  // let's go for the next chunk deletion
  if (data->active_deleting < kad::kAlpha
      && data->index < static_cast<int>(data->alive_holders.size())) {
    kad::Contact remote = data->alive_holders[data->index];
    ++data->index;
    ++data->active_deleting;
    boost::shared_ptr<DeleteArgs>
        delete_args_(new DeleteArgs(remote, data));
    const boost::shared_ptr<DeleteResponse>
        delete_response_(new DeleteResponse());
    google::protobuf::Closure* callback =
        google::protobuf::NewCallback(
            this,
            &PDClient::DeleteChunk_DeleteChunkCallback,
            delete_response_,
            delete_args_);
#ifdef DEBUG
    printf("\tIn PDClient::DeleteChunk_IterativeDeleteChunk, before Chec...\n");
#endif
    kad::connect_to_node conn_type =
      knode_->CheckContactLocalAddress(delete_args_->chunk_holder_.node_id(),
                                       delete_args_->chunk_holder_.local_ip(),
                                       delete_args_->chunk_holder_.local_port(),
                                       delete_args_->chunk_holder_.host_ip());
#ifdef DEBUG
    printf("\tIn PDClient::DeleteChunk_IterativeDeleteChunk, after Chec....\n");
#endif
    bool local = false;
    std::string ip = delete_args_->chunk_holder_.host_ip();
    uint16_t port = static_cast<uint16_t>(
                        delete_args_->chunk_holder_.host_port());
    if (conn_type == kad::LOCAL) {
      ip = delete_args_->chunk_holder_.local_ip();
      port = delete_args_->chunk_holder_.local_port();
      local = true;
      delete_args_->retry_remote = true;
    }
    client_rpcs_->Delete(delete_args_->data_->chunk_name,
                        delete_args_->data_->pub_key,
                        delete_args_->data_->sig_pub_key,
                        delete_args_->data_->sig_req,
                        delete_args_->data_->data_type,
                        ip,
                        port,
                        delete_response_.get(),
                        callback,
                        local);
  }
}

void PDClient::DeleteChunk_DeleteChunkCallback(
      const boost::shared_ptr<DeleteResponse> delete_response,
      boost::shared_ptr<DeleteArgs> delete_args) {
  if (delete_args->data_->is_callbacked) {
    return;
  }
  if (delete_response->IsInitialized() &&
      delete_response->has_pmid_id() &&
      delete_response->pmid_id() != delete_args->chunk_holder_.node_id()) {
    if (delete_args->retry_remote) {
      delete_args->retry_remote = false;
#ifdef DEBUG
      printf("\tIn PDClient::DeleteChunk_DeleteChunkCallback, before Up....\n");
#endif
      knode_->UpdatePDRTContactToRemote(delete_args->chunk_holder_.node_id());
#ifdef DEBUG
      printf("\tIn PDClient::DeleteChunk_DeleteChunkCallback, after Upd....\n");
#endif
      boost::shared_ptr<DeleteResponse> resp(new DeleteResponse());
      google::protobuf::Closure* done =
          google::protobuf::NewCallback(this,
                                    &PDClient::DeleteChunk_DeleteChunkCallback,
                                    resp,
                                    delete_args);
      client_rpcs_->Delete(delete_args->data_->chunk_name,
                          delete_args->data_->pub_key,
                          delete_args->data_->sig_pub_key,
                          delete_args->data_->sig_req,
                          delete_args->data_->data_type,
                          delete_args->chunk_holder_.host_ip(),
                          delete_args->chunk_holder_.host_port(),
                          resp.get(),
                          done,
                          false);
      return;
    }
  }
  if (!delete_response->IsInitialized() ||
      delete_response->result() == kRpcResultFailure) {
    // failed to delete ...
    // we don't retry with same arguments to delete
    --delete_args->data_->active_deleting;
    ++delete_args->data_->contacted_holders;
    DeleteChunk_IterativeDeleteChunk(delete_args->data_);
  } else {  // update chunk successfully
    --delete_args->data_->active_deleting;
    ++delete_args->data_->contacted_holders;
    ++delete_args->data_->deleted_copies;
    DeleteChunk_IterativeDeleteChunk(delete_args->data_);
  }
}

void PDClient::FindValue(const std::string &key,
                         base::callback_func_type cb) {
#ifdef DEBUG
  printf("\tIn PDClient::FindValue, before FindValue.\n");
#endif
  knode_->FindValue(key, cb);
#ifdef DEBUG
  printf("\tIn PDClient::FindValue, after FindValue.\n");
#endif
}

}  // namespace maidsafe
