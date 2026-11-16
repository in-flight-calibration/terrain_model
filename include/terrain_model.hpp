#pragma once

#include <terrain_loader.hpp>

#include <ifc_tools/processing/terrain_grid.hpp>

#include <ifc_msgs/msg/stream_telemetry.hpp>
#include <ifc_msgs/msg/terrain.hpp>
#include <rclcpp/rclcpp.hpp>

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terrain_model
{

class TerrainModel : public rclcpp::Node
{
public:
    TerrainModel()
        : rclcpp::Node("terrain_model"),
          _terrain_loader(
              TerrainLoaderLoaderParameters {
                  static_cast<int>(declare_parameter<int>("terrain_model.resolution_m", 10)),
                  static_cast<int>(declare_parameter<int>("terrain_model.half_size_m", 5000)),
                  static_cast<int>(declare_parameter<int>("terrain_model.hysteresis_m", 100)),
                  declare_parameter<std::string>("terrain_model.dtm_directory", "src/terrain_model/dtm")})
    {
        _debug = declare_parameter<bool>("terrain_model.debug", false);

        _publisher = create_publisher<ifc_msgs::msg::Terrain>(
            declare_parameter<std::string>("terrain_model.topic", "/terrain_model/terrain"),
            rclcpp::QoS(1).transient_local().reliable());

        _subscription = create_subscription<ifc_msgs::msg::StreamTelemetry>(
            declare_parameter<std::string>("stream_telemetry.topic", "/stream_telemetry/stream_telemetry"),
            rclcpp::QoS(1),
            [this](const ifc_msgs::msg::StreamTelemetry& message) { update(message); });
    }

private:
    TerrainLoader _terrain_loader;

    bool _debug = false;

    rclcpp::Publisher<ifc_msgs::msg::Terrain>::SharedPtr _publisher;
    rclcpp::Subscription<ifc_msgs::msg::StreamTelemetry>::SharedPtr _subscription;

    void update(const ifc_msgs::msg::StreamTelemetry& message)
    {
        const ifc_msgs::msg::AutopilotTelemetry& telemetry = message.autopilot_telemetry;

        if (message.stream_info.reset)
        {
            _terrain_loader.reset();
            return;
        }

        if ((telemetry.image_timestamp.sec == 0 && telemetry.image_timestamp.nanosec == 0)
            || !std::isfinite(telemetry.position[0]) || !std::isfinite(telemetry.position[1]) || !std::isfinite(telemetry.position[2])
            || !std::isfinite(telemetry.ref_lat) || !std::isfinite(telemetry.ref_lon) || !std::isfinite(telemetry.ref_alt))
        {
            return;
        }

        try
        {
            const auto start = std::chrono::steady_clock::now();
            const ifc_tools::TerrainOrigin origin(telemetry.ref_lat, telemetry.ref_lon, telemetry.ref_alt);

            if (!_terrain_loader.update(origin, telemetry.position[0], telemetry.position[1]))
            {
                return;
            }

            const ifc_tools::TerrainGrid& grid = _terrain_loader.getTerrainGrid();
            publish(grid);

            if (_debug)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
                RCLCPP_INFO(get_logger(), "terrain x %.0f:%.0f:%.0f y %.0f:%.0f:%.0f published in %lld ms", grid.x().start(), grid.x().step(), grid.x().stop(), grid.y().start(), grid.y().step(), grid.y().stop(), static_cast<long long>(elapsed.count()));
            }
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
    }

    void publish(const ifc_tools::TerrainGrid& grid)
    {
        ifc_msgs::msg::Terrain message;
        message.header.stamp = now();
        message.ref_lat = grid.origin().ref_lat();
        message.ref_lon = grid.origin().ref_lon();
        message.ref_alt = grid.origin().ref_alt();
        message.x = {grid.x().start(), grid.x().step(), grid.x().stop()};
        message.y = {grid.y().start(), grid.y().step(), grid.y().stop()};
        message.z = grid.toVector();
        _publisher->publish(message);
    }
};

}
