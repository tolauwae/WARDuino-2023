//
// WARDuino - WebAssembly interpreter for embedded devices.
//
//
#include <cstdio>
#include "../WARDuino/WARDuino.h"

WARDuino wac;

unsigned char wasm[] = {
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x16, 0x05, 0x60,
  0x02, 0x7f, 0x7f, 0x00, 0x60, 0x02, 0x7f, 0x7f, 0x00, 0x60, 0x01, 0x7f,
  0x00, 0x60, 0x00, 0x00, 0x60, 0x01, 0x7f, 0x00, 0x02, 0x3f, 0x03, 0x03,
  0x65, 0x6e, 0x76, 0x0d, 0x63, 0x68, 0x69, 0x70, 0x5f, 0x70, 0x69, 0x6e,
  0x5f, 0x6d, 0x6f, 0x64, 0x65, 0x00, 0x00, 0x03, 0x65, 0x6e, 0x76, 0x12,
  0x63, 0x68, 0x69, 0x70, 0x5f, 0x64, 0x69, 0x67, 0x69, 0x74, 0x61, 0x6c,
  0x5f, 0x77, 0x72, 0x69, 0x74, 0x65, 0x00, 0x01, 0x03, 0x65, 0x6e, 0x76,
  0x0a, 0x63, 0x68, 0x69, 0x70, 0x5f, 0x64, 0x65, 0x6c, 0x61, 0x79, 0x00,
  0x02, 0x03, 0x03, 0x02, 0x03, 0x03, 0x07, 0x08, 0x01, 0x04, 0x6d, 0x61,
  0x69, 0x6e, 0x00, 0x04, 0x0a, 0x27, 0x02, 0x07, 0x00, 0x41, 0xe8, 0x07,
  0x10, 0x02, 0x0b, 0x1d, 0x00, 0x41, 0x02, 0x41, 0x02, 0x10, 0x00, 0x03,
  0x40, 0x41, 0x10, 0x41, 0x01, 0x10, 0x01, 0x10, 0x03, 0x41, 0x10, 0x41,
  0x00, 0x10, 0x01, 0x10, 0x03, 0x0c, 0x00, 0x0b, 0x0b
};
unsigned int wasm_len = 153;

extern "C" {
				extern void app_main(void);
}

void app_main(void) {
    Module* m = wac.load_module(wasm, wasm_len, {});
    printf("START\n\n");
    wac.run_module(m);
	   printf("END\n\n");
    wac.unload_module(m);
}