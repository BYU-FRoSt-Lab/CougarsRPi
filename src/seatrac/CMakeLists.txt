cmake_minimum_required(VERSION 3.8)
project(seatrac)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(seatrac_interfaces REQUIRED)

#find_package(seatrac_driver REQUIRED)
if(NOT TARGET seatrac_driver)
    #find_package(seatrac_driver QUIET)
    if(NOT TARGET seatrac_driver)
        include(FetchContent)
        FetchContent_Declare(seatrac_driver
            GIT_REPOSITORY https://Clayton314@bitbucket.org/frostlab/seatrac_driver.git
            GIT_TAG        main
        )
        FetchContent_MakeAvailable(seatrac_driver)
    endif()
endif()

add_executable(modem src/modem_ros_node.cpp)
ament_target_dependencies(modem rclcpp std_msgs seatrac_interfaces)
target_link_libraries(modem
seatrac_driver
)
add_executable(modem_pinger src/modem_pinger.cpp)
ament_target_dependencies(modem_pinger rclcpp std_msgs seatrac_interfaces)
target_link_libraries(modem_pinger seatrac_driver)

add_executable(calibrate_modem src/calibrate_modem.cpp)
target_link_libraries(calibrate_modem
seatrac_driver
)
add_executable(setup_seatrac_beacon src/seatrac_beacon_setup_tool.cpp)
target_link_libraries(setup_seatrac_beacon seatrac_driver)
install(TARGETS
  modem
  calibrate_modem
  setup_seatrac_beacon
  modem_pinger
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()