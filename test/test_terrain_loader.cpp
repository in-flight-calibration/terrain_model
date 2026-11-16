#include <terrain_loader.hpp>

#include <ifc_tools/processing/terrain_grid.hpp>

#include <Eigen/Core>
#include <gdal.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
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
    return 100.0 + 0.01 * north - 0.02 * east;
}

void write(const std::filesystem::path& path)
{
    constexpr int size = 400;
    constexpr double pixel = 5.0;
    constexpr double west = -1000.0;
    constexpr double north = 1000.0;

    GDALAllRegister();
    GDALDatasetH dataset = GDALCreate(GDALGetDriverByName("GTiff"), path.c_str(), size, size, 1, GDT_Float32, nullptr);
    double transform[6] = {west, pixel, 0.0, north, 0.0, -pixel};
    GDALSetGeoTransform(dataset, transform);

    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRImportFromProj4(srs, "+proj=aeqd +lat_0=52 +lon_0=21 +R=6371000 +units=m +no_defs");
    GDALSetSpatialRef(dataset, srs);
    OSRDestroySpatialReference(srs);

    std::vector<float> data(static_cast<std::size_t>(size * size));

    for (int r = 0; r < size; ++r)
    {
        for (int c = 0; c < size; ++c)
        {
            data[static_cast<std::size_t>(r * size + c)] = static_cast<float>(height(north - (r + 0.5) * pixel, west + (c + 0.5) * pixel));
        }
    }

    const CPLErr error = GDALRasterIO(GDALGetRasterBand(dataset, 1), GF_Write, 0, 0, size, size, data.data(), size, size, GDT_Float32, 0, 0);
    check(error == CE_None, "synthetic DTM written");
    GDALClose(dataset);
}

bool consistent(const terrain_model::TerrainLoader& loader)
{
    const ifc_tools::TerrainGrid& grid = loader.getTerrainGrid();
    double max_error = 0.0;

    for (std::size_t i = 0; i < grid.x().size(); ++i)
    {
        for (std::size_t j = 0; j < grid.y().size(); ++j)
        {
            const double xi = grid.x().start() + static_cast<double>(i) * grid.x().step();
            const double yj = grid.y().start() + static_cast<double>(j) * grid.y().step();
            const double expected = grid.origin().ref_alt() - height(xi, yj);
            max_error = std::max(max_error, std::abs(grid.matrix()(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) - expected));
        }
    }

    return max_error < 1e-3;
}

bool centred(const terrain_model::TerrainLoader& loader, float x, float y)
{
    const ifc_tools::TerrainGrid& grid = loader.getTerrainGrid();
    return grid.x().start() == x - 200.0f && grid.x().stop() == x + 200.0f && grid.y().start() == y - 200.0f && grid.y().stop() == y + 200.0f;
}

bool throws(const std::filesystem::path& directory, int resolution, int half_size, int hysteresis)
{
    try
    {
        terrain_model::TerrainLoader loader({resolution, half_size, hysteresis, directory});
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }

    return false;
}

}

int main()
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "terrain_model_terrain_loader";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const ifc_tools::TerrainOrigin a(52.0, 21.0, 110.0f);
    const ifc_tools::TerrainOrigin b(52.0, 21.0, 120.0f);

    terrain_model::TerrainLoader empty({10, 200, 100, directory});
    check(!empty.update(a, 0.0, 0.0) && !empty.update(a, 0.0, 0.0), "no DTM gives nothing and keeps retrying");

    write(directory / "dtm.tif");
    terrain_model::TerrainLoader loader({10, 200, 100, directory});
    check(loader.update(a, 40.0, -60.0) && centred(loader, 0.0f, -100.0f), "first update loads the grid centred on the hysteresis lattice");
    check(consistent(loader), "first grid consistent");

    check(!loader.update(a, 99.0, -150.0), "inside hysteresis gives nothing");

    check(loader.update(a, 100.0, -60.0) && centred(loader, 100.0f, -100.0f), "x beyond hysteresis shifts");
    check(consistent(loader), "x shift consistent");

    check(loader.update(a, -95.0, 150.0) && centred(loader, -100.0f, 200.0f), "x and y beyond hysteresis shift");
    check(consistent(loader), "x and y shift consistent");

    check(loader.update(a, 700.0, -600.0) && centred(loader, 700.0f, -600.0f), "jump beyond the grid loads it whole");
    check(consistent(loader), "jump consistent");

    check(!loader.update(a, 700.0, -600.0), "same position gives nothing");
    loader.reset();
    check(loader.update(a, 700.0, -600.0) && centred(loader, 700.0f, -600.0f) && consistent(loader), "reset reloads");
    check(loader.update(b, 700.0, -600.0) && loader.getTerrainGrid().origin() == b && consistent(loader), "new origin reloads");

    check(throws(directory, 0, 200, 100), "zero resolution throws");
    check(throws(directory, 10, 205, 100), "half size not a multiple throws");
    check(throws(directory, 10, 200, 15), "hysteresis not a multiple throws");
    check(throws(directory, 10, 200, 10), "hysteresis of one cell throws");

    std::filesystem::remove_all(directory);
    return failures == 0 ? 0 : 1;
}
