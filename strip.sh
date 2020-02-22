#!/usr/bin/bash

# remove all building files other than cmake
find . -type f \( -name "*.m4" -o -name "*.ac" -o -name "*.in" -o -name "*.am" -o -name "*.sln" -o -name "*.vcxproj*" -o -name "*.props" -o -name "*.el" -o -name "*.yml" \) -delete
find . -type d -empty -delete

# change all encoding to utf-8 for easier python handling
find . -type f \( -name "*.cpp" -o -name "*.hpp" \) -exec isutf8 {} + | cut -d: -f1 | xargs -I@ iconv -f "ISO8859-1" -t "UTF-8" -o @ @ 


