#!/usr/bin/python3

import os, sys

files = ""

for i in sys.argv[1:]:

    c = f"../../../scripts/LVGLImage.py  {i} --cf RGB565 --ofmt C"
    print(c)
    os.system(c)

