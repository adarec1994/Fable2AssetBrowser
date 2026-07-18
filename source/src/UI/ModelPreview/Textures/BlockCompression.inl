static void decode_bc1_block(const uint8_t* b, uint32_t* outRGBA) {
    uint16_t c0 = (uint16_t)(b[0] | (b[1]<<8));
    uint16_t c1 = (uint16_t)(b[2] | (b[3]<<8));
    uint8_t r0=ex5((c0>>11)&31), g0=ex6((c0>>5)&63),  b0=ex5(c0&31);
    uint8_t r1=ex5((c1>>11)&31), g1=ex6((c1>>5)&63),  b1=ex5(c1&31);
    uint32_t cols[4];

    cols[0] = (0xFFu<<24) | ((uint32_t)b0<<16) | ((uint32_t)g0<<8) | (uint32_t)r0;
    cols[1] = (0xFFu<<24) | ((uint32_t)b1<<16) | ((uint32_t)g1<<8) | (uint32_t)r1;
    if(c0 > c1){

        cols[2] = (0xFFu<<24) | ((uint32_t)((2*b0+b1+1)/3)<<16) | ((uint32_t)((2*g0+g1+1)/3)<<8) | (uint32_t)((2*r0+r1+1)/3);
        cols[3] = (0xFFu<<24) | ((uint32_t)((b0+2*b1+1)/3)<<16) | ((uint32_t)((g0+2*g1+1)/3)<<8) | (uint32_t)((r0+2*r1+1)/3);
    }else{
        cols[2] = (0xFFu<<24) | ((uint32_t)((b0+b1)>>1)<<16) | ((uint32_t)((g0+g1)>>1)<<8) | (uint32_t)((r0+r1)>>1);
        cols[3] = 0x00000000u;
    }
    const uint32_t idx = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
    for(int py=0; py<4; ++py){
        for(int px=0; px<4; ++px){
            int s = (idx >> (2*(py*4+px))) & 3;
            outRGBA[py*4+px] = cols[s];
        }
    }
}
static void decode_bc3_block(const uint8_t* b, uint32_t* outRGBA){
    uint8_t a0=b[0], a1=b[1];
    uint64_t abits = 0;
    for(int i=0;i<6;++i) abits |= (uint64_t)b[2+i] << (8*i);
    uint8_t atab[8];
    atab[0]=a0; atab[1]=a1;

    if(a0>a1){ for(int i=1;i<=6;i++) atab[i+1]=(uint8_t)(((7-i)*a0 + i*a1 + 3)/7); }
    else{ for(int i=1;i<=4;i++) atab[i+1]=(uint8_t)(((5-i)*a0 + i*a1 + 2)/5); atab[6]=0; atab[7]=255; }
    uint32_t color[16];
    decode_bc1_block(b+8, color);
    for(int i=0;i<16;++i){
        uint8_t ai = (uint8_t)((abits>>(3*i)) & 7);
        color[i] = (color[i] & 0x00FFFFFFu) | ( ((uint32_t)atab[ai])<<24 );
    }
    for(int i=0;i<16;++i) outRGBA[i]=color[i];
}
static void swap_bc1_endian(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 2 <= size; i += 2) {
        uint8_t t = data[i]; data[i] = data[i+1]; data[i+1] = t;
    }
}
static void swap_bc3_endian(uint8_t* data, size_t size) {
    swap_bc1_endian(data, size);
}

static void decode_bc4_block(const uint8_t* b, uint8_t* out16) {
    uint8_t a0 = b[0], a1 = b[1];
    uint64_t abits = 0;
    for (int i = 0; i < 6; ++i) abits |= (uint64_t)b[2+i] << (8*i);
    uint8_t atab[8];
    atab[0] = a0; atab[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; i++)
            atab[i+1] = (uint8_t)(((7-i)*a0 + i*a1 + 3) / 7);
    } else {
        for (int i = 1; i <= 4; i++)
            atab[i+1] = (uint8_t)(((5-i)*a0 + i*a1 + 2) / 5);
        atab[6] = 0;
        atab[7] = 255;
    }
    for (int i = 0; i < 16; ++i) {
        uint8_t ai = (uint8_t)((abits >> (3*i)) & 7);
        out16[i] = atab[ai];
    }
}

static void swap_bc5_endian(uint8_t* data, size_t size) {
    swap_bc1_endian(data, size);
}

static void blit_bc1_to_rgba(const uint8_t* src, int w, int h,
                             std::vector<uint8_t>& rgba) {
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint32_t block[16];
            decode_bc1_block(src + off, block);
            off += 8;
            for (int py = 0; py < 4; ++py) {
                int yy = (int)byy * 4 + py;
                if (yy >= h) break;
                for (int px = 0; px < 4; ++px) {
                    int xx = (int)bxx * 4 + px;
                    if (xx >= w) break;
                    ((uint32_t*)rgba.data())[yy * w + xx] = block[py * 4 + px];
                }
            }
        }
    }
}

static void blit_bc3_to_rgba(const uint8_t* src, int w, int h,
                             std::vector<uint8_t>& rgba) {
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint32_t block[16];
            decode_bc3_block(src + off, block);
            off += 16;
            for (int py = 0; py < 4; ++py) {
                int yy = (int)byy * 4 + py;
                if (yy >= h) break;
                for (int px = 0; px < 4; ++px) {
                    int xx = (int)bxx * 4 + px;
                    if (xx >= w) break;
                    ((uint32_t*)rgba.data())[yy * w + xx] = block[py * 4 + px];
                }
            }
        }
    }
}

static void blit_bc5_to_rgba(const uint8_t* src, int w, int h,
                             std::vector<uint8_t>& rgba) {
    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    rgba.assign((size_t)w * (size_t)h * 4, 0xFF);
    size_t off = 0;
    for (size_t byy = 0; byy < by; ++byy) {
        for (size_t bxx = 0; bxx < bx; ++bxx) {
            uint8_t xch[16], ych[16];
            decode_bc4_block(src + off,     xch);
            decode_bc4_block(src + off + 8, ych);
            off += 16;
            for (int py = 0; py < 4; ++py) {
                int yy = (int)byy * 4 + py;
                if (yy >= h) break;
                for (int px = 0; px < 4; ++px) {
                    int xx = (int)bxx * 4 + px;
                    if (xx >= w) break;
                    int idx = py * 4 + px;
                    int xi = xch[idx];
                    int yi = ych[idx];

                    float nx = (xi / 255.0f) * 2.0f - 1.0f;
                    float ny = (yi / 255.0f) * 2.0f - 1.0f;
                    float nz2 = 1.0f - nx*nx - ny*ny;
                    float nz = nz2 > 0.0f ? sqrtf(nz2) : 0.0f;
                    int zi = (int)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
                    if (zi < 0) zi = 0;
                    if (zi > 255) zi = 255;
                    uint8_t* p = rgba.data() + (yy * w + xx) * 4;
                    p[0] = (uint8_t)xi;
                    p[1] = (uint8_t)yi;
                    p[2] = (uint8_t)zi;
                    p[3] = 0xFF;
                }
            }
        }
    }
}
