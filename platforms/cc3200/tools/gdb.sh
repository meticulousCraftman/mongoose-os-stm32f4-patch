#!/bin/bash

arm-none-eabi-gdb \
  -ex 'target remote | openocd -c "gdb_port pipe" -f tools/ocd.cfg' \
  .build/mongoose-os.axf \
  "$@"

