# vnxvideo
vnxvideo is a shared library which contains the low-level C++ components of Viinex video management SDK. They can be of use on their own in some cases. Additionally, vnxvideo public headers are required to implement video sources plugins and/or video analytics modules for Viinex.

## Folders

Headers intended for public use reside in include/vnxvideo.

src folder contains the implementation of vnxvideo library.

vnxview contains the implementation of a simple utility for viewing a raw video stream published by a Viinex component via the "local transport" feature (that is, video renderer with property "shared": true).

vnxvideotest is just a playground to experiment with various components implemented in vnxvideo.

## Building

### Dependencies

In order to build vnxvideo library, the following dependencies are required:

boost >= 1.41 && <= 1.70

OpenH264 2.0.0 (https://www.openh264.org/)

Intel IPP 2019 (https://software.intel.com/en-us/intel-ipp)

FFmpeg 3.3 (https://ffmpeg.org/download.html)

On Windows, DirectShow examples base classes library are also necessary (available at https://github.com/Microsoft/Windows-classic-samples/tree/master/Samples/Win7Samples/multimedia/directshow/baseclasses or https://github.com/viinex/ambase). This project should be built in advance.

### Windows

Make sure the dependencies are installed and/or built. Run the vnxvideo.sln solution with Microsoft Visual Studio 2015. Adjust the path to dependencies in the "Dependencies.props" property sheet. For that, in Visual Studio select Property manager -> vnxvideo project -> double click "Dependencies" property sheet -> go to User Macros tab and edit the variables IPP_HOME, FFMPEG_HOME, OPENH264_HOME, AMBASE_HOME according to locations of respective software on your local system. Boost should be restored using NuGet.

After that you should be able to build Release and Debug configurations of vnxvideo project for x86 platform.

### Linux

Install boost-dev and ffmpeg-dev packages on your system. Build and install openh264 library and install IPP.
Set the variables IPP_HOME and OPENH264_HOME (FFMPEG_HOME may also be set a custom build in a non-system-wide location is preferred).

After that you should be able to build the libvnxvideo.so library using the GNU make command.
