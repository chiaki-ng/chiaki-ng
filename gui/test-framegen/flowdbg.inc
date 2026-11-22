static void dump_flow(pl_gpu gpu, FrameGen &fg) {
    for (int l = FrameGen::kLevels - 1; l >= fg.debugFinestLevel(); l--) {
        pl_tex t = fg.debugFlow(l);
        int w = t->params.w, h = t->params.h;
        std::vector<float> hb(w * h * 4);
        struct pl_tex_transfer_params xp = {}; xp.tex = t; xp.ptr = hb.data();
        if (!pl_tex_download(gpu, &xp)) { std::printf("dl fail\n"); return; }
        auto hf = [](uint16_t h) { int e=(h>>10)&31,m=h&1023; float f=e==0?std::ldexp((float)m,-24):std::ldexp((float)(m|1024),e-25); return (h&0x8000)?-f:f; };
        // sample background point (x=1400,y=200) and box center (1000,480), HUD (200,995)
        auto at = [&](int X, int Y) { int x = X * w / W, y = Y * h / H; const float *p = &hb[(y*w+x)*4]; std::printf("(%.1f,%.1f c%.3f) ", p[0], p[1], p[2]); };
        std::printf("L%d %dx%d bg:", l, w, h); at(1400,200); std::printf(" box:"); at(1000,480); std::printf(" hud:"); at(200,995); std::printf("\n");
    }
}
