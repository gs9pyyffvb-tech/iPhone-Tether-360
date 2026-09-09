#pragma once
#include <stddef.h>
#include <stdint.h>
namespace sha256 { void Hash(const uint8_t* data,size_t len,uint8_t out[32]); }
