//============================================================================
// Name        : llvm_jit_demo.cpp
// Author      : ybbh
// Version     :
// Copyright   : Your copyright notice
// Description : LLVM JIT demo in C++, Ansi-style
//============================================================================

#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <vector>
#include <iostream>
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"

#define   BOOLEAN 0
#define   INT32 1
#define   INT64 2
#define   FLOAT 3
#define   DOUBLE 4

using namespace llvm;
using namespace std;
typedef orc::ObjectLinkingLayer<> ObjLayerT;
typedef orc::IRCompileLayer<ObjLayerT> CompileLayerT;

static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<legacy::FunctionPassManager> TheFPM;

struct TupleDesc {
  int num_slots_;
  int size_;
  int *offset_;
  int *data_type_;
};

TupleDesc *CreateDesc() {
  TupleDesc *desc = new TupleDesc;
  int size = 0;
  desc->num_slots_ = 5;
  desc->data_type_ = new int[desc->num_slots_];
  desc->offset_ = new int[desc->num_slots_];
  desc->data_type_[0] = INT32;
  desc->data_type_[1] = INT64;
  desc->data_type_[2] = BOOLEAN;
  desc->data_type_[3] = FLOAT;
  desc->data_type_[4] = DOUBLE;
  desc->offset_[0] = size;
  size += sizeof(int);
  desc->offset_[1] = size;
  size += sizeof(long);
  desc->offset_[2] = size;
  size += sizeof(char);
  desc->offset_[3] = size;
  size += sizeof(float);
  desc->offset_[4] = size;
  size += sizeof(double);
  desc->size_ = size;
  return desc;
}

Function *CodeGen(TupleDesc *desc) {
  std::vector<Type*> args(1, Type::getInt8PtrTy(TheContext));
  FunctionType *func_type = FunctionType::get(Type::getVoidTy(TheContext), args,
                                       false);
  std::string func_name = "MaterializeTupleJIT";
  Function *fucntion = Function::Create(func_type, Function::ExternalLinkage, func_name,
                                 TheModule.get());
  BasicBlock *basic_block = BasicBlock::Create(TheContext, "entry", fucntion);
  Builder.SetInsertPoint(basic_block);

  Value *arg = &(*(fucntion->arg_begin()));
  Value *tuple_ptr = Builder.CreatePtrToInt(arg, Type::getInt64Ty(TheContext));
  for (int i = 0; i < desc->num_slots_; ++i) {
    int offset = desc->offset_[i];
    Value *left;
    Value *right;

    left = tuple_ptr;
    right = ConstantInt::get(Type::getInt64Ty(TheContext), offset);
    Value *slot_ptr = Builder.CreateAdd(left, right);

    switch (desc->data_type_[i]) {
      case BOOLEAN: {
        left = ConstantInt::get(Type::getInt8Ty(TheContext), 0);
        right = Builder.CreateIntToPtr(slot_ptr, Type::getInt8PtrTy(TheContext));
      }
        break;
      case INT32: {
        left = ConstantInt::get(Type::getInt32Ty(TheContext), 1);
        right = Builder.CreateIntToPtr(slot_ptr, Type::getInt32PtrTy(TheContext));
      }
        break;
      case INT64: {
        left = ConstantInt::get(Type::getInt64Ty(TheContext), 2);
        right = Builder.CreateIntToPtr(slot_ptr, Type::getInt64PtrTy(TheContext));
      }
        break;
      case FLOAT: {
        left = ConstantFP::get(Type::getFloatTy(TheContext), 3.5);
        right = Builder.CreateIntToPtr(slot_ptr, Type::getFloatPtrTy(TheContext));
      }
        break;
      case DOUBLE:
        left = ConstantFP::get(Type::getDoubleTy(TheContext), 4.5);
        right = Builder.CreateIntToPtr(slot_ptr, Type::getDoublePtrTy(TheContext));
        break;
    }
    Builder.CreateStore(left, right);
  }
  Builder.CreateRetVoid();
  verifyFunction(*fucntion, &llvm::errs());
  TheFPM->run(*fucntion);

  return fucntion;
}

void MaterializeTuple(char* tuple, TupleDesc *desc) {
  for (int i = 0; i < desc->num_slots_; ++i) {
    {
      char *slot = tuple + desc->offset_[i];
      switch (desc->data_type_[i]) {
        case BOOLEAN:
          *(char*) slot = 0;
          break;
        case INT32:
          *(int*) slot = 1;
          break;
        case INT64:
          *(long*) slot = 2;
          break;
        case FLOAT:
          *(float*) slot = 3.5;
          break;
        case DOUBLE:
          *(double*) slot = 4.5;
          break;
      }
    }
  }
}

static void InitializeModuleAndPassManager() {
  // Open a new module.
  TheModule = llvm::make_unique<Module>("my cool jit", TheContext);
  TheModule->setDataLayout(EngineBuilder().selectTarget()->createDataLayout());

  // Create a new pass manager attached to it.
  TheFPM = llvm::make_unique<legacy::FunctionPassManager>(TheModule.get());

  // Do simple "peephole" optimizations and bit-twiddling optzns.
  TheFPM->add(createInstructionCombiningPass());
  // Reassociate expressions.
  TheFPM->add(createReassociatePass());
  // Eliminate Common SubExpressions.
  TheFPM->add(createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  TheFPM->add(createCFGSimplificationPass());

  TheFPM->doInitialization();
}

int main() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Initilaze native target
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  InitializeModuleAndPassManager();




  // Prepare jit layer
  ObjLayerT ObjectLayer;
  std::unique_ptr<TargetMachine> TM(EngineBuilder().selectTarget());
  DataLayout DL(TM->createDataLayout());
  CompileLayerT CompileLayer(ObjectLayer, orc::SimpleCompiler(*TM));
  auto Resolver = orc::createLambdaResolver(
      [&](const std::string &Name) {
        if (auto Sym = CompileLayer.findSymbol(Name, false))
        return Sym;
        return JITSymbol(nullptr);
      },
      [](const std::string &S) {return nullptr;});

  TupleDesc *desc = CreateDesc();
  CodeGen(desc);

  // Dump the llvm IR
  TheModule->dump();

  // Add MyModule to the jit layer
  std::vector<std::unique_ptr<Module>> Modules;
  Modules.push_back(std::move(TheModule));
  CompileLayer.addModuleSet(std::move(Modules),
                            make_unique<SectionMemoryManager>(),
                            std::move(Resolver));


  char  *tuple1 = new char[desc->size_];
  MaterializeTuple(tuple1, desc);

  std::string MangledName;
  raw_string_ostream MangledNameStream(MangledName);
  Mangler::getNameWithPrefix(MangledNameStream, "MaterializeTupleJIT", DL);
  auto Sym = CompileLayer.findSymbol(MangledNameStream.str(), true);


  // Cast to function
  auto func = (void (*)(char *))Sym.getAddress();
  char *tuple2 = new char[desc->size_];
  func(tuple2);

  return 0;
}

