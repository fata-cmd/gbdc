#!/bin/bash
set -e -u -x

if [ "$(uname)" == "Linux" ]; then
    cd /project
fi

# if [ "$(uname)" == "Darwin" ]; then
#     brew install wget
# fi

# rm -rf gbd_test libarchive

git clone https://github.com/libarchive/libarchive.git
cd libarchive
cmake -DCMAKE_BUILD_TYPE=Release .
make -j4
if [ "$(uname)" == "Darwin" ]; then
    macos_version=$(sw_vers -productVersion)

    # Extract the major version number
    major_version=$(echo "$macos_version" | cut -d. -f1)

    # Check if the major version is 13 or 14
    if [ "$major_version" == "14"]; then
        export LDFLAGS="-L/opt/homebrew/opt/libarchive/lib"
        export CPPFLAGS="-I/opt/homebrew/opt/libarchive/include -std=c++11"
        ls /opt/homebrew/opt/libarchive/lib
        ls /opt/homebrew/opt/libarchive/include
    else
        export LDFLAGS="-L/usr/local/opt/libarchive/lib" 
        export CPPFLAGS="-I/usr/local/opt/libarchive/include -std=c++11"
        ls /usr/local/opt/libarchive/lib
        ls /usr/local/opt/libarchive/include
    fi
    sudo make install
else
    make install
fi

cd ..


mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4
cd ..


# mkdir gbd_test
# cd gbd_test

# mkdir benchmarks

# wget --content-disposition -i scripts/allinstances.uri -P benchmarks

# export GBD_DB=my.db
# touch my.db
# gbd init -j4 local benchmarks
# gbd init -j4 base
# gbd get -r ccs

# cd ..
# rm -rf gbd_test libarchive
