clang++ -g llvm_jit_demo.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native` -o llvm_jit_demo
