mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cd ..
rm -rf build/lib.linux-x86_64-3.8/ build/temp.linux-x86_64-3.8/
