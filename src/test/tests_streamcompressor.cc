#include <stdio.h>
#include <filesystem>
#include <iostream>
#include <fstream>


// #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// #include "doctest.h"

#include "src/util/StreamBuffer.h"
#include "src/util/StreamCompressor.h"


int main() {
    auto file = std::filesystem::absolute("hello.cnf.xz");
    StreamCompressor c(file.c_str(), 25);
    c.write("\n1 2 0\n1 0\n2 3 0",17);
    c.close();
    StreamBuffer b(file.c_str());
    Cl clause;
    while(b.readClause(clause)){
        for(Lit l : clause){
            std::cerr << l << " ";
        }
        std::cerr << "\n";
    }
    return 0;
}