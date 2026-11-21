# Frame generation test

Headless check of `FrameGen` on synthetic 1080p frames (panning background,
moving object, static HUD), comparing against the true middle frame.

```
g++ -O2 -std=c++17 -I../include fgtest.cpp ../src/framegen.cpp $(pkg-config --cflags --libs libplacebo) -o fgtest
./fgtest            # add -DFRAMEGEN_DEBUG to the compile line to dump flow vectors
```

Results on llvmpipe (PSNR vs real middle frame, higher is better):
50/50 blend 17.7 dB, frame gen 34.1 dB; moving object interior 67.6 dB; static HUD 72.2 dB.
