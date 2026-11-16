#pragma once

#include <ifc_tools/processing/terrain_grid.hpp>

#include <cpl_error.h>
#include <cpl_string.h>
#include <gdal.h>
#include <gdal_utils.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace terrain_model
{

class DtmLoader
{
public:
    explicit DtmLoader(const std::filesystem::path& directory) : _vrt(nullptr, GDALClose)
    {
        GDALAllRegister();

        for (const char* argument : {"-of", "MEM", "-ot", "Float32", "-r", "average", "-dstnodata", "nan"})
        {
            _arguments.AddString(argument);
        }

        std::vector<std::string> paths;
        std::error_code error;

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
        {
            const std::string extension = entry.path().extension().string();

            if (entry.is_regular_file() && (extension == ".tif" || extension == ".tiff"))
            {
                paths.push_back(entry.path().string());
            }
        }

        if (paths.empty())
        {
            return;
        }

        std::sort(paths.begin(), paths.end());
        CPLStringList files;

        for (const std::string& path : paths)
        {
            files.AddString(path.c_str());
        }

        int usage_error = 0;
        const std::unique_ptr<GDALBuildVRTOptions, decltype(&GDALBuildVRTOptionsFree)> vrt_options(GDALBuildVRTOptionsNew(nullptr, nullptr), GDALBuildVRTOptionsFree);
        _vrt.reset(GDALBuildVRT("", files.size(), nullptr, files.List(), vrt_options.get(), &usage_error));

        if (!_vrt)
        {
            throw std::runtime_error(std::format("DtmLoader: building the VRT failed: {}", CPLGetLastErrorMsg()));
        }
    }

    std::vector<float> getTerrain(const ifc_tools::TerrainOrigin& origin, const ifc_tools::TerrainAxis& x, const ifc_tools::TerrainAxis& y) const
    {
        const std::size_t rows = x.size();
        const std::size_t cols = y.size();

        if (!_vrt)
        {
            return {};
        }

        const double north_min = static_cast<double>(x.start()) - x.step() / 2.0;
        const double east_min = static_cast<double>(y.start()) - y.step() / 2.0;

        CPLStringList arguments(_arguments);
        arguments.AddString("-t_srs");
        arguments.AddString(std::format("+proj=aeqd +lat_0={:.12f} +lon_0={:.12f} +R=6371000 +units=m +no_defs", origin.ref_lat(), origin.ref_lon()).c_str());
        arguments.AddString("-te");
        arguments.AddString(std::format("{}", east_min).c_str());
        arguments.AddString(std::format("{}", north_min).c_str());
        arguments.AddString(std::format("{}", east_min + static_cast<double>(cols) * y.step()).c_str());
        arguments.AddString(std::format("{}", north_min + static_cast<double>(rows) * x.step()).c_str());
        arguments.AddString("-ts");
        arguments.AddString(std::to_string(cols).c_str());
        arguments.AddString(std::to_string(rows).c_str());

        int usage_error = 0;
        const std::unique_ptr<GDALWarpAppOptions, decltype(&GDALWarpAppOptionsFree)> warp_options(GDALWarpAppOptionsNew(arguments.List(), nullptr), GDALWarpAppOptionsFree);
        GDALDatasetH source = _vrt.get();
        const std::unique_ptr<void, decltype(&GDALClose)> warped(GDALWarp("", nullptr, 1, &source, warp_options.get(), &usage_error), GDALClose);

        if (!warped)
        {
            return {};
        }

        std::vector<float> z(rows * cols);
        float* const last_row = z.data() + (rows - 1) * cols;
        const int line_space = -static_cast<int>(cols * sizeof(float));

        if (GDALRasterIO(GDALGetRasterBand(warped.get(), 1), GF_Read, 0, 0, static_cast<int>(cols), static_cast<int>(rows), last_row, static_cast<int>(cols), static_cast<int>(rows), GDT_Float32, 0, line_space) != CE_None)
        {
            return {};
        }

        std::transform(z.begin(), z.end(), z.begin(), [&origin](float h) { return origin.ref_alt() - h; });
        return z;
    }

private:
    std::unique_ptr<void, decltype(&GDALClose)> _vrt;
    CPLStringList _arguments;
};

}
