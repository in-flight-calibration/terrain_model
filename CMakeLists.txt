cmake_minimum_required(VERSION 3.20)
project(terrain_model)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(GDAL CONFIG REQUIRED)
find_package(ifc_msgs REQUIRED)
find_package(ifc_tools REQUIRED)
find_package(rclcpp REQUIRED)

add_executable(${PROJECT_NAME} src/main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Eigen3::Eigen
    GDAL::GDAL
    ifc_msgs::ifc_msgs__rosidl_typesupport_cpp
    ifc_tools::ifc_tools
    rclcpp::rclcpp
)

if(BUILD_TESTING)
    enable_testing()
    add_executable(terrain_model_loader test/test_terrain_loader.cpp)
    target_include_directories(terrain_model_loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(terrain_model_loader PRIVATE Eigen3::Eigen GDAL::GDAL ifc_tools::ifc_tools)
    add_test(NAME terrain_model_loader COMMAND terrain_model_loader)
    add_executable(terrain_model_dtm_loader test/test_dtm_loader.cpp)
    target_include_directories(terrain_model_dtm_loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(terrain_model_dtm_loader PRIVATE Eigen3::Eigen GDAL::GDAL ifc_tools::ifc_tools)
    add_test(NAME terrain_model_dtm_loader COMMAND terrain_model_dtm_loader)
endif()

install(TARGETS ${PROJECT_NAME}
    DESTINATION lib/${PROJECT_NAME}
)

ament_export_dependencies(Eigen3 GDAL ifc_msgs ifc_tools rclcpp)

ament_package()
