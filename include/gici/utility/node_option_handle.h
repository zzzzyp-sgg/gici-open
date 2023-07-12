/**
* @Function: Handles the configurations of nodes
*
* @Author  : Cheng Chi
* @Email   : chichengcn@sjtu.edu.cn
*
* Copyright (C) 2023 by Cheng Chi, All rights reserved.
**/
#pragma once

#include <memory>
#include <yaml-cpp/yaml.h>

namespace gici {

// Mainly used for organizing the relationship between nodes.
class NodeOptionHandle {
public:
  NodeOptionHandle(const YAML::Node& yaml_node);
  ~NodeOptionHandle() { }

  // Type of nodes
  enum class NodeType {
    Streamer, Formator, Estimator
  };

  // Node basic properties
  class NodeBase {
  public:
    NodeBase(const YAML::Node& yaml_node);
    ~NodeBase() { }

    inline std::vector<std::string> tags() { 
      std::vector<std::string> tags;
      for (auto tag : input_tags) tags.push_back(tag);
      for (auto tag : output_tags) tags.push_back(tag);
      return tags;
    }

    NodeType node_type;
    std::string tag;
    std::string type;
    std::vector<std::string> input_tags;
    std::vector<std::string> output_tags; 
    YAML::Node this_node;
    bool valid;
  };
  using NodeBasePtr = std::shared_ptr<NodeBase>;

  // Streamer node basic properties
  class StreamerNodeBase : public NodeBase {
  public:
    StreamerNodeBase(const YAML::Node& yaml_node);
    ~StreamerNodeBase() { }
  };
  using StreamerNodeBasePtr = std::shared_ptr<StreamerNodeBase>;

  // Formator node basic properties
  class FormatorNodeBase : public NodeBase {
  public:
    FormatorNodeBase(const YAML::Node& yaml_node);
    ~FormatorNodeBase() { }

    std::string io;
  };
  using FormatorNodeBasePtr = std::shared_ptr<FormatorNodeBase>;

  // Estimator node basic properties
  class EstimatorNodeBase : public NodeBase {
  public:
    EstimatorNodeBase(const YAML::Node& yaml_node);
    ~EstimatorNodeBase() { }

    std::vector<std::vector<std::string>> input_tag_roles;
  };
  using EstimatorNodeBasePtr = std::shared_ptr<EstimatorNodeBase>;

  // Whether the configurations are valid
  bool valid;                                     // 配置文件是否有效
  
  // Nodes
  YAML::Node replay_options;                      // replay节点
  std::vector<NodeBasePtr> nodes;
  std::vector<StreamerNodeBasePtr> streamers;     // streamers节点
  std::vector<FormatorNodeBasePtr> formators;     // 对应的formators
  std::vector<EstimatorNodeBasePtr> estimators;   // estimators节点
  std::map<std::string, NodeBasePtr> tag_to_node; // 把tag和nide对应起来的变量

private:
  // Check if the options of all nodes valid
  bool checkAllNodeOptions();

  // Find if a tag exists in list
  bool tagExists(const std::vector<std::string>& list, const std::string& tag);
};

using NodeOptionHandlePtr = std::shared_ptr<NodeOptionHandle>;

} // namespace gici