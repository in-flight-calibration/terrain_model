#pragma once

#include <rclcpp/rclcpp.hpp>

namespace terrain_model
{

class TerrainModel : public rclcpp::Node
{
public:
    TerrainModel() : rclcpp::Node("terrain_model")
    {
    }
};

}
