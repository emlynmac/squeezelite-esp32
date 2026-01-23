#ifndef TAS5711AUDIOSINK_H
#define TAS5711AUDIOSINK_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <iostream>
#include <vector>
#include "BufferedAudioSink.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

class TAS5711AudioSink : public BufferedAudioSink {
 public:
  TAS5711AudioSink();
  ~TAS5711AudioSink();

  void writeReg(uint8_t reg, uint8_t value);

 private:
  static constexpr uint8_t TAS5711_ADDR = 0x1b;
};

#endif
