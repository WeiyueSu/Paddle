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
#include <vector>
#include "paddle/fluid/distributed/table/weighted_sampler.h"
namespace paddle {
namespace distributed {
// enum GraphNodeType { user = 0, item = 1, query = 2, unknown = 3 };
class GraphEdge : public WeightedObject {
 public:
  double weight;
  uint64_t id;
  // GraphNodeType type;
  GraphEdge() {}
  GraphEdge(uint64_t id, double weight) : weight(weight), id(id) {}
};
class GraphNode {
 public:
  GraphNode() { sampler = NULL; }
  // GraphNode(uint64_t id, GraphNodeType type, std::string feature)
  //     : id(id), type(type), feature(feature), sampler(NULL) {}
  GraphNode(uint64_t id, std::string feature)
      : id(id), feature(feature), sampler(NULL) {}
  virtual ~GraphNode() {}
  std::vector<GraphEdge *> get_graph_edge() { return edges; }
  static int enum_size, id_size, int_size, double_size;
  uint64_t get_id() { return id; }
  void set_id(uint64_t id) { this->id = id; }
  // GraphNodeType get_graph_node_type() { return type; }
  // void set_graph_node_type(GraphNodeType type) { this->type = type; }
  void set_feature(std::string feature) { this->feature = feature; }
  std::string get_feature() { return feature; }
  virtual int get_size();
  virtual void build_sampler();
  virtual void to_buffer(char *buffer);
  virtual void recover_from_buffer(char *buffer);
  virtual void add_edge(GraphEdge *edge) { edges.push_back(edge); }
  std::vector<GraphEdge *> sample_k(int k) {
    std::vector<GraphEdge *> v;
    if (sampler != NULL) {
      auto res = sampler->sample_k(k);
      for (auto x : res) {
        v.push_back((GraphEdge *)x);
      }
    }
    return v;
  }

 protected:
  uint64_t id;
  std::string feature;
  WeightedSampler *sampler;
  std::vector<GraphEdge *> edges;
};
}
}
