#pragma once
#include "encoding.h"
#define RFB_JOIN_IMPL(a, b) a##b
#define RFB_JOIN(a, b) RFB_JOIN_IMPL(a, b)

// 内部宏，id 只用一次
#define RFB_REGISTER_ENCODING_IMPL(TYPE, CODE, ID, ...)                                \
   namespace {                                                                         \
   struct RFB_JOIN(_rfb_registrar_, ID) {                                              \
         RFB_JOIN(_rfb_registrar_, ID)() {                                             \
            libvnc::encoding::encoding_factory::get_mutable_instance().register_codec( \
               CODE, [] { return std::make_unique<TYPE>(__VA_ARGS__); });              \
         }                                                                             \
   };                                                                                  \
   RFB_JOIN(_rfb_registrar_, ID) RFB_JOIN(_rfb_registrar_inst_, ID);                   \
   }

// 外部宏，自动生成唯一 ID
#define RFB_REGISTER_ENCODING(TYPE, CODE, ...) RFB_REGISTER_ENCODING_IMPL(TYPE, CODE, __COUNTER__, __VA_ARGS__)