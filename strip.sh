#!/usr/bin/bash

# remove all building files other than cmake
find . -type f \( -name "*.m4" -o -name "*.ac" -o -name "*.in" -o -name "*.am" -o -name "*.sln" -o -name "*.vcxproj*" -o -name "*.props" \) -delete
find . -type d -empty -delete


