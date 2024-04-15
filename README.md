# EFIS (Electronic Flight Instrument System) Project

This project is an Electronic Flight Instrument System (EFIS) developed in C++ with OpenGL and GLUT.

## Overview

The EFIS project provides a graphical interface for displaying aircraft attitude and other flight-related data. It includes components such as attitude widgets, data managers, and screens.

## Prerequisites

Before running the EFIS project, make sure you have the following dependencies installed on your system:

- OpenGL
- GLUT (OpenGL Utility Toolkit)
- CURL (for fetching data)
- Doxygen (for generating documentation)

# Additional Setup for wgrib2

If your project requires the wgrib2 library for handling GRIB files, follow these steps to prepare and build the library:
Prepare the Build Environment

`mkdir -p ~/wgrib2/build`
`cd ~/wgrib2/build`

## Run CMake

You'll need to specify the CMAKE_INSTALL_PREFIX to determine where the library should be installed. For system-wide installation, you might use /usr/local. The CMAKE_PREFIX_PATH should point to where your dependencies are installed if they are not in standard locations.

`cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_PREFIX_PATH=/path/to/dependencies`

## Compile the Library

`make`

## Install the Library

This step typically requires elevated privileges if installing to a system-wide directory like /usr/local.

`sudo make install`

## Building and Running EFIS

To build the EFIS project, follow these steps:

1. Clone this repository to your local machine.
2. Navigate to the project directory.
3. Create a build directory: `mkdir build && cd build`.
4. Generate the build files: `cmake ..`.
5. Build the project: `make`.

After building the project, you can run the EFIS executable. For example:
`./efis`

## Documentation

The project includes Doxygen comments for generating documentation. To generate the documentation, run:
`make doxygen`

The documentation will be generated in the `doc` directory.

## Contributing

Contributions to this project are welcome! If you find any issues or have suggestions for improvements, please submit a pull request or open an issue on GitHub.

## License

This project is licensed under the TBD.


