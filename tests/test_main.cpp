#include "test_framework.h"

int main(int argc, char** argv) {
    return bgtest::run_all(argc > 1 ? argv[1] : "");
}
