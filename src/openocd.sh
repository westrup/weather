#!/bin/sh

#/opt/openocd/bin/openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nrf52.cfg -c "adapter_khz 240"
/opt/openocd/bin/openocd -f interface/stlink-v2.cfg -c "transport select hla_swd" -f target/nrf52.cfg -c "adapter_khz 240"
