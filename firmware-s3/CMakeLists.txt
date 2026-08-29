cmake_minimum_required(VERSION 3.16)

# Trim the build to main and its dependencies.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stripe_revenue_display)
