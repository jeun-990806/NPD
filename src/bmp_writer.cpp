#include <iostream>
#include <fstream>
#include <cstring>

#include "headers/bmp.h"
#include "headers/common.h"

void put_integer(std::vector<char>& buffer, const uint v){
    char temp[4] = {
        static_cast<char>((v >> 0) & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
        static_cast<char>((v >> 16) & 0xFF),
        static_cast<char>((v >> 24) & 0xFF)
    };
    buffer.insert(buffer.end(), std::begin(temp), std::end(temp));
}

void put_short(std::vector<char>& buffer, const uint v){
    char temp[2] = {
        static_cast<char>((v >> 0) & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
    };
    buffer.insert(buffer.end(), std::begin(temp), std::end(temp));
}

void write_BMP(std::vector<uint32_t>& metadata, std::vector<std::vector<short>>& mcus, int start_dpu_index, const std::string& filename, std::vector<char>& write_buffer){
    std::ofstream output = std::ofstream(filename, std::ios::out | std::ios::binary);
    if(!output.is_open()){
        std::cout << filename << ": Error - Unable to create BMP file" << std::endl;
        return;
    }

    const int width = metadata[18];
    const int height = metadata[17];
    const int mcu_width = metadata[3];
    const uint padding = width % 4;
    const uint size = 14 + 12 + height * width * 3 + padding * height;
    const int max_blk_per_dpu = MAX_MCU_PER_DPU / 4;

    write_buffer.clear();

    write_buffer.push_back('B');
    write_buffer.push_back('M');
    put_integer(write_buffer, size);
    put_integer(write_buffer, 0);
    put_integer(write_buffer, 0x1A);
    put_integer(write_buffer, 12);
    put_short(write_buffer, width);
    put_short(write_buffer, height);
    put_short(write_buffer, 1);
    put_short(write_buffer, 24);

    for(int y=height-1; y>=0; y--){
        const uint mcu_row = y / 8;
        const uint pixel_row = y % 8;
        for(int x=0; x<width; x++){
            const uint mcu_column = x / 8;
            const uint pixel_column = x % 8;
            const uint pixel_index = pixel_row * 8 + pixel_column;

            int mcu_index = mcu_row * mcu_width + mcu_column;
            int block_index = (mcu_index / (mcu_width * 2)) * ((mcu_width + 1) / 2) + ((mcu_index % mcu_width) / 2);
            int block_position = (((mcu_index / mcu_width) % 2) * 2 + ((mcu_index % mcu_width) % 2)) * 64;
            int dpu_index = block_index / max_blk_per_dpu;

            block_index %= max_blk_per_dpu;
            block_index *= 768;

            char rgb[3] = {
                static_cast<char>(mcus[start_dpu_index + dpu_index][block_index + 512 + block_position + pixel_index]), // 2
                static_cast<char>(mcus[start_dpu_index + dpu_index][block_index + 256 + block_position + pixel_index]), // 1
                static_cast<char>(mcus[start_dpu_index + dpu_index][block_index + block_position + pixel_index]) // 0
            };
            write_buffer.insert(write_buffer.end(), std::begin(rgb), std::end(rgb));
        }
        write_buffer.resize(write_buffer.size() + padding, 0);
    }

    size_t chunk_size = 1024 * 1024;
    size_t written = 0;
    while (written < write_buffer.size()) {
        size_t to_write = std::min(chunk_size, write_buffer.size() - written);
        output.write(write_buffer.data() + written, to_write);
        written += to_write;
    }
    output.close();
}

// Fast Write Function

static inline void put_u32(std::vector<char>& buf, std::size_t& ofs, uint32_t v)
{
    std::memcpy(buf.data() + ofs, &v, 4); ofs += 4;
}
static inline void put_u16(std::vector<char>& buf, std::size_t& ofs, uint16_t v)
{
    std::memcpy(buf.data() + ofs, &v, 2); ofs += 2;
}

void write_BMP_fast(const std::vector<uint32_t>& meta,
                    const std::vector<std::vector<short>>& mcus,
                    int   start_dpu,
                    const std::string& filename)
{
    const int  W          = meta[18];
    const int  H          = meta[17]; 
    const int  mcuW       = meta[3];
    const int  maxBlkDPU  = MAX_MCU_PER_DPU / 4;
    const int  rowBytes   = W * 3;
    const int  pad        = (4 - (rowBytes & 3)) & 3;   // rowBytes%4 의 보수
    const int  pixelsSize = (rowBytes + pad) * H;
    const int  fileSize   = 14 + 12 + pixelsSize;

    std::vector<char> buf;
    buf.resize(fileSize);
    std::size_t off = 0;

    buf[off++] = 'B'; buf[off++] = 'M';
    put_u32(buf, off, fileSize);   // bfSize
    put_u32(buf, off, 0);          // bfReserved
    put_u32(buf, off, 14 + 12);    // bfOffBits (= 0x1A)

    put_u32(buf, off, 12);         // bcSize
    put_u16(buf, off, static_cast<uint16_t>(W));
    put_u16(buf, off, static_cast<uint16_t>(H));
    put_u16(buf, off, 1);          // bcPlanes
    put_u16(buf, off, 24);         // bcBitCnt

    char* const pixelBase = buf.data() + off;
    const int   stride    = rowBytes + pad;

    for (int y = 0; y < H; ++y)
    {
        const int outY   = H - 1 - y;
        char* rowPtr     = pixelBase + outY * stride;

        const int mcuRow = y >> 3;                 // y / 8
        const int pixRow = y & 7;                  // y % 8

        for (int x = 0; x < W; ++x)
        {
            const int mcuCol   = x >> 3;           // x / 8
            const int pixCol   = x & 7;            // x % 8
            const int pixIdx   = (pixRow << 3) | pixCol;     // row*8 + col

            const int mcuIdx   = mcuRow * mcuW + mcuCol;
            const int blkIdx   = (mcuIdx / (mcuW << 1)) * ((mcuW + 1) >> 1)
                               + ((mcuIdx % mcuW) >> 1);
            const int dpu      = blkIdx / maxBlkDPU;
            const int blkOff   = (blkIdx % maxBlkDPU) * 768;
            const int posOff   = (((mcuIdx / mcuW) & 1) << 1 | (mcuIdx & 1)) * 64;

            const short* src   = mcus[start_dpu + dpu].data()
                               + blkOff + posOff + pixIdx;

            rowPtr[0] = static_cast<char>(src[512]);   // Blue
            rowPtr[1] = static_cast<char>(src[256]);   // Green
            rowPtr[2] = static_cast<char>(src[0]);     // Red
            rowPtr   += 3;
        }
    }

    std::ofstream out(filename, std::ios::binary);
    if (!out) { std::cerr << filename << " : cannot open\n"; return; }
    out.write(buf.data(), buf.size());
}