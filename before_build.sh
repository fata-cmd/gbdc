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
    sudo make install
    export LDFLAGS="-L/usr/local/opt/lib" 
    export CPPFLAGS="-I/usr/local/opt/include -std=c++11"
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
