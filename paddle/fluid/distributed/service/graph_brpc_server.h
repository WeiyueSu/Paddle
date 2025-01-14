// Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"

#include <memory>
#include <vector>
#include "paddle/fluid/distributed/service/brpc_ps_server.h"
#include "paddle/fluid/distributed/service/server.h"

namespace paddle {
namespace distributed {
class GraphBrpcServer : public PSServer {
 public:
  GraphBrpcServer() {}
  virtual ~GraphBrpcServer() {}
  virtual uint64_t start(const std::string &ip, uint32_t port);
  virtual int32_t stop() {
    std::unique_lock<std::mutex> lock(mutex_);
    stoped_ = true;
    cv_.notify_all();

    _server.Stop(1000);
    _server.Join();
    return 0;
  }
  virtual int32_t port();

 private:
  virtual int32_t initialize();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stoped_ = false;
  brpc::Server _server;
  std::shared_ptr<PsBaseService> _service;
  std::vector<std::shared_ptr<brpc::Channel>> _pserver_channels;
};
// class GraphBrpcServer : public GraphBrpcServer {
//  public:
//   GraphBrpcServer() {}
//   virtual ~GraphBrpcServer() {}

//  private:
//   virtual int32_t initialize();
// };
class GraphBrpcService;

typedef int32_t (GraphBrpcService::*serviceFunc)(
    Table *table, const PsRequestMessage &request, PsResponseMessage &response,
    brpc::Controller *cntl);

class GraphBrpcService : public PsBaseService {
 public:
  virtual int32_t initialize() override;

  virtual void service(::google::protobuf::RpcController *controller,
                       const PsRequestMessage *request,
                       PsResponseMessage *response,
                       ::google::protobuf::Closure *done) override;

 protected:
  std::unordered_map<int32_t, serviceFunc> _service_handler_map;
  int32_t initialize_shard_info();
  int32_t pull_graph_list(Table *table, const PsRequestMessage &request,
                          PsResponseMessage &response, brpc::Controller *cntl);
  int32_t graph_random_sample(Table *table, const PsRequestMessage &request,
                              PsResponseMessage &response,
                              brpc::Controller *cntl);
  int32_t barrier(Table *table, const PsRequestMessage &request,
                  PsResponseMessage &response, brpc::Controller *cntl);
  int32_t load_one_table(Table *table, const PsRequestMessage &request,
                         PsResponseMessage &response, brpc::Controller *cntl);
  int32_t load_all_table(Table *table, const PsRequestMessage &request,
                         PsResponseMessage &response, brpc::Controller *cntl);
  int32_t stop_server(Table *table, const PsRequestMessage &request,
                      PsResponseMessage &response, brpc::Controller *cntl);
  int32_t start_profiler(Table *table, const PsRequestMessage &request,
                         PsResponseMessage &response, brpc::Controller *cntl);
  int32_t stop_profiler(Table *table, const PsRequestMessage &request,
                        PsResponseMessage &response, brpc::Controller *cntl);

  int32_t print_table_stat(Table *table, const PsRequestMessage &request,
                           PsResponseMessage &response, brpc::Controller *cntl);

 private:
  bool _is_initialize_shard_info;
  std::mutex _initialize_shard_mutex;
  std::unordered_map<int32_t, serviceHandlerFunc> _msg_handler_map;
  std::vector<float> _ori_values;
};
// class GraphBrpcService : public BrpcPsService {
//  public:
//   virtual int32_t initialize() override;
//   int32_t pull_graph_list(Table *table, const PsRequestMessage &request,
//                            PsResponseMessage &response, brpc::Controller
//                            *cntl);
// int32_t graph_random_sample(Table *table, const PsRequestMessage &request,
//                            PsResponseMessage &response, brpc::Controller
//                            *cntl);
// };

}  // namespace distributed
}  // namespace paddle
