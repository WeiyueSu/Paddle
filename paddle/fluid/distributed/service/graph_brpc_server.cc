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

#include "paddle/fluid/distributed/service/graph_brpc_server.h"
#include "paddle/fluid/distributed/service/brpc_ps_server.h"

#include <thread>  // NOLINT
#include "butil/endpoint.h"
#include "iomanip"
#include "paddle/fluid/distributed/table/table.h"
#include "paddle/fluid/framework/archive.h"
#include "paddle/fluid/platform/profiler.h"
namespace paddle {
namespace distributed {

int32_t GraphBrpcServer::initialize() {
  auto &service_config = _config.downpour_server_param().service_param();
  if (!service_config.has_service_class()) {
    LOG(ERROR) << "miss service_class in ServerServiceParameter";
    return -1;
  }
  auto *service =
      CREATE_PSCORE_CLASS(PsBaseService, service_config.service_class());
  if (service == NULL) {
    LOG(ERROR) << "service is unregistered, service_name:"
               << service_config.service_class();
    return -1;
  }

  _service.reset(service);
  if (service->configure(this) != 0 || service->initialize() != 0) {
    LOG(ERROR) << "service initialize failed, service_name:"
               << service_config.service_class();
    return -1;
  }
  if (_server.AddService(service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
    LOG(ERROR) << "service add to brpc failed, service:"
               << service_config.service_class();
    return -1;
  }
  return 0;
}

uint64_t GraphBrpcServer::start(const std::string &ip, uint32_t port) {
  std::unique_lock<std::mutex> lock(mutex_);

  std::string ip_port = ip + ":" + std::to_string(port);
  VLOG(3) << "server of rank " << _rank << " starts at " << ip_port;
  brpc::ServerOptions options;

  int num_threads = std::thread::hardware_concurrency();
  auto trainers = _environment->get_trainers();
  options.num_threads = trainers > num_threads ? trainers : num_threads;

  if (_server.Start(ip_port.c_str(), &options) != 0) {
    LOG(ERROR) << "GraphBrpcServer start failed, ip_port=" << ip_port;
    return 0;
  }
  VLOG(0) << "GraphBrpcServer::start registe_ps_server";
  _environment->registe_ps_server(ip, port, _rank);
  VLOG(0) << "GraphBrpcServer::start wait";
  cv_.wait(lock, [&] { return stoped_; });

  PSHost host;
  host.ip = ip;
  host.port = port;
  host.rank = _rank;
  VLOG(0) << "GraphBrpcServer::start return host.rank";
  return host.rank;
}

int32_t GraphBrpcServer::port() { return _server.listen_address().port; }

int32_t GraphBrpcService::initialize() {
  _is_initialize_shard_info = false;
  _service_handler_map[PS_STOP_SERVER] = &GraphBrpcService::stop_server;
  _service_handler_map[PS_LOAD_ONE_TABLE] = &GraphBrpcService::load_one_table;
  _service_handler_map[PS_LOAD_ALL_TABLE] = &GraphBrpcService::load_all_table;

  _service_handler_map[PS_PRINT_TABLE_STAT] =
      &GraphBrpcService::print_table_stat;
  _service_handler_map[PS_BARRIER] = &GraphBrpcService::barrier;
  _service_handler_map[PS_START_PROFILER] = &GraphBrpcService::start_profiler;
  _service_handler_map[PS_STOP_PROFILER] = &GraphBrpcService::stop_profiler;

  _service_handler_map[PS_PULL_GRAPH_LIST] = &GraphBrpcService::pull_graph_list;
  _service_handler_map[PS_GRAPH_SAMPLE] =
      &GraphBrpcService::graph_random_sample;

  // shard初始化,server启动后才可从env获取到server_list的shard信息
  initialize_shard_info();

  return 0;
}

#define CHECK_TABLE_EXIST(table, request, response)        \
  if (table == NULL) {                                     \
    std::string err_msg("table not found with table_id:"); \
    err_msg.append(std::to_string(request.table_id()));    \
    set_response_code(response, -1, err_msg.c_str());      \
    return -1;                                             \
  }

int32_t GraphBrpcService::initialize_shard_info() {
  if (!_is_initialize_shard_info) {
    std::lock_guard<std::mutex> guard(_initialize_shard_mutex);
    if (_is_initialize_shard_info) {
      return 0;
    }
    size_t shard_num = _server->environment()->get_ps_servers().size();
    auto &table_map = *(_server->table());
    for (auto itr : table_map) {
      itr.second->set_shard(_rank, shard_num);
    }
    _is_initialize_shard_info = true;
  }
  return 0;
}

void GraphBrpcService::service(google::protobuf::RpcController *cntl_base,
                               const PsRequestMessage *request,
                               PsResponseMessage *response,
                               google::protobuf::Closure *done) {
  brpc::ClosureGuard done_guard(done);
  std::string log_label("ReceiveCmd-");
  if (!request->has_table_id()) {
    set_response_code(*response, -1, "PsRequestMessage.tabel_id is required");
    return;
  }

  response->set_err_code(0);
  response->set_err_msg("");
  auto *table = _server->table(request->table_id());
  brpc::Controller *cntl = static_cast<brpc::Controller *>(cntl_base);
  auto itr = _service_handler_map.find(request->cmd_id());
  if (itr == _service_handler_map.end()) {
    std::string err_msg(
        "undefined cmd_id, should match PsCmdID in ps.proto, cmd_id:");
    err_msg.append(std::to_string(request->cmd_id()));
    set_response_code(*response, -1, err_msg.c_str());
    return;
  }
  serviceFunc handler_func = itr->second;
  int service_ret = (this->*handler_func)(table, *request, *response, cntl);
  if (service_ret != 0) {
    response->set_err_code(service_ret);
    response->set_err_msg("server internal error");
  }
}

int32_t GraphBrpcService::barrier(Table *table, const PsRequestMessage &request,
                                  PsResponseMessage &response,
                                  brpc::Controller *cntl) {
  CHECK_TABLE_EXIST(table, request, response)

  if (request.params_size() < 1) {
    set_response_code(response, -1,
                      "PsRequestMessage.params is requeired at "
                      "least 1 for num of sparse_key");
    return 0;
  }

  auto trainer_id = request.client_id();
  auto barrier_type = request.params(0);
  table->barrier(trainer_id, barrier_type);
  return 0;
}

int32_t GraphBrpcService::print_table_stat(Table *table,
                                           const PsRequestMessage &request,
                                           PsResponseMessage &response,
                                           brpc::Controller *cntl) {
  CHECK_TABLE_EXIST(table, request, response)
  std::pair<int64_t, int64_t> ret = table->print_table_stat();
  paddle::framework::BinaryArchive ar;
  ar << ret.first << ret.second;
  std::string table_info(ar.Buffer(), ar.Length());
  response.set_data(table_info);

  return 0;
}

int32_t GraphBrpcService::load_one_table(Table *table,
                                         const PsRequestMessage &request,
                                         PsResponseMessage &response,
                                         brpc::Controller *cntl) {
  CHECK_TABLE_EXIST(table, request, response)
  if (request.params_size() < 2) {
    set_response_code(
        response, -1,
        "PsRequestMessage.datas is requeired at least 2 for path & load_param");
    return -1;
  }
  if (table->load(request.params(0), request.params(1)) != 0) {
    set_response_code(response, -1, "table load failed");
    return -1;
  }
  return 0;
}

int32_t GraphBrpcService::load_all_table(Table *table,
                                         const PsRequestMessage &request,
                                         PsResponseMessage &response,
                                         brpc::Controller *cntl) {
  auto &table_map = *(_server->table());
  for (auto &itr : table_map) {
    if (load_one_table(itr.second.get(), request, response, cntl) != 0) {
      LOG(ERROR) << "load table[" << itr.first << "] failed";
      return -1;
    }
  }
  return 0;
}

int32_t GraphBrpcService::stop_server(Table *table,
                                      const PsRequestMessage &request,
                                      PsResponseMessage &response,
                                      brpc::Controller *cntl) {
  auto *p_server = _server;
  std::thread t_stop([p_server]() {
    p_server->stop();
    LOG(INFO) << "Server Stoped";
  });
  t_stop.detach();
  return 0;
}

int32_t GraphBrpcService::stop_profiler(Table *table,
                                        const PsRequestMessage &request,
                                        PsResponseMessage &response,
                                        brpc::Controller *cntl) {
  platform::DisableProfiler(platform::EventSortingKey::kDefault,
                            string::Sprintf("server_%s_profile", _rank));
  return 0;
}

int32_t GraphBrpcService::start_profiler(Table *table,
                                         const PsRequestMessage &request,
                                         PsResponseMessage &response,
                                         brpc::Controller *cntl) {
  platform::EnableProfiler(platform::ProfilerState::kCPU);
  return 0;
}

int32_t GraphBrpcService::pull_graph_list(Table *table,
                                          const PsRequestMessage &request,
                                          PsResponseMessage &response,
                                          brpc::Controller *cntl) {
  CHECK_TABLE_EXIST(table, request, response)
  if (request.params_size() < 2) {
    set_response_code(response, -1,
                      "pull_graph_list request requires at least 2 arguments");
    return 0;
  }
  int start = *(int *)(request.params(0).c_str());
  int size = *(int *)(request.params(1).c_str());
  std::vector<float> res_data;
  char *buffer;
  int actual_size;
  table->pull_graph_list(start, size, buffer, actual_size);
  cntl->response_attachment().append(buffer, actual_size);
  return 0;
}
int32_t GraphBrpcService::graph_random_sample(Table *table,
                                              const PsRequestMessage &request,
                                              PsResponseMessage &response,
                                              brpc::Controller *cntl) {
  CHECK_TABLE_EXIST(table, request, response)
  if (request.params_size() < 2) {
    set_response_code(
        response, -1,
        "graph_random_sample request requires at least 2 arguments");
    return 0;
  }
  uint64_t node_id = *(uint64_t *)(request.params(0).c_str());
  int sample_size = *(uint64_t *)(request.params(1).c_str());
  char *buffer;
  int actual_size;
  table->random_sample(node_id, sample_size, buffer, actual_size);
  cntl->response_attachment().append(buffer, actual_size);
  return 0;
}

}  // namespace distributed
}  // namespace paddle
