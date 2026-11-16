#pragma once

#include <dtm_loader.hpp>

#include <ifc_tools/processing/terrain_grid.hpp>

#include <Eigen/Core>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terrain_model
{

struct TerrainLoaderLoaderParameters
{
    int resolution_m = 10;
    int half_size_m = 5000;
    int hysteresis_m = 100;
    std::string dtm_directory = "src/terrain_model/dtm";
};

class TerrainLoader
{
public:
    explicit TerrainLoader(const TerrainLoaderLoaderParameters& parameters)
        : _parameters(parameters), _dtm(parameters.dtm_directory)
    {
        if (_parameters.resolution_m <= 0 || _parameters.half_size_m <= 0 || _parameters.hysteresis_m < 2 * _parameters.resolution_m
            || _parameters.half_size_m % _parameters.resolution_m != 0 || _parameters.hysteresis_m % _parameters.resolution_m != 0)
        {
            throw std::invalid_argument("TerrainLoader: resolution and half size must be positive, hysteresis at least two cells and the sizes multiples of the resolution");
        }
    }

    void reset()
    {
        _grid.reset();
    }

    bool update(const ifc_tools::TerrainOrigin& origin, double x, double y)
    {
        if (_grid && !(_grid->origin() == origin))
        {
            reset();
        }

        const double px = x / _parameters.resolution_m;
        const double py = y / _parameters.resolution_m;
        const double hysteresis = _parameters.hysteresis_m / _parameters.resolution_m;

        if (_grid && std::abs(px - static_cast<double>(centre(_grid->x()))) < hysteresis
            && std::abs(py - static_cast<double>(centre(_grid->y()))) < hysteresis)
        {
            return false;
        }

        const int64_t cx = round(px);
        const int64_t cy = round(py);
        const int64_t size = 2 * half() + 1;
        const std::optional<Eigen::MatrixXd> z = _grid ? shift(cx, cy) : load(origin, cx, cy, 0, size, 0, size);

        if (!z)
        {
            return false;
        }

        _grid.emplace(origin, axis(cx - half(), size), axis(cy - half(), size), std::move(*z));
        return true;
    }

    const ifc_tools::TerrainGrid& getTerrainGrid() const
    {
        return _grid.value();
    }

private:
    const TerrainLoaderLoaderParameters _parameters;
    DtmLoader _dtm;
    std::optional<ifc_tools::TerrainGrid> _grid;

    std::optional<Eigen::MatrixXd> shift(int64_t cx, int64_t cy) const
    {
        const int64_t size = 2 * half() + 1;
        const int64_t dx = cx - centre(_grid->x());
        const int64_t dy = cy - centre(_grid->y());
        const int64_t rows = size - std::abs(dx);
        const int64_t cols = size - std::abs(dy);

        if (rows < 2 || cols < 2)
        {
            return load(_grid->origin(), cx, cy, 0, size, 0, size);
        }

        Eigen::MatrixXd z(size, size);
        z.block(std::max<int64_t>(-dx, 0), std::max<int64_t>(-dy, 0), rows, cols) = _grid->matrix().block(std::max<int64_t>(dx, 0), std::max<int64_t>(dy, 0), rows, cols);

        if (dx != 0)
        {
            const int64_t row = dx > 0 ? rows : 0;
            const std::optional<Eigen::MatrixXd> strip = load(_grid->origin(), cx, cy, row, std::abs(dx), 0, size);

            if (!strip)
            {
                return std::nullopt;
            }

            z.block(row, 0, std::abs(dx), size) = *strip;
        }

        if (dy != 0)
        {
            const int64_t row = std::max<int64_t>(-dx, 0);
            const int64_t col = dy > 0 ? cols : 0;
            const std::optional<Eigen::MatrixXd> strip = load(_grid->origin(), cx, cy, row, rows, col, std::abs(dy));

            if (!strip)
            {
                return std::nullopt;
            }

            z.block(row, col, rows, std::abs(dy)) = *strip;
        }

        return z;
    }

    std::optional<Eigen::MatrixXd> load(const ifc_tools::TerrainOrigin& origin, int64_t cx, int64_t cy, int64_t row, int64_t rows, int64_t col, int64_t cols) const
    {
        const ifc_tools::TerrainAxis x = axis(cx - half() + row, rows);
        const ifc_tools::TerrainAxis y = axis(cy - half() + col, cols);
        const std::vector<float> z = _dtm.getTerrain(origin, x, y);

        if (z.size() != x.size() * y.size())
        {
            return std::nullopt;
        }

        return ifc_tools::TerrainGrid(origin, x, y, z).matrix();
    }

    int64_t half() const
    {
        return _parameters.half_size_m / _parameters.resolution_m;
    }

    int64_t centre(const ifc_tools::TerrainAxis& axis) const
    {
        return std::lround(axis.start() / static_cast<float>(_parameters.resolution_m)) + half();
    }

    int64_t round(double position) const
    {
        const int64_t hysteresis = _parameters.hysteresis_m / _parameters.resolution_m;
        return hysteresis * std::llround(position / static_cast<double>(hysteresis));
    }

    ifc_tools::TerrainAxis axis(int64_t first, int64_t count) const
    {
        return ifc_tools::TerrainAxis(
            static_cast<float>(first * _parameters.resolution_m),
            static_cast<float>(_parameters.resolution_m),
            static_cast<float>((first + count - 1) * _parameters.resolution_m));
    }
};

}
