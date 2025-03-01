#!/usr/bin/python3

import os, sys

files = ""

for i in sys.argv[1:]:

    c = f"../../../scripts/LVGLImage.py  {i} --cf ARGB8888 --ofmt C --compress LZ4"
    print(c)
    os.system(c)

