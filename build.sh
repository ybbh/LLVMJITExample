clang++ -g llvm_jit_demo.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native` -O3 -o llvm_jit_demo
