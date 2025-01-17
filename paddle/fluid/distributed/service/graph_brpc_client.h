// Copyright (c) 2020 PaddlePaddle Authors. All Rights Reserved.
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

#include <memory>
#include <string>
#include <vector>

#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "paddle/fluid/distributed/service/brpc_ps_client.h"
#include "paddle/fluid/distributed/service/ps_client.h"
#include "paddle/fluid/distributed/table/table.h"
#include "paddle/fluid/framework/lod_tensor.h"
#include "paddle/fluid/framework/scope.h"
#include "paddle/fluid/framework/tensor_util.h"

namespace paddle {
namespace distributed {

class GraphBrpcClient : public BrpcPsClient {
 public:
  GraphBrpcClient() {}
  virtual ~GraphBrpcClient() {}
  virtual std::future<int32_t> sample(uint32_t table_id, uint64_t node_id,
                                      int sample_size,
                                      std::vector<GraphNode> &res);
  virtual std::future<int32_t> pull_graph_list(uint32_t table_id,
                                               int server_index, int start,
                                               int size,
                                               std::vector<GraphNode> &res);
  virtual int32_t initialize();
  int get_shard_num() { return shard_num; }
  void set_shard_num(int shard_num) { this->shard_num = shard_num; }
  int get_server_index_by_id(uint64_t id);

 private:
  int shard_num;
  size_t server_size;
};

}  // namespace distributed
}  // namespace paddle
