#!/usr/bin/env python3

# $ hcitool lewladd --random CF:17:AD:40:23:70
#
# $ hcitool lescan --whitelist --duplicates
#
# $ sudo hcidump --raw | ~/Documents/kicad/weather/weather.py
#

import time
import sys

data = ""
for line in sys.stdin:
    # print(line)
    if not line.startswith("> "):
        data = data + " " + line.strip()
        continue

    # print(data)

    if "70 23 40 AD 17 CF" in data:
        d = data.split()
        temp = int(d[40] + d[41], base=16) / 100.0
        rh = int(d[42] + d[43], base=16)
        print("%s temp: %s rh: %s" % (time.strftime("%F %T"), temp, rh))

    data = line.strip()

    # if "Data: " in line:
    #     print("%s temp: %s" % (time.strftime("%F %T"), int(line.split()[1][36:40], base=16)))
