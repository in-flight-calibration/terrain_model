#include <dtm_loader.hpp>

#include <ifc_tools/processing/terrain_grid.hpp>

#include <gdal.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    failures += condition ? 0 : 1;
}

double height(double north, double east)
{
    return 100.0 + 0.01 * north + 0.02 * east;
}

void write(const std::filesystem::path& path)
{
    constexpr int cols = 600;
    constexpr int rows = 400;
    constexpr double west = -300.0;
    constexpr double north = 200.0;

    GDALAllRegister();
    GDALDatasetH dataset = GDALCreate(GDALGetDriverByName("GTiff"), path.c_str(), cols, rows, 1, GDT_Float32, nullptr);
    double transform[6] = {west, 1.0, 0.0, north, 0.0, -1.0};
    GDALSetGeoTransform(dataset, transform);

    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRImportFromProj4(srs, "+proj=aeqd +lat_0=52 +lon_0=21 +R=6371000 +units=m +no_defs");
    GDALSetSpatialRef(dataset, srs);
    OSRDestroySpatialReference(srs);

    std::vector<float> data(static_cast<std::size_t>(cols * rows));

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            data[static_cast<std::size_t>(r * cols + c)] = static_cast<float>(height(north - r - 0.5, west + c + 0.5));
        }
    }

    const CPLErr error = GDALRasterIO(GDALGetRasterBand(dataset, 1), GF_Write, 0, 0, cols, rows, data.data(), cols, rows, GDT_Float32, 0, 0);
    check(error == CE_None, "synthetic DTM written");
    GDALClose(dataset);
}

}

int main()
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "terrain_model_dtm_loader";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

        const ifc_tools::TerrainOrigin origin(52.0, 21.0, 110.0f);
    const std::vector<float> empty = terrain_model::DtmLoader(directory).getTerrain(origin, ifc_tools::TerrainAxis(0.0f, 10.0f, 20.0f), ifc_tools::TerrainAxis(0.0f, 10.0f, 30.0f));
    check(empty.empty(), "empty directory gives no terrain");

    write(directory / "dtm.tif");
    const terrain_model::DtmLoader loader(directory);

    const std::size_t rows = 31;
    const std::size_t cols = 51;
    const std::vector<float> z = loader.getTerrain(origin, ifc_tools::TerrainAxis(-150.0f, 10.0f, 150.0f), ifc_tools::TerrainAxis(-250.0f, 10.0f, 250.0f));
    double max_error = 0.0;

    for (std::size_t i = 0; i < rows; ++i)
    {
        for (std::size_t j = 0; j < cols; ++j)
        {
            const double expected = 110.0 - height(-150.0 + 10.0 * static_cast<double>(i), -250.0 + 10.0 * static_cast<double>(j));
            max_error = std::max(max_error, std::abs(static_cast<double>(z[i * cols + j]) - expected));
        }
    }

    std::printf("max error %g m\n", max_error);
    check(z.size() == rows * cols && max_error < 1e-3, "rows north, columns east, NED down relative to the origin");

    const std::vector<float> outside = loader.getTerrain(origin, ifc_tools::TerrainAxis(190.0f, 10.0f, 230.0f), ifc_tools::TerrainAxis(0.0f, 10.0f, 10.0f));
    check(std::isfinite(outside[0]) && std::isnan(outside[4 * 2]), "NaN outside the DTM");

    std::filesystem::remove_all(directory);
    return failures == 0 ? 0 : 1;
}
